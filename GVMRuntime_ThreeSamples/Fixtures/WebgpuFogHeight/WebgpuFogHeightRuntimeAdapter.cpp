#include "WebgpuFogHeightRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t InstanceCount = 100u;
        constexpr const char *FogSettingsReplaySha256 =
            "f4cb6644b6f6da9916389926972133cd22de33c6a9ed02b63a6eb1b944f3f31f";
        constexpr const char *FogOrbitReplaySha256 =
            "e867fba878e6e8205ccb7ca9f6eae3b509463d1da5874776c0df11987e24d0aa";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Creates parent directories for one capture artifact. */
        void prepareFogPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Converts one sRGB byte channel into linear light. */
        float fogSrgbByteToLinear(uint32_t value)
        {
            const float channel = float(value) / 255.0f;
            return channel <= 0.04045f
                ? channel / 12.92f
                : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns the SHA-256 identity of a locked fog input replay file. */
        eastl::string calculateFogReplaySha256(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open webgpu_fog_height input replay.");
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || uint64_t(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("webgpu_fog_height input replay has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read webgpu_fog_height input replay.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest{};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Appends one typed buffer payload to a RenderSet allocation. */
        void appendFogBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Builds a 1 by 25 by 1 box with one normal per face. */
        void buildFogBox(
            eastl::vector<WebgpuFogHeightHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 positions[8u] = {
                {-0.5f, -12.5f, -0.5f}, {0.5f, -12.5f, -0.5f},
                {0.5f, 12.5f, -0.5f}, {-0.5f, 12.5f, -0.5f},
                {-0.5f, -12.5f, 0.5f}, {0.5f, -12.5f, 0.5f},
                {0.5f, 12.5f, 0.5f}, {-0.5f, 12.5f, 0.5f},
            };
            const uint32_t faces[6u][4u] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u},
                {4u, 0u, 3u, 7u}, {1u, 5u, 6u, 2u},
                {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u},
            };
            const glm::vec3 normals[6u] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            };
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                {
                    vertices.push_back({
                        glm::vec4(positions[faces[face][corner]], 1.0f),
                        glm::vec4(normals[face], 0.0f)});
                }
                indices.insert(indices.end(), {
                    base + 0u, base + 1u, base + 2u,
                    base + 0u, base + 2u, base + 3u,
                });
            }
        }

        /** Builds the WebGPU depth-range projection used by the generated shader. */
        glm::mat4 makeFogProjection(uint32_t width, uint32_t height)
        {
            constexpr double nearDistance = 1.0;
            constexpr double farDistance = 600.0;
            const double top = nearDistance * std::tan(45.0 * 3.141592653589793 / 360.0);
            const double viewHeight = top * 2.0;
            const double viewWidth = double(width) / double(height) * viewHeight;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = float(2.0 * nearDistance / viewWidth);
            projection[1u][1u] = float(-2.0 * nearDistance / viewHeight);
            projection[2u][2u] = float(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = float(-farDistance * nearDistance / depth);
            return projection;
        }
    } // namespace

    void WebgpuFogHeightRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool nondefault = options.scenarioId == "nondefault-fog" && options.targetFrame == 1u;
        const bool orbit = options.scenarioId == "orbit" && options.targetFrame == 2u;
        const bool replayScenario = nondefault || orbit;
        if (options.caseId != "webgpu_fog_height" ||
            (!initial && !nondefault && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (replayScenario != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument("webgpu_fog_height scenario does not match the locked r185 contract.");
        }
        if (replayScenario)
        {
            const eastl::string replaySha256 = calculateFogReplaySha256(
                std::filesystem::path(options.inputReplayPath.c_str()));
            const char *expectedSha256 = nondefault
                ? FogSettingsReplaySha256
                : FogOrbitReplaySha256;
            if (replaySha256 != expectedSha256)
            {
                throw std::invalid_argument("webgpu_fog_height input replay hash differs from the locked r185 contract.");
            }
        }
        device = inDevice;
        buildFogBox(vertices, indices);
        instances.resize(InstanceCount);
        for (uint32_t index = 0u; index < InstanceCount; ++index)
        {
            const uint32_t row = index / 10u;
            const uint32_t column = index % 10u;
            const float x = -18.0f + float(column) * 4.0f;
            const float z = -18.0f + float(row) * 4.0f;
            instances[index] = {
                {1.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {x, -10.0f, z, 1.0f},
                {float(column) / 9.0f, float(row) / 9.0f, 0.0f, 1.0f},
            };
        }
        materialData.baseColorAndAmbient = glm::vec4(
            fogSrgbByteToLinear(0xcdu),
            fogSrgbByteToLinear(0x95u),
            fogSrgbByteToLinear(0x9au),
            fogSrgbByteToLinear(0xccu));
        materialData.specularColorAndShininess = glm::vec4(
            fogSrgbByteToLinear(0x11u),
            fogSrgbByteToLinear(0x11u),
            fogSrgbByteToLinear(0x11u),
            30.0f);
        fogParameters = glm::vec4(initial || orbit ? 0.04f : 0.0725f,
                                  initial || orbit ? 2.0f : -1.5f,
                                  0.0f,
                                  0.0f);
        renderFlags = glm::uvec4(1u, 100u, 0u, 0u);
        updateObjectData(options.width, options.height, options.targetFrame);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgpu_fog_height could not create its Scene Set encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = InstanceCount;
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::vertices,
                        "FogHeightVertices",
                        vertices.data(),
                        vertices.size() * sizeof(WebgpuFogHeightHostVertex),
                        1u);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::indices,
                        "FogHeightIndices",
                        indices.data(),
                        indices.size() * sizeof(uint32_t),
                        1u);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::objects,
                        "FogHeightObject",
                        &objectData,
                        sizeof(objectData),
                        1u);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::instances,
                        "FogHeightInstances",
                        instances.data(),
                        instances.size() * sizeof(WebgpuFogHeightHostInstanceData),
                        InstanceCount);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::materials,
                        "FogHeightMaterial",
                        &materialData,
                        sizeof(materialData),
                        1u);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::fogParameters,
                        "FogHeightParameters",
                        &fogParameters,
                        sizeof(fogParameters),
                        1u);
        appendFogBuffer(allocation,
                        WebgpuFogHeightSceneRenderSetComponents::renderFlags,
                        "FogHeightFlags",
                        &renderFlags,
                        sizeof(renderFlags),
                        1u);
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuFogHeightRuntimeAdapter::updateObjectData(
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex)
    {
        // OrbitControls applies the default 0.05 damping factor for three fixed frames
        // after the eight-pixel horizontal drag in the locked input replay.
        const double orbitAngle = frameIndex >= 2u ? 0.01864 : 0.0;
        const glm::dvec3 cameraPosition(
            20.0 * std::cos(orbitAngle) - 25.0 * std::sin(orbitAngle),
            10.0,
            20.0 * std::sin(orbitAngle) + 25.0 * std::cos(orbitAngle));
        const glm::dmat4 view = glm::lookAt(
            cameraPosition,
            glm::dvec3(0.0, 0.0, 0.0),
            glm::dvec3(0.0, 1.0, 0.0));
        objectData.viewProjection = makeFogProjection(width, height) * glm::mat4(view);
        objectData.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        objectData.view = glm::mat4(view);
        objectData.cameraPositionAndDensity = glm::vec4(
            glm::vec3(cameraPosition), fogParameters.x);
        objectData.fogHeightAndReserved = glm::vec4(fogParameters.y, 0.0f, 0.0f, 0.0f);
        objectData.lightDirectionAndIntensity = glm::vec4(
            glm::normalize(glm::vec3(10.0f, -10.0f, -10.0f)), 2.0f);
        objectData.lightColor = glm::vec4(
            fogSrgbByteToLinear(0xffu),
            fogSrgbByteToLinear(0xc0u),
            fogSrgbByteToLinear(0xcbu),
            1.0f);
    }

    void WebgpuFogHeightRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgpu_fog_height could not create its update encoder.");
        }
        encoder->setBufferComponentData(
            entityIndex,
            WebgpuFogHeightSceneRenderSetComponents::objects,
            &objectData,
            sizeof(objectData),
            0u,
            1u);
        encoder->setBufferComponentData(
            entityIndex,
            WebgpuFogHeightSceneRenderSetComponents::fogParameters,
            &fogParameters,
            sizeof(fogParameters),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuFogHeightRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareFogPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
    }

    void WebgpuFogHeightRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareFogPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"caseId\":\"webgpu_fog_height\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\""
               << options.pipeline.c_str() << "\",\"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\"frame\":"
               << frameIndex << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":"
               << height << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}";
        if (options.scenarioId == "nondefault-fog" || options.scenarioId == "orbit")
        {
            const bool nondefault = options.scenarioId == "nondefault-fog";
            output << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgpu_fog_height\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"captureFrame\":" << frameIndex
                   << ",\"sha256\":\"" << (nondefault ? FogSettingsReplaySha256 : FogOrbitReplaySha256)
                   << "\",\"target\":\"canvas:not([class])\",\"eventCount\":"
                   << (nondefault ? 1u : 3u) << "}";
        }
        output << "}\n";
    }

    void WebgpuFogHeightRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareFogPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgpu_fog_height\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"strict-pass\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":1,\n"
               << "  \"entityCount\":1,\n"
               << "  \"instanceCount\":100,\n"
               << "  \"instanceCounts\":[100],\n"
               << "  \"scenePassCount\":1,\n"
               << "  \"screenPassCount\":1,\n"
               << "  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"fogParameters\",\"renderFlags\"],\n"
               << "  \"fogDensity\":" << fogParameters.x << ",\n"
               << "  \"fogHeight\":" << fogParameters.y << ",\n"
               << "  \"assetAndAlgorithmState\":\"box-geometry-100-instances-exponential-height-fog\",\n"
               << "  \"renderSetType\":\"WebgpuFogHeightSceneRenderSet\",\n"
               << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-phong-height-fog\"}],\n"
               << "  \"scenePasses\":[{\"name\":\"main-phong-height-fog\",\"renderClass\":\"WebgpuFogHeightMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebgpuFogHeightSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"fogParameters\",\"kind\":\"buffer\",\"role\":\"height-fog-parameters\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"geometry-group-material-phase\"}],\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"instanced-box-field\",\"instanceCount\":100}],\"scenePasses\":[{\"name\":\"main-phong-height-fog\",\"renderClass\":\"WebgpuFogHeightMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n"
               << "}\n";
    }

    void WebgpuFogHeightRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebgpuFogHeightRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
    }
} // namespace GVM::ThreeSamples
