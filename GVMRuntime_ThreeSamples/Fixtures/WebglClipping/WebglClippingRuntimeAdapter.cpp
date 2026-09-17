#include "WebglClippingRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t TubularSegments = 95u;
        constexpr uint32_t RadialSegments = 20u;
        constexpr float TorusRadius = 0.4f;
        constexpr float TorusTube = 0.08f;

        static_assert(sizeof(WebglClippingHostVertex) == 32u);
        static_assert(sizeof(WebglClippingHostObjectData) == 256u);
        static_assert(sizeof(WebglClippingHostInstanceData) == 16u);
        static_assert(sizeof(WebglClippingHostMaterialData) == 16u);
        static_assert(sizeof(WebglClippingHostPlaneData) == 48u);
        static_assert(sizeof(WebglClippingHostRenderFlagsData) == 16u);

        /** Creates a parent directory for one optional output artifact. */
        void preparePath(const eastl::string &pathValue)
        {
            if (pathValue.empty())
                return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Validates one of the four frozen clipping scenarios. */
        void validateScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" &&
                options.targetFrame == 0u && options.inputReplayPath.empty();
            const bool animated = options.scenarioId == "animated" &&
                options.targetFrame == 120u && options.inputReplayPath.empty();
            const bool global = options.scenarioId == "global-plane" &&
                options.targetFrame == 121u && !options.inputReplayPath.empty();
            const bool hardLocal = options.scenarioId == "hard-local-clip" &&
                options.targetFrame == 121u && !options.inputReplayPath.empty();
            if (options.caseId != "webgl_clipping" ||
                (!initial && !animated && !global && !hardLocal) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_clipping requires its locked case, scenarios, extent, and seed.");
            }
            if ((global || hardLocal) &&
                !std::filesystem::is_regular_file(
                    std::filesystem::path(options.inputReplayPath.c_str())))
            {
                throw std::invalid_argument(
                    "webgl_clipping replay scenario requires a readable input file.");
            }
        }

        /** Builds the OpenGL perspective matrix used before DSL depth conversion. */
        glm::mat4 makeProjection(uint32_t width, uint32_t height)
        {
            constexpr double nearDistance = 0.25;
            constexpr double farDistance = 16.0;
            constexpr double fieldOfView = 36.0;
            const double top = nearDistance *
                std::tan(fieldOfView * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth = projectionHeight *
                double(width) / double(height);
            glm::mat4 projection(0.0f);
            projection[0][0] = float(2.0 * nearDistance / projectionWidth);
            projection[1][1] = float(2.0 * nearDistance / projectionHeight);
            projection[2][2] = float(-(farDistance + nearDistance) /
                (farDistance - nearDistance));
            projection[2][3] = -1.0f;
            projection[3][2] = float(-2.0 * farDistance * nearDistance /
                (farDistance - nearDistance));
            return projection;
        }

        /** Evaluates the default p=2/q=3 TorusKnot center curve. */
        glm::dvec3 torusCenter(double u)
        {
            const double cu = std::cos(u);
            const double su = std::sin(u);
            const double quOverP = 1.5 * u;
            const double cs = std::cos(quOverP);
            return {
                TorusRadius * (2.0 + cs) * 0.5 * cu,
                TorusRadius * (2.0 + cs) * 0.5 * su,
                TorusRadius * std::sin(quOverP) * 0.5};
        }

        /** Appends one typed component payload to an allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
                          const void *value,
                          uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }
    }

    void WebglClippingRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateScenario(options);
        device = inDevice;
        entities.clear();
        entities.resize(2u);
        entities[0].logicalId = "torus-knot";
        entities[1].logicalId = "ground";
        buildTorusKnot(entities[0]);
        buildGround(entities[1]);
        updateFrame(options, options.targetFrame);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "webgl_clipping could not create its RenderSet encoder.");
        for (WebglClippingEntityData &entity : entities)
            entity.entityIndex = allocateEntity(*encoder, entity);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglClippingRuntimeAdapter::buildTorusKnot(
        WebglClippingEntityData &entity)
    {
        entity.vertices.clear();
        entity.indices.clear();
        entity.vertices.reserve((TubularSegments + 1u) *
            (RadialSegments + 1u));
        entity.indices.reserve(TubularSegments * RadialSegments * 6u);
        for (uint32_t tubular = 0u; tubular <= TubularSegments; ++tubular)
        {
            const double u = double(tubular) / double(TubularSegments) *
                2.0 * Pi * 2.0;
            const glm::dvec3 p1 = torusCenter(u);
            const glm::dvec3 p2 = torusCenter(u + 0.01);
            glm::dvec3 tangent = p2 - p1;
            glm::dvec3 normalSeed = p2 + p1;
            glm::dvec3 binormal = glm::normalize(glm::cross(tangent, normalSeed));
            glm::dvec3 normalBasis = glm::normalize(glm::cross(binormal, tangent));
            for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
            {
                const double v = double(radial) / double(RadialSegments) *
                    2.0 * Pi;
                const double cx = -double(TorusTube) * std::cos(v);
                const double cy = double(TorusTube) * std::sin(v);
                const glm::dvec3 position = p1 +
                    cx * normalBasis + cy * binormal;
                const glm::dvec3 normal = glm::normalize(position - p1);
                entity.vertices.push_back({
                    glm::vec4(glm::vec3(position), 1.0f),
                    glm::vec4(glm::vec3(normal), 0.0f)});
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
                entity.indices.insert(entity.indices.end(), {a, b, d, b, c, d});
            }
        }
        // MeshPhongMaterial colors are authored in sRGB but the DSL lighting
        // path operates in linear space before its explicit output transfer.
        entity.materialData.baseColorAndFlags =
            glm::vec4(0.2158605f, 0.8549926f, 0.0051815f, 100.0f);
        entity.instanceData.reserved = glm::vec4(0.0f);
        entity.renderFlags.phaseAndFlags = glm::vec4(0.0f, 1.0f, 1.0f, 0.0f);
    }

    void WebglClippingRuntimeAdapter::buildGround(
        WebglClippingEntityData &entity)
    {
        entity.vertices = {
            {{-4.5f, -4.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
            {{ 4.5f, -4.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
            {{ 4.5f,  4.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
            {{-4.5f,  4.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
        };
        // The front scene pass uses Back-face culling.  Keep the ground's
        // post-rotation normal facing the camera so the plane is submitted in
        // the same front-facing orientation as the upstream example.
        entity.indices = {0u, 3u, 1u, 1u, 3u, 2u};
        entity.materialData.baseColorAndFlags =
            glm::vec4(0.3515326f, 0.4178851f, 0.4286905f, 150.0f);
        entity.instanceData.reserved = glm::vec4(0.0f);
        entity.renderFlags.phaseAndFlags = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    }

    void WebglClippingRuntimeAdapter::updateFrame(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 1.3f, 3.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = makeProjection(options.width, options.height);
        const glm::mat4 shadowView = glm::lookAt(
            glm::vec3(0.0f, 3.0f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::mat4 shadowProjection = glm::ortho(
            -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 10.0f);
        const float time = float(frameIndex) / 60.0f;
        glm::mat4 torusModel(1.0f);
        torusModel = glm::translate(torusModel, glm::vec3(0.0f, 0.8f, 0.0f));
        torusModel = glm::rotate(torusModel, time * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        torusModel = glm::rotate(torusModel, time * 0.2f, glm::vec3(0.0f, 1.0f, 0.0f));
        const float scale = std::cos(time) * 0.125f + 0.875f;
        torusModel = glm::scale(torusModel, glm::vec3(scale));
        glm::mat4 groundModel(1.0f);
        groundModel = glm::rotate(groundModel, -float(Pi * 0.5),
            glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 models[2] = {torusModel, groundModel};
        const glm::vec3 spotPosition(2.0f, 3.0f, 3.0f);
        const glm::vec3 directionalPosition(0.0f, 3.0f, 0.0f);
        const glm::vec3 spotPositionView = glm::vec3(
            view * glm::vec4(spotPosition, 1.0f));
        // SpotLight.target defaults to the origin.  WebGLLights therefore
        // uploads the direction from the light position toward that target,
        // rather than the position vector itself.
        const glm::vec3 spotDirection = glm::normalize(
            glm::vec3(view * glm::vec4(-spotPosition, 0.0f)));
        const glm::vec3 directionalDirection = glm::normalize(
            glm::vec3(view * glm::vec4(directionalPosition, 0.0f)));
        const glm::vec4 worldGlobalPlane =
            (options.scenarioId == "global-plane")
            ? glm::vec4(-1.0f, 0.0f, 0.0f, -0.2f)
            : glm::vec4(-1.0f, 0.0f, 0.0f, 0.1f);
        const glm::vec4 globalPlaneView = glm::transpose(glm::inverse(view)) *
            worldGlobalPlane;
        const bool globalEnabled = options.scenarioId == "global-plane";
        const bool hardLocal = options.scenarioId == "hard-local-clip";
        const float localConstant = hardLocal ? 1.1f : 0.8f;
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            WebglClippingEntityData &entity = entities[index];
            entity.objectData.modelView = view * models[index];
            entity.objectData.modelViewProjection = projection *
                entity.objectData.modelView;
            entity.objectData.shadowModelViewProjection =
                shadowProjection * shadowView * models[index];
            entity.objectData.spotDirectionAndIntensity = glm::vec4(
                spotPositionView, 60.0f);
            entity.objectData.directionalDirectionAndIntensity = glm::vec4(
                directionalDirection, 3.0f);
            entity.objectData.ambientAndFogNear = glm::vec4(
                0.3000000f,
                0.3000000f,
                0.3000000f,
                std::cos(float(Pi / 5.0)));
            entity.objectData.fogColorAndFar = glm::vec4(
                spotDirection,
                std::cos(float((Pi / 5.0) * (1.0 - 0.2))));
            const glm::vec4 localPlane(0.0f, -1.0f, 0.0f, localConstant);
            // Three.js projects material clipping planes by the camera view
            // matrix.  The plane constant is therefore expressed in world
            // space even though it is attached to the material; applying the
            // model transform here would incorrectly cancel object.position.y.
            entity.planeData.localPlane = glm::transpose(glm::inverse(view)) *
                localPlane;
            entity.planeData.globalPlaneView = globalPlaneView;
            entity.planeData.enableAndReserved = glm::vec4(
                index == 0u && !globalEnabled ? 1.0f : 0.0f,
                index == 0u && globalEnabled ? 1.0f : 0.0f,
                index == 0u && !hardLocal ? 1.0f : 0.0f,
                0.0f);
            if (index == 1u)
                entity.planeData.enableAndReserved = glm::vec4(0.0f);
            entity.renderFlags.phaseAndFlags.x = 0.0f;
        }
    }

    GVM::Core::RenderEntityIndex WebglClippingRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        WebglClippingEntityData &entity)
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        const eastl::string suffix(entity.logicalId);
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::vertices,
            suffix + "-vertices", entity.vertices.data(),
            entity.vertices.size() * sizeof(entity.vertices[0]));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::indices,
            suffix + "-indices", entity.indices.data(),
            entity.indices.size() * sizeof(entity.indices[0]));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::objects,
            suffix + "-objects", &entity.objectData, sizeof(entity.objectData));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::instances,
            suffix + "-instances", &entity.instanceData, sizeof(entity.instanceData));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::materials,
            suffix + "-materials", &entity.materialData, sizeof(entity.materialData));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::clipPlanes,
            suffix + "-clip-planes", &entity.planeData, sizeof(entity.planeData));
        appendBuffer(allocation,
            WebglClippingSceneRenderSetComponents::renderFlags,
            suffix + "-render-flags", &entity.renderFlags, sizeof(entity.renderFlags));
        return encoder.allocEntity(allocation);
    }

    void WebglClippingRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateFrame(options, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "webgl_clipping could not create its frame update encoder.");
        for (const WebglClippingEntityData &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglClippingSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglClippingSceneRenderSetComponents::clipPlanes,
                &entity.planeData, sizeof(entity.planeData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglClippingRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
            return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture,
            rgba.data(), rgba.size())->submit();
        preparePath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        preparePath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
                   << "\"caseId\":\"webgl_clipping\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"pipeline\":\""
                   << options.pipeline.c_str() << "\",\"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\"frame\":"
                   << frameIndex << ",\"randomSeed\":" << options.randomSeed
                   << ",\"width\":" << width << ",\"height\":"
                   << height << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
                   << ",\"byteCount\":" << byteCount
                   << ",\"format\":\"rgba8unorm\",\"sceneRenderSetCount\":1,"
                   << "\"entityCount\":2,\"instanceCounts\":[1,1],"
                   << "\"scenePassCount\":3,\"drawCommandCount\":4,"
                   << "\"sampleCount\":1,\"msaaEnabled\":false,"
                   << "\"directDrawFallback\":false}\n";
        }
        preparePath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_clipping\","
                   << "\"scenarioId\":\"" << options.scenarioId.c_str()
                   << "\",\"frame\":" << frameIndex
                   << ",\"implementationLevel\":\"semantic-complete\","
                   << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
                   << "\"sceneRenderSetCount\":1,\"renderSetType\":\"WebglClippingSceneRenderSet\","
                   << "\"renderableObjectCount\":2,\"entityCount\":2,"
                   << "\"instanceCounts\":[1,1],\"scenePassCount\":3,"
                   << "\"screenPassCount\":0,\"drawCommandCount\":4,"
                   << "\"singleSample\":true,\"msaaEnabled\":false,"
                   << "\"directDrawFallback\":false,\"scenePasses\":["
                   << "{\"name\":\"shadow-depth\",\"renderClass\":\"WebglClippingShadowDepthPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":2,\"drawCommandCount\":2,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"main-phong-front\",\"renderClass\":\"WebglClippingFrontPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"main-phong-back\",\"renderClass\":\"WebglClippingBackPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],"
                   << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"shadow-depth\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"shadow-depth\",\"entityOrdinal\":1},{\"sceneRoot\":\"scene\",\"scenePass\":\"main-phong-front\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"main-phong-back\",\"entityOrdinal\":0}],"
                   << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebglClippingSceneRenderSet\",\"renderableObjectCount\":2,\"entityCount\":2,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"torus-knot\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"ground\",\"instanceCount\":1}],"
                   << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"clipPlanes\",\"kind\":\"buffer\",\"role\":\"local-global-plane-equations-and-enable-flags\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"material-shadow-sidedness-and-clip-phase\"}],\"scenePasses\":[{\"name\":\"shadow-depth\",\"renderClass\":\"WebglClippingShadowDepthPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":2,\"drawCommandCount\":2,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"main-phong-front\",\"renderClass\":\"WebglClippingFrontPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"main-phong-back\",\"renderClass\":\"WebglClippingBackPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"drawCommandCount\":4,\"directDrawFallback\":false}]}\n";
        }
        captureWritten = true;
    }

    void WebglClippingRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }

    void WebglClippingRuntimeAdapter::prepareOutputPath(
        const eastl::string &path) const
    {
        preparePath(path);
    }
}
