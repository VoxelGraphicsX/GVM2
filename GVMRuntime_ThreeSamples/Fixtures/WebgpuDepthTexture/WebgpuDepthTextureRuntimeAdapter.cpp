#include "WebgpuDepthTextureRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <CommonCrypto/CommonDigest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t DepthEntityCount = 50u;
        constexpr uint32_t TubularSegments = 128u;
        constexpr uint32_t RadialSegments = 64u;
        constexpr uint32_t TorusKnotVertexCount =
            (TubularSegments + 1u) * (RadialSegments + 1u);
        constexpr uint32_t TorusKnotIndexCount =
            TubularSegments * RadialSegments * 6u;
        // The locked r185 module graph consumes deterministic UUID draws before
        // the example loop, and each default Mesh/Material pair consumes eight more.
        constexpr uint32_t PreSceneRandomCallCount = 192u;
        constexpr uint32_t MeshUuidRandomCallCount = 8u;
        constexpr uint32_t InitialSceneRandomState = 1513084622u;
        constexpr float OrbitDragPixelsX = 60.0f;
        constexpr float OrbitDragPixelsY = -30.0f;
        // The replay dispatches one pointer update followed by two animation
        // frames. OrbitControls applies damping on all three updates.
        constexpr float OrbitDampedDragFactor = 0.05f;
        constexpr float OrbitCanvasHeight = 500.0f;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuDepthTextureHostVertex) == 16u);
        static_assert(sizeof(WebgpuDepthTextureHostObjectData) == 64u);
        static_assert(sizeof(WebgpuDepthTextureHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuDepthTextureHostMaterialData) == 16u);

        /** Creates a parent directory for one depth capture. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Calculates the SHA-256 identity of the locked input replay file. */
        std::string calculateReplaySha256(const eastl::string &path)
        {
            if (path.empty()) return {};
            std::ifstream input(std::filesystem::path(path.c_str()),
                                std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open depth texture input replay.");
            const std::streamoff size = input.tellg();
            if (size <= 0) throw std::runtime_error("Depth texture input replay is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input) throw std::runtime_error("Could not read depth texture input replay.");
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Evaluates Three's default p=2, q=3 TorusKnot center curve. */
        glm::dvec3 calculateTorusKnotPosition(double u)
        {
            const double cosineU = std::cos(u);
            const double sineU = std::sin(u);
            const double qOverP = 1.5 * u;
            const double cosineQ = std::cos(qOverP);
            return {
                (2.0 + cosineQ) * 0.5 * cosineU,
                (2.0 + cosineQ) * 0.5 * sineU,
                std::sin(qOverP) * 0.5,
            };
        }

        /** Generates the exact indexed TorusKnotGeometry vertex and triangle order. */
        void buildTorusKnotGeometry(
            eastl::vector<WebgpuDepthTextureHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(TorusKnotVertexCount);
            indices.reserve(TorusKnotIndexCount);
            for (uint32_t tubular = 0u; tubular <= TubularSegments; ++tubular)
            {
                const double u = double(tubular) / double(TubularSegments) * 4.0 * Pi;
                const glm::dvec3 center = calculateTorusKnotPosition(u);
                const glm::dvec3 ahead = calculateTorusKnotPosition(u + 0.01);
                const glm::dvec3 tangent = ahead - center;
                const glm::dvec3 normalSeed = ahead + center;
                glm::dvec3 binormal = glm::cross(tangent, normalSeed);
                glm::dvec3 normalBasis = glm::cross(binormal, tangent);
                binormal = glm::normalize(binormal);
                normalBasis = glm::normalize(normalBasis);
                for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
                {
                    const double v = double(radial) / double(RadialSegments) * 2.0 * Pi;
                    const glm::dvec3 position = center -
                        0.3 * std::cos(v) * normalBasis +
                        0.3 * std::sin(v) * binormal;
                    vertices.push_back({glm::vec4(
                        float(position.x), float(position.y), float(position.z), 1.0f)});
                }
            }
            for (uint32_t tubular = 1u; tubular <= TubularSegments; ++tubular)
            {
                for (uint32_t radial = 1u; radial <= RadialSegments; ++radial)
                {
                    const uint32_t a = (RadialSegments + 1u) * (tubular - 1u) + radial - 1u;
                    const uint32_t b = (RadialSegments + 1u) * tubular + radial - 1u;
                    const uint32_t c = (RadialSegments + 1u) * tubular + radial;
                    const uint32_t d = (RadialSegments + 1u) * (tubular - 1u) + radial;
                    indices.insert(indices.end(), {a, b, d, b, c, d});
                }
            }
            if (vertices.size() != TorusKnotVertexCount || indices.size() != TorusKnotIndexCount)
                throw std::runtime_error("webgpu_depth_texture TorusKnot counts diverged from r185.");
        }

        /** Builds the zero-to-one, Y-inverted camera projection used by WebGPU. */
        glm::mat4 makeDepthPerspective(uint32_t width, uint32_t height)
        {
            constexpr double nearDistance = 1.0;
            constexpr double farDistance = 20.0;
            const double top = nearDistance * std::tan(70.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth = projectionHeight * double(width) / double(height);
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = float(2.0 * nearDistance / projectionWidth);
            projection[1u][1u] = float(-2.0 * nearDistance / projectionHeight);
            projection[2u][2u] = float(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = float(-farDistance * nearDistance / depth);
            return projection;
        }

        /** Builds the camera view matrix after the one-frame OrbitControls replay. */
        glm::mat4 makeDepthViewProjection(uint32_t width, uint32_t height,
                                          uint32_t frameIndex)
        {
            glm::mat4 view = glm::translate(glm::mat4(1.0f),
                                            glm::vec3(0.0f, 0.0f, -4.0f));
            if (frameIndex == 1u)
            {
                const float theta = -2.0f * float(Pi) * OrbitDragPixelsX /
                    OrbitCanvasHeight * OrbitDampedDragFactor;
                const float phi = 0.5f * float(Pi) -
                    2.0f * float(Pi) * OrbitDragPixelsY /
                    OrbitCanvasHeight * OrbitDampedDragFactor;
                const glm::vec3 cameraPosition(
                    4.0f * std::sin(phi) * std::sin(theta),
                    4.0f * std::cos(phi),
                    4.0f * std::sin(phi) * std::cos(theta));
                view = glm::lookAt(cameraPosition, glm::vec3(0.0f),
                                   glm::vec3(0.0f, 1.0f, 0.0f));
            }
            return makeDepthPerspective(width, height) * view;
        }

        void consumeDepthMeshUuidRandomDraws(ThreeCompat::DeterministicRandom &random);

        /** Returns the seeded object transform used by one canonical frame. */
        glm::mat4 makeDepthObjectTransform(
            ThreeCompat::DeterministicRandom &random,
            uint32_t frameIndex)
        {
            const float angle = random.nextFloat() * float(2.0 * Pi);
            const float z = random.nextFloat() * 2.0f - 1.0f;
            const float zScale = std::sqrt(std::max(0.0f, 1.0f - z * z)) * 5.0f;
            consumeDepthMeshUuidRandomDraws(random);
            const glm::vec3 position(
                std::cos(angle) * zScale,
                std::sin(angle) * zScale,
                z * 5.0f);
            const float rotationX = random.nextFloat();
            const float rotationY = random.nextFloat();
            const float rotationZ = random.nextFloat();
            glm::mat4 model(1.0f);
            model = glm::translate(model, position);
            model = glm::rotate(model, rotationX, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, rotationZ, glm::vec3(0.0f, 0.0f, 1.0f));
            return model;
        }

        /** Advances the shared stream to the first example object draw. */
        void advanceToDepthSceneRandomStream(ThreeCompat::DeterministicRandom &random)
        {
            for (uint32_t call = 0u; call < PreSceneRandomCallCount; ++call)
                (void)random.nextUint32();
        }

        /** Consumes the UUID draws made by one default Mesh and Material pair. */
        void consumeDepthMeshUuidRandomDraws(ThreeCompat::DeterministicRandom &random)
        {
            for (uint32_t call = 0u; call < MeshUuidRandomCallCount; ++call)
                (void)random.nextUint32();
        }

        /** Validates the initial and orbit scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool orbit = options.scenarioId == "orbit" && options.targetFrame == 1u;
            if (options.caseId != "webgpu_depth_texture" || (!initial && !orbit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgpu_depth_texture scenario does not match the locked r185 contract.");
        }
    } // namespace

    void WebgpuDepthTextureRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        viewProjection = makeDepthViewProjection(options.width, options.height, 0u);
        eastl::vector<WebgpuDepthTextureHostVertex> canonicalVertices;
        eastl::vector<uint32_t> canonicalIndices;
        buildTorusKnotGeometry(canonicalVertices, canonicalIndices);
        entities.clear();
        entities.resize(DepthEntityCount);
        ThreeCompat::DeterministicRandom random(DefaultThreeRandomSeed);
        advanceToDepthSceneRandomStream(random);
        for (uint32_t index = 0u; index < DepthEntityCount; ++index)
        {
            auto &entity = entities[index];
            entity.vertices = canonicalVertices;
            entity.indices = canonicalIndices;
            entity.objectData.modelViewProjection = viewProjection *
                makeDepthObjectTransform(random, 0u);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColor = glm::vec4(1.0f);
        }
        if (random.getState() != InitialSceneRandomState)
            throw std::runtime_error("webgpu_depth_texture random stream diverged from the locked r185 object stream.");
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_depth_texture could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < DepthEntityCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("DepthEntity-") + eastl::to_string(index);
            appendBuffer(allocation, WebgpuDepthTextureSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(),
                         entity.vertices.size() * sizeof(WebgpuDepthTextureHostVertex), 1u);
            appendBuffer(allocation, WebgpuDepthTextureSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t), 1u);
            appendBuffer(allocation, WebgpuDepthTextureSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendBuffer(allocation, WebgpuDepthTextureSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendBuffer(allocation, WebgpuDepthTextureSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuDepthTextureRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        viewProjection = makeDepthViewProjection(800u, 500u, frameIndex);
        ThreeCompat::DeterministicRandom random(DefaultThreeRandomSeed);
        advanceToDepthSceneRandomStream(random);
        for (uint32_t index = 0u; index < entities.size(); ++index)
            entities[index].objectData.modelViewProjection = viewProjection *
                makeDepthObjectTransform(random, frameIndex);
    }

    void WebgpuDepthTextureRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                       const ThreeSampleHostOptions &options,
                                                       uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_depth_texture could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                WebgpuDepthTextureSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuDepthTextureRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                            const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebgpuDepthTextureRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                                uint32_t frameIndex, uint32_t width,
                                                                uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgpu_depth_texture\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false";
        if (!options.inputReplayPath.empty())
        {
            output << ",\"inputReplay\":{\"sha256\":\""
                   << calculateReplaySha256(options.inputReplayPath)
                   << "\",\"caseId\":\"webgpu_depth_texture\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"captureFrame\":1,\"eventCount\":3,\"target\":\"body > canvas\"}";
        }
        output << "}\n";
    }

    void WebgpuDepthTextureRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                                   uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgpu_depth_texture\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebgpuDepthTextureSceneRenderSet\",\n  \"renderableObjectCount\":50,\n"
               << "  \"entityCount\":50,\n  \"instanceCount\":50,\n  \"instanceCounts\":[1],\n"
               << "  \"vertexCount\":8385,\n  \"indexCount\":49152,\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":1,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"assetAndAlgorithmState\":\"r185-torus-knot-depth32float-fullscreen-visualization\",\n"
               << "  \"scenePasses\":[{\"name\":\"scene-depth\",\"renderClass\":\"WebgpuDepthTextureScenePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"screenPasses\":[{\"name\":\"depth-texture-visualization\",\"renderClass\":\"WebgpuDepthTextureScreenPass\",\"samplesDepthTexture\":true,\"redrawsSceneGeometry\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebgpuDepthTextureSceneRenderSet\",\"renderableObjectCount\":50,\"entityCount\":50,\"entities\":[";
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\"depth-"
                   << index << "\",\"instanceCount\":1}";
        }
        output << "],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"scene-depth\",\"renderClass\":\"WebgpuDepthTextureScenePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"scene-depth\",\"entityOrdinal\":0}]}\n";
    }

    void WebgpuDepthTextureRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                      const ThreeSampleHostOptions &options,
                                                      uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                      uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgpu_depth_texture capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgpu_depth_texture has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebgpuDepthTextureRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                    const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
