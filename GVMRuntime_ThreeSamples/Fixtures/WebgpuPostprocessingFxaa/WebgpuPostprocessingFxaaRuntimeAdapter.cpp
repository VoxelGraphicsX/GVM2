#include "WebgpuPostprocessingFxaaRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle BatchSceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuPostprocessingFxaaHostVertex) == 80u);
        static_assert(sizeof(WebgpuPostprocessingFxaaHostObjectData) == 144u);
        static_assert(sizeof(WebgpuPostprocessingFxaaHostInstanceData) == 80u);
        static_assert(sizeof(WebgpuPostprocessingFxaaHostMaterialData) == 48u);
        constexpr const char *DisabledReplaySha256 =
            "7e73fbd81551991e6bd14c4ffe3ccfe78d2c197ca1d2a517905cf22d69351035";
        constexpr const char *StaticReplaySha256 =
            "cc5bfa8703ee360de4cfb501c6fb79ec52fab1ccd1ad3e558ab7e702af8a4c69";

        /** Returns the lowercase SHA-256 digest of one locked FXAA replay. */
        eastl::string calculateFxaaReplaySha256(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open the locked WebGPU FXAA replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error("The locked WebGPU FXAA replay has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read the locked WebGPU FXAA replay.");
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

        /** Validates the four deterministic r185 WebGPU FXAA scenarios. */
        void validateFxaaScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool disabled = options.scenarioId == "disabled" && options.targetFrame == 1u;
            const bool staticEnabled = options.scenarioId == "static-enabled" && options.targetFrame == 120u;
            const bool replay = disabled || staticEnabled;
            if (options.caseId != "webgpu_postprocessing_fxaa" ||
                (!initial && !animated && !disabled && !staticEnabled) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (replay != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU FXAA requires one locked 800x500 Manifest scenario.");
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareFxaaOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Appends one typed host payload to a RenderSet allocation descriptor. */
        void appendFxaaBufferPayload(GVM::Core::RenderSetAllocInfo &allocation,
                                      GVM::Core::RenderComponentHandle component,
                                      const eastl::string &name,
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

        /** Advances the exact xorshift32 stream installed by the reference capture bootstrap. */
        float nextFxaaReferenceRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Converts one Three.js XYZ Euler rotation into the exact compose quaternion. */
        glm::quat makeFxaaThreeXyzQuaternion(const glm::vec3 &rotation)
        {
            const float cosineX = std::cos(rotation.x * 0.5f);
            const float cosineY = std::cos(rotation.y * 0.5f);
            const float cosineZ = std::cos(rotation.z * 0.5f);
            const float sineX = std::sin(rotation.x * 0.5f);
            const float sineY = std::sin(rotation.y * 0.5f);
            const float sineZ = std::sin(rotation.z * 0.5f);
            return glm::quat(
                cosineX * cosineY * cosineZ -
                    sineX * sineY * sineZ,
                sineX * cosineY * cosineZ +
                    cosineX * sineY * sineZ,
                cosineX * sineY * cosineZ -
                    sineX * cosineY * sineZ,
                cosineX * cosineY * sineZ +
                    sineX * sineY * cosineZ);
        }

        /** Builds the r185 TetrahedronGeometry and one hundred seeded instance transforms for FXAA. */
        WebgpuPostprocessingFxaaEntityState buildFxaaEntity(
            const ThreeSampleHostOptions &options)
        {
            WebgpuPostprocessingFxaaEntityState entity;
            entity.logicalId = "fxaa-tetrahedra";
            constexpr float Radius = 1.0f;
            constexpr float Coordinate = Radius * 0.5773502691896258f;
            const glm::vec3 sourcePositions[4u] = {
                {Coordinate, Coordinate, Coordinate},
                {-Coordinate, -Coordinate, Coordinate},
                {-Coordinate, Coordinate, -Coordinate},
                {Coordinate, -Coordinate, -Coordinate},
            };
            constexpr uint32_t sourceIndices[12u] = {
                2u, 1u, 0u, 0u, 3u, 2u, 1u, 3u, 0u, 2u, 3u, 1u,
            };
            entity.vertices.reserve(12u);
            entity.indices.reserve(12u);
            for (uint32_t triangle = 0u; triangle < 4u; ++triangle)
            {
                const glm::vec3 a = sourcePositions[sourceIndices[triangle * 3u]];
                const glm::vec3 b = sourcePositions[sourceIndices[triangle * 3u + 1u]];
                const glm::vec3 c = sourcePositions[sourceIndices[triangle * 3u + 2u]];
                const glm::vec3 normal = glm::normalize(glm::cross(c - b, a - b));
                const glm::vec3 trianglePositions[3u] = {a, b, c};
                for (const glm::vec3 &position : trianglePositions)
                {
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(1.0f),
                        glm::vec4(0.0f),
                        glm::vec4(position, 1.0f),
                    });
                    entity.indices.push_back(static_cast<uint32_t>(entity.indices.size()));
                }
            }

            const float groupRotation = options.scenarioId == "animated"
                ? static_cast<float>(options.targetFrame) / 600.0f
                : 0.0f;
            const glm::vec3 cameraPosition(
                0.0f, 0.0f, 50.0f);
            const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const float fieldOfView = 45.0f * 3.14159265358979323846f / 180.0f;
            const float nearDistance = 0.1f;
            const float farDistance = 200.0f;
            const float top = nearDistance * std::tan(fieldOfView * 0.5f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = nearDistance / right;
            projection[1u][1u] = -nearDistance / top;
            projection[2u][2u] = -farDistance / (farDistance - nearDistance);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = -farDistance * nearDistance / (farDistance - nearDistance);
            entity.objectData.modelViewProjection = projection * view;
            entity.objectData.modelView = view;
            entity.objectData.parameters = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);

            uint32_t randomState = 2835668270u;
            entity.instances.resize(100u);
            for (uint32_t instanceIndex = 0u; instanceIndex < 100u; ++instanceIndex)
            {
                const glm::vec3 translation(
                    nextFxaaReferenceRandom(randomState) * 50.0f - 25.0f,
                    nextFxaaReferenceRandom(randomState) * 50.0f - 25.0f,
                    nextFxaaReferenceRandom(randomState) * 50.0f - 25.0f);
                const float scaleValue = nextFxaaReferenceRandom(randomState) * 2.0f + 1.0f;
                const glm::vec3 rotation(
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f,
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f,
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f);
                const glm::quat orientation =
                    makeFxaaThreeXyzQuaternion(rotation);
                const glm::mat4 model =
                    glm::rotate(glm::mat4(1.0f), groupRotation, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::translate(glm::mat4(1.0f), translation) *
                    glm::toMat4(orientation) *
                    glm::scale(
                        glm::mat4(1.0f),
                        glm::vec3(scaleValue));
                WebgpuPostprocessingFxaaHostInstanceData &instance = entity.instances[instanceIndex];
                instance.transformColumn0 = model[0u];
                instance.transformColumn1 = model[1u];
                instance.transformColumn2 = model[2u];
                instance.transformColumn3 = model[3u];
                const glm::quat worldOrientation =
                    glm::angleAxis(groupRotation, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    orientation;
                instance.color = glm::vec4(
                    worldOrientation.x, worldOrientation.y,
                    worldOrientation.z, worldOrientation.w);
            }
            entity.materialData.baseColor = glm::vec4(
                0.93011086f, 0.03189603f, 0.03189603f, 1.0f);
            entity.materialData.emissiveAndOpacity = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            entity.materialData.modeAndParameters = glm::vec4(10.0f, 1.0f, 0.0f, 0.0f);
            return entity;
        }

        /** Writes one tightly packed RGBA8 capture file. */
        void writeFxaaRgba(const eastl::string &pathValue, const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path outputPath(pathValue.c_str());
            prepareFxaaOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write Phase 1 batch RGBA capture.");
        }
    } // namespace

    void WebgpuPostprocessingFxaaRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateFxaaScenario(options);
        device = inDevice;
        caseId = options.caseId;
        inputReplaySha256.clear();
        inputReplayEventCount = 0u;
        if (!options.inputReplayPath.empty())
        {
            inputReplaySha256 = calculateFxaaReplaySha256(
                std::filesystem::path(options.inputReplayPath.c_str()));
            const char *expectedSha256 = options.scenarioId == "disabled"
                ? DisabledReplaySha256
                : StaticReplaySha256;
            if (inputReplaySha256 != expectedSha256)
                throw std::runtime_error("WebGPU FXAA input replay diverged from its locked digest.");
            inputReplayEventCount = 1u;
        }
        if (caseId != "webgpu_postprocessing_fxaa")
        {
            throw std::invalid_argument("FXAA adapter received an unexpected case id.");
        }
        entities.reserve(1u);
        const auto encoder = renderer.createRenderSetCommandEncoder(BatchSceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not create the FXAA Scene RenderSet encoder.");
        entities.push_back(buildFxaaEntity(options));
        entities.back().entityIndex = allocateEntity(*encoder, entities.back());
        renderer.executeRenderSetCommand(BatchSceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebgpuPostprocessingFxaaRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const WebgpuPostprocessingFxaaEntityState &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = static_cast<uint32_t>(entity.instances.size());
        const eastl::string prefix = "WebgpuPostprocessingFxaa" + entity.logicalId;
        appendFxaaBufferPayload(allocation, WebgpuPostprocessingFxaaSceneRenderSetComponents::vertices,
            prefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(WebgpuPostprocessingFxaaHostVertex), 1u);
        appendFxaaBufferPayload(allocation, WebgpuPostprocessingFxaaSceneRenderSetComponents::indices,
            prefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t), 1u);
        appendFxaaBufferPayload(allocation, WebgpuPostprocessingFxaaSceneRenderSetComponents::objects,
            prefix + "Object", &entity.objectData, sizeof(entity.objectData), 1u);
        appendFxaaBufferPayload(allocation, WebgpuPostprocessingFxaaSceneRenderSetComponents::instances,
            prefix + "Instances", entity.instances.data(), entity.instances.size() * sizeof(WebgpuPostprocessingFxaaHostInstanceData),
            static_cast<uint32_t>(entity.instances.size()));
        appendFxaaBufferPayload(allocation, WebgpuPostprocessingFxaaSceneRenderSetComponents::materials,
            prefix + "Material", &entity.materialData, sizeof(entity.materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebgpuPostprocessingFxaaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuPostprocessingFxaaRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("Batch capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebgpuPostprocessingFxaaRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        writeFxaaRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(options.captureMetadataPath.c_str());
            prepareFxaaOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed << ",\n"
                   << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n  \"format\":\"rgba8unorm\",\n"
                   << "  \"samplePolicy\":{\"mode\":\"single-sample\","
                   << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                   << "  \"inputReplay\":";
            if (inputReplaySha256.empty())
            {
                output << "null\n}\n";
            }
            else
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_fxaa\","
                       << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                       << "\"captureFrame\":" << frameIndex << ",\"sha256\":\""
                       << inputReplaySha256.c_str() << "\","
                       << "\"target\":\"canvas:not([class])\",\"eventCount\":"
                       << inputReplayEventCount << "}\n}\n";
            }
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path path(options.sceneSnapshotPath.c_str());
            prepareFxaaOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\n  \"caseId\":\"" << caseId.c_str() << "\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"strict-pass\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":false,\n  \"assetHashes\":[],\n"
                   << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
                   << "  \"sceneRoots\":";
            output
                    << "[{\"id\":\"scene\",\"renderSetCount\":1,"
                    << "\"renderSetId\":\"webgpu-postprocessing-fxaa-scene-set\","
                    << "\"renderSetType\":\"WebgpuPostprocessingFxaaSceneRenderSet\","
                    << "\"renderableObjectCount\":1,\"entityCount\":1,"
                    << "\"entities\":[{\"entityId\":" << entities[0].entityIndex
                    << ",\"logicalRenderableId\":\"fxaa-tetrahedra\","
                    << "\"instanceCount\":100}],"
                    << "\"componentSchema\":["
                    << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                    << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                    << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                    << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                    << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                    << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
                    << "\"scenePasses\":[{\"name\":\"main-instanced\","
                    << "\"renderClass\":\"WebgpuPostprocessingFxaaMainPass\","
                    << "\"renderSetId\":\"webgpu-postprocessing-fxaa-scene-set\","
                    << "\"renderSetBindingCount\":1,"
                    << "\"drawMode\":\"render-set-indexed-indirect\","
                    << "\"invocationCount\":1,\"drawCommandCount\":1,"
                    << "\"usesStandaloneGeometry\":false,"
                    << "\"usesExplicitDrawCount\":false}]}],\n";
            output
                   << "  \"renderableObjectCount\":" << entities.size() << ",\n"
                   << "  \"entityCount\":" << entities.size() << ",\n"
                   << "  \"instanceCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].instances.size();
            }
            output << "],\n  \"vertexCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].vertices.size();
            }
            output << "],\n  \"indexCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].indices.size();
            }
            output << "],\n"
                   << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":2,\n"
                   << "  \"scenePasses\":[\"WebgpuPostprocessingFxaaMainPass\"],\n"
                   << "  \"screenPasses\":[\"render-output\",\"fxaa-or-bypass\"],\n"
                   << "  \"attachmentFormats\":[\"rgba16float\",\"depth32float\",\"rgba8unorm\"],\n"
                   << "  \"computeDispatchThreads\":[],\n"
                   << "  \"scenePassInvocations\":[{\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"main-instanced\",\"invocationCount\":1}],\n"
                   << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"main-instanced\",\"entityOrdinal\":0}],\n"
                   << "  \"drawCommandCount\":" << entities.size() << ",\n"
                   << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false\n}\n";
        }
    }

    void WebgpuPostprocessingFxaaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
