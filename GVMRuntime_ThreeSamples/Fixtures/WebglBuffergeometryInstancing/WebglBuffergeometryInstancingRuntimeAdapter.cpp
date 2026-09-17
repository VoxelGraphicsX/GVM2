#include "WebglBuffergeometryInstancingRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t CanonicalRandomSeed = 0x18500006u;
        constexpr uint32_t CanonicalInstanceCount = 50000u;
        constexpr uint32_t ReducedInstanceCount = 12500u;
        constexpr uint32_t UpstreamRandomDrawsBeforeInstances = 84u;
        constexpr uint32_t RandomDrawsPerInstance = 15u;
        constexpr uint32_t ExpectedFinalRandomState = 305162645u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *ReducedCountReplaySha256 = "86d78787cea1280f621080011ac73972fada9640799c13f350df8c756e2a8f18";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(BuffergeometryInstancingHostFloat4) == 16u);
        static_assert(sizeof(BuffergeometryInstancingHostUint4) == 16u);
        static_assert(sizeof(BuffergeometryInstancingHostVertex) == 16u);
        static_assert(sizeof(BuffergeometryInstancingHostObjectData) == 96u);
        static_assert(sizeof(BuffergeometryInstancingHostInstanceData) == 64u);
        static_assert(sizeof(BuffergeometryInstancingHostMaterialData) == 16u);
        static_assert(UpstreamRandomDrawsBeforeInstances + CanonicalInstanceCount * RandomDrawsPerInstance == 750084u);

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareInstancingOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeInstancingRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("webgl_buffergeometry_instancing RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded file as exact bytes for immutable replay validation. */
        eastl::vector<uint8_t> readInstancingFileBytes(const std::filesystem::path &inputPath, const char *label)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open " + std::string(label) + ": " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(std::string(label) + " has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete " + std::string(label) + ".");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateInstancingSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
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

        /** Resolves the explicitly supplied canonical replay without environment configuration. */
        std::filesystem::path resolveInstancingReplayPath(const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(options.inputReplayPath.c_str());
            if (requested.is_absolute() && std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() && std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) / requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument("webgl_buffergeometry_instancing could not resolve --input-replay.");
        }

        /** Reproduces the bootstrap's repeated fixed-step clock at one capture frame. */
        double makeInstancingVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                virtualTimeMilliseconds += FrameStepMilliseconds;
            }
            return virtualTimeMilliseconds;
        }

        /** Builds and validates one initial, animated, or GUI-reduced scenario contract. */
        BuffergeometryInstancingScenarioState makeInstancingScenarioState(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_buffergeometry_instancing")
            {
                throw std::invalid_argument("Instancing adapter requires case-id webgl_buffergeometry_instancing.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool reducedCount = options.scenarioId == "reduced-count" && options.targetFrame == 61u;
            if (!initial && !animated && !reducedCount)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_instancing requires initial/frame 0, "
                    "animated/frame 60, or reduced-count/frame 61.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument("webgl_buffergeometry_instancing requires the locked 800x500 extent.");
            }
            if (options.randomSeed != CanonicalRandomSeed)
            {
                throw std::invalid_argument("webgl_buffergeometry_instancing requires random seed 0x18500006.");
            }
            if (!reducedCount && !options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Non-GUI instancing scenarios must not consume an input replay.");
            }
            if (reducedCount && options.inputReplayPath.empty())
            {
                throw std::invalid_argument("The reduced-count instancing scenario requires --input-replay.");
            }

            BuffergeometryInstancingScenarioState state;
            state.virtualTimeMilliseconds = makeInstancingVirtualTimeMilliseconds(options.targetFrame);
            state.rotationY = static_cast<float>(state.virtualTimeMilliseconds * 0.0005);
            state.shaderTime = static_cast<float>(state.virtualTimeMilliseconds * 0.005);
            state.sineTime = static_cast<float>(std::sin(double(state.shaderTime) * 0.05));
            if (!reducedCount)
            {
                return state;
            }

            const eastl::vector<uint8_t> replayBytes = readInstancingFileBytes(resolveInstancingReplayPath(options), "webgl_buffergeometry_instancing input replay");
            state.replaySha256 = calculateInstancingSha256(replayBytes);
            if (state.replaySha256 != ReducedCountReplaySha256)
            {
                throw std::invalid_argument("Instancing replay SHA-256 differs from the locked GUI sequence.");
            }
            state.activeInstanceCount = ReducedInstanceCount;
            state.replayEventCount = 2u;
            state.replayLastEventFrame = 0u;
            state.usesInputReplay = true;
            state.requiresEntityReallocation = true;
            return state;
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript unit double. */
        double nextInstancingRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Normalizes one JavaScript Vector4 in double precision before Float32Array storage. */
        BuffergeometryInstancingHostFloat4 makeInstancingOrientation(ThreeCompat::DeterministicRandom &random)
        {
            const double x = nextInstancingRandomUnit(random) * 2.0 - 1.0;
            const double y = nextInstancingRandomUnit(random) * 2.0 - 1.0;
            const double z = nextInstancingRandomUnit(random) * 2.0 - 1.0;
            const double w = nextInstancingRandomUnit(random) * 2.0 - 1.0;
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!(length > 0.0))
            {
                throw std::runtime_error("Canonical instancing stream produced a zero quaternion.");
            }
            return {
                static_cast<float>(x / length),
                static_cast<float>(y / length),
                static_cast<float>(z / length),
                static_cast<float>(w / length),
            };
        }

        /** Builds the exact Float32Array instance attribute stream from Three r185. */
        uint32_t buildInstancingInstances(eastl::vector<BuffergeometryInstancingHostInstanceData> &instances)
        {
            ThreeCompat::DeterministicRandom random(CanonicalRandomSeed);
            for (uint32_t drawIndex = 0u; drawIndex < UpstreamRandomDrawsBeforeInstances; ++drawIndex)
            {
                (void)random.nextUint32();
            }

            instances.clear();
            instances.reserve(CanonicalInstanceCount);
            for (uint32_t instanceIndex = 0u; instanceIndex < CanonicalInstanceCount; ++instanceIndex)
            {
                BuffergeometryInstancingHostInstanceData instance;
                instance.offset = {
                    static_cast<float>(nextInstancingRandomUnit(random) - 0.5),
                    static_cast<float>(nextInstancingRandomUnit(random) - 0.5),
                    static_cast<float>(nextInstancingRandomUnit(random) - 0.5),
                    0.0f,
                };
                instance.color = {
                    static_cast<float>(nextInstancingRandomUnit(random)),
                    static_cast<float>(nextInstancingRandomUnit(random)),
                    static_cast<float>(nextInstancingRandomUnit(random)),
                    static_cast<float>(nextInstancingRandomUnit(random)),
                };
                instance.orientationStart = makeInstancingOrientation(random);
                instance.orientationEnd = makeInstancingOrientation(random);
                instances.push_back(instance);
            }

            if (random.getState() != ExpectedFinalRandomState)
            {
                throw std::runtime_error("Canonical instancing random stream ended in an unexpected state.");
            }
            return random.getState();
        }

        /** Builds Three's fov-50 perspective with the backend clip-space Y/Z conversion. */
        glm::mat4 makeInstancingProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 50.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10.0;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth = (double(width) / double(height)) * projectionHeight;
            const double left = -0.5 * projectionWidth;
            const double projectionDepth = FarDistance - NearDistance;

            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(-2.0 * NearDistance / projectionHeight);
            projection[2u][0u] = static_cast<float>(-(2.0 * left + projectionWidth) / projectionWidth);
            projection[2u][2u] = static_cast<float>(-FarDistance / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds the target-frame positive-Y object rotation and camera-z translation. */
        glm::mat4 makeInstancingModelView(float rotationY)
        {
            const double sineY = std::sin(double(rotationY));
            const double cosineY = std::cos(double(rotationY));
            glm::mat4 model(1.0f);
            model[0u][0u] = static_cast<float>(cosineY);
            model[0u][2u] = static_cast<float>(-sineY);
            model[2u][0u] = static_cast<float>(sineY);
            model[2u][2u] = static_cast<float>(cosineY);

            glm::mat4 view(1.0f);
            view[3u][2u] = -2.0f;
            return view * model;
        }

        /** Appends one typed payload to a RenderSet buffer component allocation. */
        void appendInstancingBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const char *name, const void *value, uint64_t byteCount, uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }
    } // namespace

    void WebglBuffergeometryInstancingRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        scenarioState = makeInstancingScenarioState(options);
        device = inDevice;
        vertices = {
            BuffergeometryInstancingHostVertex{.position = {0.025f, -0.025f, 0.0f, 1.0f}},
            BuffergeometryInstancingHostVertex{.position = {-0.025f, 0.025f, 0.0f, 1.0f}},
            BuffergeometryInstancingHostVertex{.position = {0.0f, 0.0f, 0.025f, 1.0f}},
        };
        indices = {0u, 1u, 2u};
        finalRandomState = buildInstancingInstances(instances);
        objectData = {
            .modelViewProjection = makeInstancingProjection(options.width, options.height) * makeInstancingModelView(scenarioState.rotationY),
            .timeAndSineTime =
                {
                    scenarioState.shaderTime,
                    scenarioState.sineTime,
                    0.0f,
                    0.0f,
                },
            .materialAndFlags = {0u, 0u, 0u, 0u},
        };
        materialData = {
            .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Instancing case could not create its Scene RenderSet encoder.");
        }
        initialEntityIndex = allocateSceneEntity(*encoder, CanonicalInstanceCount);
        activeEntityIndex = initialEntityIndex;
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglBuffergeometryInstancingRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, uint32_t instanceCount) const
    {
        if (instanceCount == 0u || instanceCount > instances.size())
        {
            throw std::out_of_range("Instancing entity allocation has an invalid active instance count.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = instanceCount;
        appendInstancingBufferPayload(allocation, WebglBuffergeometryInstancingSceneRenderSetComponents::vertices, "WebglBuffergeometryInstancingVertices", vertices.data(), vertices.size() * sizeof(BuffergeometryInstancingHostVertex), 1u);
        appendInstancingBufferPayload(allocation, WebglBuffergeometryInstancingSceneRenderSetComponents::indices, "WebglBuffergeometryInstancingIndices", indices.data(), indices.size() * sizeof(uint32_t), 1u);
        appendInstancingBufferPayload(allocation, WebglBuffergeometryInstancingSceneRenderSetComponents::objects, "WebglBuffergeometryInstancingObject", &objectData, sizeof(objectData), 1u);
        appendInstancingBufferPayload(allocation, WebglBuffergeometryInstancingSceneRenderSetComponents::instances, instanceCount == ReducedInstanceCount ? "WebglBuffergeometryInstancingInstances12500" : "WebglBuffergeometryInstancingInstances50000", instances.data(), uint64_t(instanceCount) * sizeof(BuffergeometryInstancingHostInstanceData), instanceCount);
        appendInstancingBufferPayload(allocation, WebglBuffergeometryInstancingSceneRenderSetComponents::materials, "WebglBuffergeometryInstancingMaterial", &materialData, sizeof(materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::reallocateReducedCountEntity(GVM::Core::AbstractRendererImpl &renderer)
    {
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Reduced-count scenario could not create its RenderSet encoder.");
        }
        encoder->removeEntity(activeEntityIndex);
        activeEntityIndex = allocateSceneEntity(*encoder, ReducedInstanceCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        entityReallocated = true;
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)options;
        if (scenarioState.requiresEntityReallocation && frameIndex == 0u && !entityReallocated)
        {
            reallocateReducedCountEntity(renderer);
        }
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeInstancingRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Instancing capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error("Instancing capture could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareInstancingOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_buffergeometry_instancing RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete webgl_buffergeometry_instancing RGBA capture.");
        }
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareInstancingOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_buffergeometry_instancing metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_instancing\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!scenarioState.usesInputReplay)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << scenarioState.replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_buffergeometry_instancing\",\n"
                   << "    \"scenarioId\": \"reduced-count\",\n"
                   << "    \"captureFrame\": 61,\n"
                   << "    \"eventCount\": " << scenarioState.replayEventCount << ",\n"
                   << "    \"lastEventFrame\": " << scenarioState.replayLastEventFrame << ",\n"
                   << "    \"target\": \"#container > canvas\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglBuffergeometryInstancingRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareInstancingOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_buffergeometry_instancing snapshot output path.");
        }
        const BuffergeometryInstancingHostInstanceData &firstInstance = instances[0u];
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_instancing\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"entityCount\": 1,\n"
               << "  \"entities\": [{\"role\": \"instanced-triangle\", "
                  "\"entity\": "
               << activeEntityIndex << ", \"instanceCount\": " << scenarioState.activeInstanceCount << "}],\n"
               << "  \"instanceCount\": " << scenarioState.activeInstanceCount << ",\n"
               << "  \"containsInstancing\": true,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\": \"scene\", "
                  "\"scenePass\": \"main-instanced\", \"entityOrdinal\": 0}],\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 1,\n"
               << "    \"renderSetId\": \"scene-set-0\",\n"
               << "    \"renderSetType\": "
                  "\"WebglBuffergeometryInstancingSceneRenderSet\",\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 1,\n"
               << "    \"entities\": [{\"entityId\": " << activeEntityIndex << ", \"logicalRenderableId\": \"mesh\", \"instanceCount\": " << scenarioState.activeInstanceCount << "}],\n"
               << "    \"componentSchema\": ["
                  "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                  "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                  "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                  "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                  "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": [{\"name\":\"main-instanced\","
                  "\"renderClass\":\"WebglBuffergeometryInstancingMainPass\","
                  "\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,"
                  "\"drawMode\":\"render-set-indexed-indirect\","
                  "\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":false,"
                  "\"usesExplicitDrawCount\":false}]\n"
               << "  }],\n"
               << "  \"vertexCount\": 3,\n"
               << "  \"indexCount\": 3,\n"
               << "  \"triangleCount\": 1,\n"
               << "  \"componentSchema\": [\"vertices\", \"indices\", "
                  "\"objects\", \"instances\", \"materials\"],\n"
               << "  \"componentByteSizes\": {\"vertex\": 16, \"object\": 96, "
                  "\"instance\": 64, \"material\": 16},\n"
               << "  \"randomDrawsBeforeInstances\": " << UpstreamRandomDrawsBeforeInstances << ",\n"
               << "  \"randomDrawCount\": " << UpstreamRandomDrawsBeforeInstances + CanonicalInstanceCount * RandomDrawsPerInstance << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"virtualTimeMilliseconds\": " << scenarioState.virtualTimeMilliseconds << ",\n"
               << "  \"rotationY\": " << scenarioState.rotationY << ",\n"
               << "  \"shaderTime\": " << scenarioState.shaderTime << ",\n"
               << "  \"sineTime\": " << scenarioState.sineTime << ",\n"
               << "  \"firstOffset\": [" << firstInstance.offset.x << ", " << firstInstance.offset.y << ", " << firstInstance.offset.z << "],\n"
               << "  \"initialEntityId\": " << initialEntityIndex << ",\n"
               << "  \"activeEntityId\": " << activeEntityIndex << ",\n"
               << "  \"activeCountMutation\": \"" << (scenarioState.requiresEntityReallocation ? "remove-reallocate-same-render-set" : "none") << "\",\n"
               << "  \"entityReallocated\": " << (entityReallocated ? "true" : "false") << "\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
