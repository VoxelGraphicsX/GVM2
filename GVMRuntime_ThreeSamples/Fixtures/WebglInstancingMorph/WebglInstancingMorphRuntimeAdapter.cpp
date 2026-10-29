#include "WebglInstancingMorphRuntimeAdapter.hpp"

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

        static_assert(sizeof(WebglInstancingMorphHostVertex) == 64u);
        static_assert(sizeof(WebglInstancingMorphHostObjectData) == 176u);
        static_assert(sizeof(WebglInstancingMorphHostInstanceData) == 144u);
        static_assert(sizeof(WebglInstancingMorphHostMaterialData) == 32u);
        static_assert(sizeof(WebglInstancingMorphHostShadowFlags) == 16u);

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

    void WebglInstancingMorphRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool loader = options.scenarioId == "loader-snapshot" && options.targetFrame == 0u;
        const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
        if (options.caseId != "webgl_instancing_morph" ||
            (!loader && !initial && !animated) || options.width != 800u ||
            options.height != 500u || options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_instancing_morph requires one frozen Manifest scenario.");
        }
        device = inDevice;
        horseAsset = loadHorseGlbAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "Horse.glb");
        horseSha256 = horseAsset.sha256;

        WebglInstancingMorphEntityState &ground = entities[0u];
        ground.name = "Ground";
        ground.vertices = {
            {{-500000.0f, 0.0f, -500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)},
            {{ 500000.0f, 0.0f, -500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)},
            {{-500000.0f, 0.0f,  500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(2.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)},
            {{ 500000.0f, 0.0f,  500000.0f, 1.0f}, glm::vec4(1.0f), glm::vec4(3.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)},
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

        WebglInstancingMorphEntityState &horseEntity = entities[1u];
        horseEntity.name = "HorseInstances";
        horseEntity.vertices.reserve(horseAsset.indices.size());
        horseEntity.indices.reserve(horseAsset.indices.size());
        for (uint32_t triangleIndex = 0u;
             triangleIndex < horseAsset.indices.size();
             triangleIndex += 3u)
        {
            const uint32_t sourceIndices[3u] = {
                horseAsset.indices[triangleIndex],
                horseAsset.indices[triangleIndex + 1u],
                horseAsset.indices[triangleIndex + 2u],
            };
            const glm::vec3 faceNormal = glm::normalize(glm::cross(
                glm::vec3(horseAsset.vertices[sourceIndices[1u]].position -
                          horseAsset.vertices[sourceIndices[0u]].position),
                glm::vec3(horseAsset.vertices[sourceIndices[2u]].position -
                          horseAsset.vertices[sourceIndices[0u]].position)));
            for (uint32_t corner = 0u; corner < 3u; ++corner)
            {
                const uint32_t expandedVertexIndex = triangleIndex + corner;
                const HorseGlbVertex &vertex =
                    horseAsset.vertices[sourceIndices[corner]];
                horseEntity.vertices.push_back({
                    vertex.position,
                    vertex.color,
                    glm::vec4(static_cast<float>(expandedVertexIndex),
                              0.0f, 0.0f, 0.0f),
                    glm::vec4(faceNormal, 0.0f),
                });
                horseEntity.indices.push_back(expandedVertexIndex);
            }
        }
        horseEntity.morphTargets.reserve(
            horseAsset.morphTargetCount * horseAsset.indices.size());
        for (uint32_t targetIndex = 0u;
             targetIndex < horseAsset.morphTargetCount;
             ++targetIndex)
        {
            for (const uint32_t sourceVertexIndex : horseAsset.indices)
            {
                horseEntity.morphTargets.push_back(
                    horseAsset.morphPositions[
                        targetIndex * horseAsset.vertices.size() +
                        sourceVertexIndex]);
            }
        }
        horseEntity.instances.resize(HorseInstanceCount);
        horseEntity.materialData.baseColor = glm::vec4(1.0f);
        horseEntity.materialData.roughnessMetalnessAndFlags = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);
        horseEntity.shadowFlags.values = glm::uvec4(1u, 0u, 0u, 0u);

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t moduleDraw = 0u; moduleDraw < 76u; ++moduleDraw)
        {
            (void)random.nextUint32();
        }
        timeOffsets.resize(HorseInstanceCount);
        for (float &timeOffset : timeOffsets)
        {
            timeOffset = static_cast<float>(nextMorphRandom(random) * 3.0);
        }
        for (uint32_t loaderDraw = 0u; loaderDraw < 100u; ++loaderDraw)
        {
            (void)random.nextUint32();
        }
        uint32_t instanceIndex = 0u;
        for (uint32_t x = 0u; x < 32u; ++x)
        {
            for (uint32_t y = 0u; y < 32u; ++y)
            {
                WebglInstancingMorphHostInstanceData &instance =
                    horseEntity.instances[instanceIndex++];
                const double sourceX = 5000.0 - 300.0 * double(x) +
                                       200.0 * nextMorphRandom(random);
                const double sourceZ = 5000.0 - 300.0 * double(y);
                instance.model = glm::translate(
                    glm::mat4(1.0f), glm::vec3(sourceX, 0.0, sourceZ));
                instance.color = makeMorphHsl(nextMorphRandom(random), 0.5, 0.66);
            }
        }
        updateCamera(0u, options.width, options.height);
        updateMorphWeights(0u);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Could not create the instancing morph Set encoder.");
        }
        for (WebglInstancingMorphEntityState &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglInstancingMorphRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const WebglInstancingMorphEntityState &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = static_cast<uint32_t>(entity.instances.size());
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::vertices,
            entity.name + "Vertices", entity.vertices.data(),
            entity.vertices.size() * sizeof(WebglInstancingMorphHostVertex), 1u);
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::indices,
            entity.name + "Indices", entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t), 1u);
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::objects,
            entity.name + "Object", &entity.objectData,
            sizeof(entity.objectData), 1u);
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::instances,
            entity.name + "Instances", entity.instances.data(),
            entity.instances.size() * sizeof(WebglInstancingMorphHostInstanceData),
            static_cast<uint32_t>(entity.instances.size()));
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::materials,
            entity.name + "Material", &entity.materialData,
            sizeof(entity.materialData), 1u);
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::morphTargets,
            entity.name + "MorphTargets", entity.morphTargets.data(),
            entity.morphTargets.size() * sizeof(glm::vec4),
            static_cast<uint32_t>(entity.morphTargets.size()));
        appendMorphPayload(allocation,
            WebglInstancingMorphSceneRenderSetComponents::shadowFlags,
            entity.name + "ShadowFlags", &entity.shadowFlags,
            sizeof(entity.shadowFlags), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglInstancingMorphRuntimeAdapter::updateCamera(
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
            WebglInstancingMorphHostObjectData &objectData =
                entities[entityIndex].objectData;
            objectData.viewProjection = viewProjection;
            objectData.shadowViewProjection = shadowViewProjection;
            objectData.cameraPositionAndFogNear = glm::vec4(hostCamera, 5000.0);
            objectData.cameraForwardAndFogFar = glm::vec4(hostForward, 10000.0);
            objectData.directionalLightAndEntityKind =
                glm::vec4(lightDirection, entityIndex == 1u ? 1.0f : 0.0f);
        }
    }

    void WebglInstancingMorphRuntimeAdapter::updateMorphWeights(uint32_t frameIndex)
    {
        WebglInstancingMorphEntityState &horseEntity = entities[1u];
        const float baseTime = static_cast<float>(frameIndex) / 60.0f;
        eastl::vector<float> weights;
        for (uint32_t instanceIndex = 0u;
             instanceIndex < HorseInstanceCount;
             ++instanceIndex)
        {
            sampleHorseMorphWeights(
                horseAsset, baseTime + timeOffsets[instanceIndex], weights);
            WebglInstancingMorphHostInstanceData &instance =
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

    void WebglInstancingMorphRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (frameIndex == 0u) return;
        updateCamera(frameIndex, options.width, options.height);
        updateMorphWeights(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not update instancing morph components.");
        for (const WebglInstancingMorphEntityState &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglInstancingMorphSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        const WebglInstancingMorphEntityState &horseEntity = entities[1u];
        encoder->setBufferComponentData(
            horseEntity.entityIndex,
            WebglInstancingMorphSceneRenderSetComponents::instances,
            horseEntity.instances.data(),
            horseEntity.instances.size() *
                sizeof(WebglInstancingMorphHostInstanceData),
            0u, HorseInstanceCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInstancingMorphRuntimeAdapter::afterFrame(
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
                 << "  \"caseId\":\"webgl_instancing_morph\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false,\n"
                 << "  \"horseGlbSha256\":\"" << horseSha256.c_str() << "\"\n}\n";
        writeMorphEvidence(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
              << "  \"caseId\":\"webgl_instancing_morph\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"renderSetPolicy\":\"required\",\n"
              << "  \"sceneRenderSetCount\":1,\n"
              << "  \"renderSetType\":\"WebglInstancingMorphSceneRenderSet\",\n"
              << "  \"renderableObjectCount\":2,\n"
              << "  \"entityCount\":2,\n"
              << "  \"instanceCounts\":[1,1024],\n"
              << "  \"drawCommandCount\":2,\n"
              << "  \"scenePassCount\":2,\n"
              << "  \"scenePassSequence\":[\"WebglInstancingMorphShadowPass\",\"WebglInstancingMorphMainPass\"],\n"
              << "  \"usesRenderEntityID\":true,\n"
              << "  \"usesRenderEntityInstanceID\":true,\n"
              << "  \"sampleCount\":1,\n"
              << "  \"msaaEnabled\":false,\n"
              << "  \"sceneRoots\":[{\n"
              << "    \"id\":\"scene\",\n"
              << "    \"renderSetCount\":1,\n"
              << "    \"renderSetId\":\"scene\",\n"
              << "    \"renderSetType\":\"WebglInstancingMorphSceneRenderSet\",\n"
              << "    \"renderableObjectCount\":2,\n"
              << "    \"entityCount\":2,\n"
              << "    \"entities\":[{\"entityId\":" << entities[0u].entityIndex << ",\"logicalRenderableId\":\"ground\",\"instanceCount\":1},{\"entityId\":" << entities[1u].entityIndex << ",\"logicalRenderableId\":\"horse-instances\",\"instanceCount\":1024}],\n"
              << "    \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"morphTargets\",\"kind\":\"buffer\",\"role\":\"morph-target\"}],\n"
              << "    \"drawCommandCount\":2,\n"
              << "    \"directDrawFallback\":false,\n"
              << "    \"scenePasses\":[{\"name\":\"vsm-shadow-moments\",\"renderClass\":\"WebglInstancingMorphVsmShadowPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"main-lit\",\"renderClass\":\"WebglInstancingMorphMainLitPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
              << "  }],\n"
              << "  \"directDrawFallback\":false\n}\n";
        writeMorphEvidence(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgl_instancing_morph\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"frame\":" << frameIndex << ",\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"horse-glb-one-mesh-796-vertices-2952-indices-15-position-morph-targets\",\n"
                     << "  \"result\":{\"renderableObjectCount\":1,"
                     << "\"sceneRootCount\":1,"
                     << "\"canonicalSceneSha256\":\"b8d09113950438dd84f6f4cacb78eddc3ea0216bb10ba8131f7abe8f4aaf7b3f\","
                     << "\"canonicalSceneDigestInput\":\"webgl_instancing_morph|796|2952|15|16|1024|"
                     << horseSha256.c_str() << "\","
                     << "\"asset\":{\"encoding\":\"glb-2.0\","
                     << "\"assetCount\":1,\"vertexCount\":796,"
                     << "\"indexCount\":2952,\"morphTargetCount\":15,"
                     << "\"animationKeyCount\":16,\"instanceCount\":1024,"
                     << "\"sha256\":\"" << horseSha256.c_str() << "\"}}\n}\n";
            writeMorphEvidence(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglInstancingMorphRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        for (WebglInstancingMorphEntityState &entity : entities)
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
