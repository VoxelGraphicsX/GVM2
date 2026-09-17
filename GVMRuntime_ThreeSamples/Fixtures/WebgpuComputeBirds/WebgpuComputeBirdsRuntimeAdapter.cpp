#include "WebgpuComputeBirdsRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "WebgpuComputeBirdsSkyMesh.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t BirdCount = 8192u;
        constexpr uint32_t ReferencePreFlockRandomCount = 229u;
        constexpr uint32_t ReferencePostFlockRandomCount = 48u;
        constexpr const char *PointerReplaySha256 =
            "cd019174c5331b1a5d1140b0193071dc77d3663e6e70effb99fbc5c13e0aebb2";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuComputeBirdsHostVertex) == 16u);
        static_assert(sizeof(WebgpuComputeBirdsHostObjectData) == 208u);
        static_assert(sizeof(WebgpuComputeBirdsHostInstanceData) == 64u);
        static_assert(sizeof(WebgpuComputeBirdsHostMaterialData) == 16u);

        /** Returns the exact SHA-256 identity of one bounded replay file. */
        eastl::string calculateComputeBirdsReplaySha256(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open WebGPU compute birds replay.");
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                uint64_t(end) >
                    std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "WebGPU compute birds replay has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(end));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read WebGPU compute birds replay.");
            }
            eastl::array<
                uint8_t,
                CC_SHA256_DIGEST_LENGTH> digest{};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] =
                "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(
                    HexDigits[value >> 4u]);
                result.push_back(
                    HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Validates the three locked webgpu_compute_birds scenarios. */
        void validateComputeBirdsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool pointer =
                options.scenarioId == "pointer-settings" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_compute_birds" ||
                (!initial && !animated && !pointer) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (pointer != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU compute birds requires one locked Manifest scenario.");
            }
        }

        /** Creates parent directories for one requested output artifact. */
        void prepareComputeBirdsOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeComputeBirdsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareComputeBirdsOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute birds artifact.");
            }
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendComputeBirdsPayload(
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

        /** Builds the WebGPU-depth perspective projection for the r185 camera. */
        glm::mat4 makeComputeBirdsProjection()
        {
            constexpr float NearDistance = 1.0f;
            constexpr float FarDistance = 5000.0f;
            constexpr float Pi = 3.14159265358979323846f;
            const float top =
                NearDistance * std::tan(50.0f * Pi / 360.0f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 result(0.0f);
            result[0u][0u] = NearDistance / right;
            result[1u][1u] = -NearDistance / top;
            result[2u][2u] =
                -FarDistance / (FarDistance - NearDistance);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                -(FarDistance * NearDistance) /
                (FarDistance - NearDistance);
            return result;
        }

        /** Builds the exact nine-vertex r185 BirdGeometry after its 0.2 scale. */
        eastl::vector<WebgpuComputeBirdsHostVertex>
        buildComputeBirdVertices()
        {
            constexpr float Values[27u] = {
                0.0f, 0.0f, -4.0f,
                0.0f, -1.6f, 2.0f,
                0.0f, 0.0f, 6.0f,
                0.0f, 0.0f, -3.0f,
                -4.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 3.0f,
                0.0f, 0.0f, 3.0f,
                4.0f, 0.0f, 1.0f,
                0.0f, 0.0f, -3.0f,
            };
            eastl::vector<WebgpuComputeBirdsHostVertex> vertices(9u);
            for (uint32_t vertexIndex = 0u;
                 vertexIndex < vertices.size();
                 ++vertexIndex)
            {
                vertices[vertexIndex].positionAndVertex =
                    glm::vec4(
                        Values[vertexIndex * 3u + 0u],
                        Values[vertexIndex * 3u + 1u],
                        Values[vertexIndex * 3u + 2u],
                        float(vertexIndex));
            }
            return vertices;
        }
    } // namespace

    void WebgpuComputeBirdsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateComputeBirdsScenario(options);
        device = inDevice;
        inputReplaySha256.clear();
        if (options.scenarioId == "pointer-settings")
        {
            inputReplaySha256 =
                calculateComputeBirdsReplaySha256(
                    std::filesystem::path(
                        options.inputReplayPath.c_str()));
            if (inputReplaySha256 != PointerReplaySha256)
            {
                throw std::runtime_error(
                    "WebGPU compute birds replay diverged from its Oracle lock.");
            }
        }

        const std::filesystem::path skyPath =
            std::filesystem::path(GVM_THREE_SAMPLE_SOURCE_ROOT) /
            "Fixtures/WebgpuComputeBirds/Assets/webgpu_compute_birds_sky.bin";
        WebgpuComputeBirdsSkyMesh sky =
            decodeWebgpuComputeBirdsSkyMesh(skyPath);
        skyVertexCount =
            static_cast<uint32_t>(sky.positions.size());
        skyIndexCount =
            static_cast<uint32_t>(sky.indices.size());
        eastl::vector<WebgpuComputeBirdsHostVertex> skyVertices(
            sky.positions.size());
        for (uint32_t index = 0u;
             index < skyVertices.size();
             ++index)
        {
            skyVertices[index].positionAndVertex =
                glm::vec4(sky.positions[index], 0.0f);
        }

        birdInstances.resize(BirdCount);
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u;
             draw < ReferencePreFlockRandomCount;
             ++draw)
        {
            (void)random.nextFloat();
        }
        for (uint32_t birdIndex = 0u;
             birdIndex < BirdCount;
             ++birdIndex)
        {
            birdInstances[birdIndex].initialPosition = glm::vec4(
                random.nextFloat() * 800.0f - 400.0f,
                random.nextFloat() * 800.0f - 400.0f,
                random.nextFloat() * 800.0f - 400.0f,
                1.0f);
            birdInstances[birdIndex].initialVelocity = glm::vec4(
                (random.nextFloat() - 0.5f) * 10.0f,
                (random.nextFloat() - 0.5f) * 10.0f,
                (random.nextFloat() - 0.5f) * 10.0f,
                0.0f);
            birdInstances[birdIndex].initialPhase =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            birdInstances[birdIndex].ordinal =
                glm::vec4(float(birdIndex), 0.0f, 0.0f, 0.0f);
        }
        for (uint32_t draw = 0u;
             draw < ReferencePostFlockRandomCount;
             ++draw)
        {
            (void)random.nextFloat();
        }
        finalRandomState = random.getState();
        if (finalRandomState != 1051561637u)
        {
            throw std::runtime_error(
                "WebGPU compute birds random stream does not match the locked oracle.");
        }

        controls =
            glm::vec4(15.0f, 20.0f, 20.0f, 1.0f / 60.0f);
        rayOrigin =
            glm::vec4(0.0f, 0.0f, 1000.0f, 0.0f);
        constexpr float TangentHalfFov = 0.4663076582f;
        const glm::vec3 direction =
            glm::normalize(glm::vec3(
                0.0f,
                10.0f * TangentHalfFov,
                -1.0f));
        rayDirection =
            glm::vec4(direction, 0.0f);
        inspectorEnabled = 0.0f;

        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 0.0f, 1000.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection =
            makeComputeBirdsProjection();
        glm::mat4 skyModel(1.0f);
        skyModel =
            glm::rotate(
                skyModel,
                0.75f,
                glm::vec3(0.0f, 0.0f, 1.0f));
        skyModel =
            glm::scale(
                skyModel,
                glm::vec3(1200.0f));
        glm::mat4 birdModel(1.0f);
        birdModel =
            glm::rotate(
                birdModel,
                1.5707963267948966f,
                glm::vec3(0.0f, 1.0f, 0.0f));

        const WebgpuComputeBirdsHostObjectData skyObject = {
            .model = skyModel,
            .view = view,
            .projection = projection,
            .phaseAndFog =
                glm::vec4(0.0f, 700.0f, 3000.0f, 0.0f),
        };
        const WebgpuComputeBirdsHostObjectData birdObject = {
            .model = birdModel,
            .view = view,
            .projection = projection,
            .phaseAndFog =
                glm::vec4(1.0f, 700.0f, 3000.0f, 0.0f),
        };
        const WebgpuComputeBirdsHostMaterialData skyMaterial = {
            .colorAndPhase =
                glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
        };
        const WebgpuComputeBirdsHostMaterialData birdMaterial = {
            .colorAndPhase =
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        };
        const WebgpuComputeBirdsHostInstanceData skyInstance = {
            .initialPosition = glm::vec4(0.0f),
            .initialVelocity = glm::vec4(0.0f),
            .initialPhase = glm::vec4(0.0f),
            .ordinal = glm::vec4(0.0f),
        };
        const eastl::vector<WebgpuComputeBirdsHostVertex> birdVertices =
            buildComputeBirdVertices();
        eastl::vector<uint32_t> birdIndices(9u);
        for (uint32_t index = 0u;
             index < birdIndices.size();
             ++index)
        {
            birdIndices[index] = index;
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGPU compute birds RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo skyAllocation;
        skyAllocation.verticesCount = skyVertexCount;
        skyAllocation.indicesCount = skyIndexCount;
        skyAllocation.instanceCount = 1u;
        appendComputeBirdsPayload(
            skyAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::vertices,
            "WebgpuComputeBirdsSkyVertices",
            skyVertices.data(),
            skyVertices.size() * sizeof(skyVertices[0u]),
            1u);
        appendComputeBirdsPayload(
            skyAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::indices,
            "WebgpuComputeBirdsSkyIndices",
            sky.indices.data(),
            sky.indices.size() * sizeof(sky.indices[0u]),
            1u);
        appendComputeBirdsPayload(
            skyAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::objects,
            "WebgpuComputeBirdsSkyObject",
            &skyObject,
            sizeof(skyObject),
            1u);
        appendComputeBirdsPayload(
            skyAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::instances,
            "WebgpuComputeBirdsSkyInstance",
            &skyInstance,
            sizeof(skyInstance),
            1u);
        appendComputeBirdsPayload(
            skyAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::materials,
            "WebgpuComputeBirdsSkyMaterial",
            &skyMaterial,
            sizeof(skyMaterial),
            1u);
        encoder->allocEntity(skyAllocation);

        GVM::Core::RenderSetAllocInfo birdAllocation;
        birdAllocation.verticesCount =
            static_cast<uint32_t>(birdVertices.size());
        birdAllocation.indicesCount =
            static_cast<uint32_t>(birdIndices.size());
        birdAllocation.instanceCount = BirdCount;
        appendComputeBirdsPayload(
            birdAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::vertices,
            "WebgpuComputeBirdsBirdVertices",
            birdVertices.data(),
            birdVertices.size() * sizeof(birdVertices[0u]),
            1u);
        appendComputeBirdsPayload(
            birdAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::indices,
            "WebgpuComputeBirdsBirdIndices",
            birdIndices.data(),
            birdIndices.size() * sizeof(birdIndices[0u]),
            1u);
        appendComputeBirdsPayload(
            birdAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::objects,
            "WebgpuComputeBirdsBirdObject",
            &birdObject,
            sizeof(birdObject),
            1u);
        appendComputeBirdsPayload(
            birdAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::instances,
            "WebgpuComputeBirdsBirdInstances",
            birdInstances.data(),
            birdInstances.size() * sizeof(birdInstances[0u]),
            BirdCount);
        appendComputeBirdsPayload(
            birdAllocation,
            WebgpuComputeBirdsSceneRenderSetComponents::materials,
            "WebgpuComputeBirdsBirdMaterial",
            &birdMaterial,
            sizeof(birdMaterial),
            1u);
        encoder->allocEntity(birdAllocation);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebgpuComputeBirdsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeBirdsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(
                options.captureRgbaPath.c_str());
            prepareComputeBirdsOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute birds RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgpu_compute_birds\",\n"
            << "  \"scenarioId\":\""
            << options.scenarioId.c_str()
            << "\",\n  \"pipeline\":\""
            << options.pipeline.c_str()
            << "\",\n  \"backend\":\""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\":"
            << frameIndex
            << ",\n  \"randomSeed\":"
            << options.randomSeed
            << ",\n  \"randomState\":"
            << finalRandomState
            << ",\n  \"width\":"
            << width
            << ",\n  \"height\":"
            << height
            << ",\n  \"rowStrideBytes\":"
            << uint64_t(width) * 4u
            << ",\n  \"byteCount\":"
            << byteCount
            << ",\n  \"format\":\"rgba8unorm\",\n"
            << "  \"samplePolicy\":{\"mode\":\"single-sample\","
            << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
            << "  \"inputReplay\":";
        if (inputReplaySha256.empty())
        {
            metadata << "null,\n";
        }
        else
        {
            metadata
                << "{\"schemaVersion\":1,"
                << "\"caseId\":\"webgpu_compute_birds\","
                << "\"scenarioId\":\"pointer-settings\","
                << "\"captureFrame\":61,"
                << "\"sha256\":\""
                << inputReplaySha256.c_str()
                << "\",\"target\":\"canvas:not([class])\","
                << "\"eventCount\":1,\"lastEventFrame\":0},\n";
        }
        metadata
            << "  \"renderSetCount\":1,\n"
            << "  \"entityCount\":2,\n"
            << "  \"instanceCounts\":[1,8192],\n"
            << "  \"computeDispatchThreads\":8192\n}\n";
        writeComputeBirdsText(
            options.captureMetadataPath,
            metadata.str());

        std::ostringstream sceneSnapshot;
        sceneSnapshot
            << "{\n  \"caseId\":\"webgpu_compute_birds\",\n"
            << "  \"scenarioId\":\""
            << options.scenarioId.c_str()
            << "\",\n  \"frame\":"
            << frameIndex
            << ",\n  \"implementationLevel\":\"strict-pass\",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"required\",\n"
            << "  \"renderableObjectCount\":2,\n"
            << "  \"sceneRenderSetCount\":1,\n"
            << "  \"drawCommandCount\":2,\n"
            << "  \"skyVertexCount\":"
            << skyVertexCount
            << ",\n  \"skyIndexCount\":"
            << skyIndexCount
            << ",\n  \"sceneRoots\":[{\n"
            << "    \"id\":\"scene\",\n"
            << "    \"renderSetCount\":1,\n"
            << "    \"renderSetId\":\"webgpu-compute-birds-scene-set\",\n"
            << "    \"renderSetType\":\"WebgpuComputeBirdsSceneRenderSet\",\n"
            << "    \"renderableObjectCount\":2,\n"
            << "    \"entityCount\":2,\n"
            << "    \"entities\":["
            << "{\"entityId\":\"sky\",\"logicalRenderableId\":\"sky\","
            << "\"instanceCount\":1},"
            << "{\"entityId\":\"birds\",\"logicalRenderableId\":\"birds\","
            << "\"instanceCount\":8192}],\n"
            << "    \"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
            << "    \"drawCommandCount\":2,\n"
            << "    \"directDrawFallback\":false,\n"
            << "    \"scenePasses\":["
            << "{\"name\":\"sky-backside-fogged\","
            << "\"renderClass\":\"WebgpuComputeBirdsSkyPass\","
            << "\"renderSetId\":\"webgpu-compute-birds-scene-set\","
            << "\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"birds-double-sided-fogged\","
            << "\"renderClass\":\"WebgpuComputeBirdsFlockPass\","
            << "\"renderSetId\":\"webgpu-compute-birds-scene-set\","
            << "\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]\n"
            << "  }]\n}\n";
        writeComputeBirdsText(
            options.sceneSnapshotPath,
            sceneSnapshot.str());
        writeComputeBirdsText(
            options.semanticSnapshotPath,
            "{\n  \"algorithm\":\"r185-o-n-squared-flock-compute\",\n"
            "  \"birdCount\":8192,\n"
            "  \"velocityDispatchCountPerAnimatedFrame\":1,\n"
            "  \"positionDispatchCountPerAnimatedFrame\":1,\n"
            "  \"usesRenderEntityID\":true,\n"
            "  \"usesRenderEntityInstanceID\":true,\n"
            "  \"skyGeometry\":\"IcosahedronGeometry(1,6)\",\n"
            "  \"toneMapping\":\"NeutralToneMapping\",\n"
            "  \"singleSample\":true\n}\n");
        captureWritten = true;
    }

    void WebgpuComputeBirdsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        birdInstances.clear();
    }
} // namespace GVM::ThreeSamples
