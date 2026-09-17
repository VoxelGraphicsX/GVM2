#include "WebgpuInstancingMorphRuntimeAdapter.hpp"

#include "HorseGlbAsset.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t HorseInstanceCount = 1024u;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebgpuInstancingMorphHostVertex) == 48u);
        static_assert(sizeof(WebgpuInstancingMorphHostObjectData) == 176u);
        static_assert(sizeof(WebgpuInstancingMorphHostInstanceData) == 144u);
        static_assert(sizeof(WebgpuInstancingMorphHostMaterialData) == 32u);
        static_assert(sizeof(WebgpuInstancingMorphHostShadowFlags) == 16u);

        /** Returns one deterministic JavaScript-compatible unit random value. */
        double nextMorphRandom(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one sRGB unit channel into linear working space. */
        float instancingMorphSrgbToLinear(double value)
        {
            return static_cast<float>(value <= 0.04045
                ? value / 12.92
                : std::pow((value + 0.055) / 1.055, 2.4));
        }

        /** Converts one HSL tuple to the linear working RGB used by Three Color. */
        glm::vec4 makeMorphHsl(double hue, double saturation, double lightness)
        {
            const double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
            const double sector = hue * 6.0;
            const double secondary = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            const uint32_t index = static_cast<uint32_t>(std::floor(sector)) % 6u;
            if (index == 0u) { red = chroma; green = secondary; }
            else if (index == 1u) { red = secondary; green = chroma; }
            else if (index == 2u) { green = chroma; blue = secondary; }
            else if (index == 3u) { green = secondary; blue = chroma; }
            else if (index == 4u) { red = secondary; blue = chroma; }
            else { red = chroma; blue = secondary; }
            const double match = lightness - chroma * 0.5;
            return glm::vec4(
                instancingMorphSrgbToLinear(red + match),
                instancingMorphSrgbToLinear(green + match),
                instancingMorphSrgbToLinear(blue + match),
                1.0f);
        }

        /** Builds the current zero-to-one perspective projection. */
        glm::mat4 makeMorphPerspective(uint32_t width, uint32_t height)
        {
            constexpr double NearDistance = 100.0;
            constexpr double FarDistance = 10000.0;
            const double aspect = double(width) / double(height);
            const double top = NearDistance * std::tan(60.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth = projectionHeight * aspect;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            result[1u][1u] = static_cast<float>(-2.0 * NearDistance / projectionHeight);
            result[2u][2u] = static_cast<float>(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Builds the fixed directional shadow camera with zero-to-one depth. */
        glm::mat4 makeMorphShadowViewProjection()
        {
            constexpr double NearDistance = 0.5;
            constexpr double FarDistance = 2000.0;
            constexpr double Extent = 5000.0;
            const glm::dvec3 lightPosition(200.0, 1000.0, 50.0);
            const glm::dmat4 view = glm::lookAt(
                lightPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
            glm::dmat4 projection(1.0);
            projection[0u][0u] = 1.0 / Extent;
            projection[1u][1u] = 1.0 / Extent;
            projection[2u][2u] = -1.0 / (FarDistance - NearDistance);
            projection[3u][2u] = -NearDistance / (FarDistance - NearDistance);
            return glm::mat4(projection * view);
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendMorphPayload(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Creates parent directories for one requested evidence artifact. */
        void prepareMorphEvidencePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeMorphEvidence(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMorphEvidencePath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write instancing morph evidence.");
        }
    } // namespace

    void WebgpuInstancingMorphRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool loader = options.scenarioId == "loader-snapshot" && options.targetFrame == 0u;
        const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
        if (options.caseId != "webgpu_instancing_morph" ||
            (!loader && !initial && !animated) || options.width != 800u ||
            options.height != 500u || options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgpu_instancing_morph requires one frozen Manifest scenario.");
        }
        device = inDevice;
        horseAsset = loadHorseGlbAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "Horse.glb");
        horseSha256 = horseAsset.sha256;

        WebgpuInstancingMorphEntityState &ground = entities[0u];
        ground.name = "Ground";
        ground.vertices = {
            {{-500000.0f, 0.0f, -500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(0.0f)},
            {{ 500000.0f, 0.0f, -500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)},
            {{-500000.0f, 0.0f,  500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)},
            {{ 500000.0f, 0.0f,  500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(3.0f, 0.0f, 0.0f, 0.0f)},
        };
        ground.indices = {0u, 2u, 1u, 2u, 3u, 1u};
        ground.instances.resize(1u);
        ground.instances[0u].model = glm::mat4(1.0f);
        ground.instances[0u].color = glm::vec4(1.0f);
        ground.morphTargets = {glm::vec4(0.0f)};
        ground.materialData.baseColor = glm::vec4(
            instancingMorphSrgbToLinear(0x66u / 255.0),
            instancingMorphSrgbToLinear(0x99u / 255.0),
            instancingMorphSrgbToLinear(0x33u / 255.0), 1.0f);
        ground.materialData.roughnessMetalnessAndFlags = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        ground.shadowFlags.values = glm::uvec4(0u, 1u, 0u, 0u);

        WebgpuInstancingMorphEntityState &horseEntity = entities[1u];
        horseEntity.name = "HorseInstances";
        horseEntity.vertices.reserve(horseAsset.vertices.size());
        for (uint32_t vertexIndex = 0u;
             vertexIndex < horseAsset.vertices.size();
             ++vertexIndex)
        {
            const HorseGlbVertex &vertex = horseAsset.vertices[vertexIndex];
            horseEntity.vertices.push_back({
                vertex.position,
                glm::vec4(1.0f),
                glm::vec4(static_cast<float>(vertexIndex), 0.0f, 0.0f, 0.0f),
            });
        }
        horseEntity.indices = horseAsset.indices;
        horseEntity.morphTargets = horseAsset.morphPositions;
        horseEntity.instances.resize(HorseInstanceCount);
        horseEntity.materialData.baseColor = glm::vec4(1.0f);
        horseEntity.materialData.roughnessMetalnessAndFlags = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);
        horseEntity.shadowFlags.values = glm::uvec4(1u, 0u, 0u, 0u);

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t upstreamDraw = 0u; upstreamDraw < 176u; ++upstreamDraw)
        {
            (void)random.nextUint32();
        }
        timeOffsets.resize(HorseInstanceCount);
        for (float &timeOffset : timeOffsets)
        {
            timeOffset = static_cast<float>(nextMorphRandom(random) * 3.0);
        }
        for (uint32_t loaderDraw = 0u; loaderDraw < 101u; ++loaderDraw)
        {
            (void)random.nextUint32();
        }
        uint32_t instanceIndex = 0u;
        for (uint32_t x = 0u; x < 32u; ++x)
        {
            for (uint32_t y = 0u; y < 32u; ++y)
            {
                WebgpuInstancingMorphHostInstanceData &instance =
                    horseEntity.instances[instanceIndex++];
                const double sourceX = 5000.0 - 300.0 * double(x) +
                                       200.0 * nextMorphRandom(random);
                const double sourceZ = 5000.0 - 300.0 * double(y);
                instance.model = glm::translate(
                    glm::mat4(1.0f), glm::vec3(sourceX, 0.0, sourceZ));
                instance.color = makeMorphHsl(nextMorphRandom(random), 0.5, 0.66);
            }
        }
        updateCamera(options.targetFrame, options.width, options.height);
        updateMorphWeights(options.targetFrame);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Could not create the instancing morph Set encoder.");
        }
        for (WebgpuInstancingMorphEntityState &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebgpuInstancingMorphRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const WebgpuInstancingMorphEntityState &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = static_cast<uint32_t>(entity.instances.size());
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::vertices,
            entity.name + "Vertices", entity.vertices.data(),
            entity.vertices.size() * sizeof(WebgpuInstancingMorphHostVertex), 1u);
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::indices,
            entity.name + "Indices", entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t), 1u);
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::objects,
            entity.name + "Object", &entity.objectData,
            sizeof(entity.objectData), 1u);
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::instances,
            entity.name + "Instances", entity.instances.data(),
            entity.instances.size() * sizeof(WebgpuInstancingMorphHostInstanceData),
            static_cast<uint32_t>(entity.instances.size()));
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::materials,
            entity.name + "Material", &entity.materialData,
            sizeof(entity.materialData), 1u);
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::morphTargets,
            entity.name + "MorphTargets", entity.morphTargets.data(),
            entity.morphTargets.size() * sizeof(glm::vec4),
            static_cast<uint32_t>(entity.morphTargets.size()));
        appendMorphPayload(allocation,
            WebgpuInstancingMorphSceneRenderSetComponents::shadowFlags,
            entity.name + "ShadowFlags", &entity.shadowFlags,
            sizeof(entity.shadowFlags), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebgpuInstancingMorphRuntimeAdapter::updateCamera(
        uint32_t frameIndex, uint32_t width, uint32_t height)
    {
        const double time = double(frameIndex) / 60.0;
        const glm::dvec3 sourceCamera(
            std::sin(time / 10.0) * 3000.0,
            1500.0 + 1000.0 * std::cos(time / 5.0),
            std::cos(time / 10.0) * 3000.0);
        const glm::dvec3 hostCamera = sourceCamera;
        const glm::dvec3 hostForward = glm::normalize(-hostCamera);
        const glm::mat4 view = glm::mat4(glm::lookAt(
            hostCamera, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 viewProjection = makeMorphPerspective(width, height) * view;
        const glm::mat4 shadowViewProjection = makeMorphShadowViewProjection();
        const glm::vec3 lightDirection = glm::normalize(glm::vec3(200.0f, 1000.0f, 50.0f));
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            WebgpuInstancingMorphHostObjectData &objectData =
                entities[entityIndex].objectData;
            objectData.viewProjection = viewProjection;
            objectData.shadowViewProjection = shadowViewProjection;
            objectData.cameraPositionAndFogNear = glm::vec4(hostCamera, 5000.0);
            objectData.cameraForwardAndFogFar = glm::vec4(hostForward, 10000.0);
            objectData.directionalLightAndEntityKind =
                glm::vec4(lightDirection, entityIndex == 1u ? 1.0f : 0.0f);
        }
    }

    void WebgpuInstancingMorphRuntimeAdapter::updateMorphWeights(uint32_t frameIndex)
    {
        WebgpuInstancingMorphEntityState &horseEntity = entities[1u];
        const float baseTime = static_cast<float>(frameIndex) / 60.0f;
        eastl::vector<float> weights;
        for (uint32_t instanceIndex = 0u;
             instanceIndex < HorseInstanceCount;
             ++instanceIndex)
        {
            sampleHorseMorphWeights(
                horseAsset, baseTime + timeOffsets[instanceIndex], weights);
            WebgpuInstancingMorphHostInstanceData &instance =
                horseEntity.instances[instanceIndex];
            instance.morphWeights0 = glm::vec4(
                weights[0u], weights[1u], weights[2u], weights[3u]);
            instance.morphWeights1 = glm::vec4(
                weights[4u], weights[5u], weights[6u], weights[7u]);
            instance.morphWeights2 = glm::vec4(
                weights[8u], weights[9u], weights[10u], weights[11u]);
            instance.morphWeights3 = glm::vec4(
                weights[12u], weights[13u], weights[14u], 0.0f);
        }
    }

    void WebgpuInstancingMorphRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateCamera(frameIndex, options.width, options.height);
        updateMorphWeights(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not update instancing morph components.");
        for (const WebgpuInstancingMorphEntityState &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebgpuInstancingMorphSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        const WebgpuInstancingMorphEntityState &horseEntity = entities[1u];
        encoder->setBufferComponentData(
            horseEntity.entityIndex,
            WebgpuInstancingMorphSceneRenderSetComponents::instances,
            horseEntity.instances.data(),
            horseEntity.instances.size() *
                sizeof(WebgpuInstancingMorphHostInstanceData),
            0u, HorseInstanceCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuInstancingMorphRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("Instancing morph capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareMorphEvidencePath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write instancing morph RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_instancing_morph\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false,\n"
                 << "  \"horseGlbSha256\":\"" << horseSha256.c_str() << "\"\n}\n";
        writeMorphEvidence(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
              << "  \"caseId\":\"webgpu_instancing_morph\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"renderSetPolicy\":\"required\",\n"
              << "  \"sceneRenderSetCount\":1,\n"
              << "  \"renderSetType\":\"WebgpuInstancingMorphSceneRenderSet\",\n"
              << "  \"entityCount\":2,\n"
              << "  \"instanceCounts\":[1,1024],\n"
              << "  \"drawCommandCount\":2,\n"
              << "  \"scenePassCount\":2,\n"
              << "  \"scenePassSequence\":[\"WebgpuInstancingMorphShadowPass\",\"WebgpuInstancingMorphMainPass\"],\n"
              << "  \"usesRenderEntityID\":true,\n"
              << "  \"usesRenderEntityInstanceID\":true,\n"
              << "  \"sampleCount\":1,\n"
              << "  \"msaaEnabled\":false\n}\n";
        writeMorphEvidence(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgpu_instancing_morph\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"result\":{\"assetCount\":1,\"vertexCount\":796,"
                     << "\"indexCount\":2952,\"morphTargetCount\":15,"
                     << "\"animationKeyCount\":16,\"instanceCount\":1024,"
                     << "\"horseGlbSha256\":\"" << horseSha256.c_str() << "\"}\n}\n";
            writeMorphEvidence(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebgpuInstancingMorphRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        for (WebgpuInstancingMorphEntityState &entity : entities)
        {
            entity.vertices.clear();
            entity.indices.clear();
            entity.instances.clear();
            entity.morphTargets.clear();
        }
        timeOffsets.clear();
        horseAsset = HorseGlbAsset{};
    }
} // namespace GVM::ThreeSamples
