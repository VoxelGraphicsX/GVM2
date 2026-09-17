#include "MiscControlsTrackballRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TrackballInstanceCount = 500u;
        constexpr uint32_t TrackballRandomDrawsBeforeInstances = 104u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(MiscControlsTrackballHostVertex) == 16u);
        static_assert(sizeof(MiscControlsTrackballHostObjectData) == 176u);
        static_assert(sizeof(MiscControlsTrackballHostInstanceData) == 16u);
        static_assert(sizeof(MiscControlsTrackballHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareTrackballOutput(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Advances the deterministic xorshift32 stream installed by the reference harness. */
        double nextTrackballRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<double>(value >> 8u) / 16777216.0;
        }

        /** Builds the exact indexed stream of ConeGeometry(10, 30, 4, 1). */
        void buildTrackballCone(
            eastl::vector<MiscControlsTrackballHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(20u);
            indices.reserve(24u);
            for (uint32_t segment = 0u; segment <= 4u; ++segment)
                vertices.push_back({glm::vec4(0.0f, 15.0f, 0.0f, 1.0f)});
            for (uint32_t segment = 0u; segment <= 4u; ++segment)
            {
                const double angle = double(segment) * Pi * 0.5;
                vertices.push_back({glm::vec4(
                    float(std::sin(angle) * 10.0),
                    -15.0f,
                    float(std::cos(angle) * 10.0),
                    1.0f)});
            }
            for (uint32_t segment = 0u; segment < 4u; ++segment)
                vertices.push_back({glm::vec4(0.0f, -15.0f, 0.0f, 1.0f)});
            for (uint32_t segment = 0u; segment <= 4u; ++segment)
            {
                const double angle = double(segment) * Pi * 0.5;
                vertices.push_back({glm::vec4(
                    float(std::sin(angle) * 10.0),
                    -15.0f,
                    float(std::cos(angle) * 10.0),
                    1.0f)});
            }
            indices = {
                5u, 6u, 1u, 6u, 7u, 2u,
                7u, 8u, 3u, 8u, 9u, 4u,
                15u, 14u, 10u, 16u, 15u, 11u,
                17u, 16u, 12u, 18u, 17u, 13u,
            };
        }

        /** Builds the 500 seeded InstancedMesh translation records. */
        void buildTrackballInstances(
            uint32_t randomSeed,
            eastl::vector<MiscControlsTrackballHostInstanceData> &instances)
        {
            uint32_t randomState = randomSeed;
            instances.clear();
            instances.reserve(TrackballInstanceCount);
            for (uint32_t draw = 0u;
                 draw < TrackballRandomDrawsBeforeInstances;
                 ++draw)
            {
                nextTrackballRandom(randomState);
            }
            for (uint32_t instance = 0u;
                 instance < TrackballInstanceCount;
                 ++instance)
            {
                const double x = (nextTrackballRandom(randomState) - 0.5) * 1000.0;
                const double y = (nextTrackballRandom(randomState) - 0.5) * 1000.0;
                const double z = (nextTrackballRandom(randomState) - 0.5) * 1000.0;
                instances.push_back({glm::vec4(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    0.0f)});
            }
        }

        /** Creates the generated-backend zero-to-one perspective projection. */
        glm::dmat4 makeTrackballProjection(uint32_t width, uint32_t height)
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top = NearDistance * std::tan(60.0 * Pi / 360.0);
            const double right = top * double(width) / double(height);
            glm::dmat4 projection(0.0);
            projection[0u][0u] = NearDistance / right;
            projection[1u][1u] = -NearDistance / top;
            projection[2u][2u] = -FarDistance / (FarDistance - NearDistance);
            projection[2u][3u] = -1.0;
            projection[3u][2u] =
                -FarDistance * NearDistance / (FarDistance - NearDistance);
            return projection;
        }

        /** Creates Three's orthographic projection with the selected camera zoom. */
        glm::dmat4 makeTrackballOrthographicProjection(
            uint32_t width,
            uint32_t height,
            double zoom)
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double halfHeight = 200.0 / zoom;
            const double halfWidth = halfHeight * double(width) / double(height);
            glm::dmat4 projection(1.0);
            projection[0u][0u] = 1.0 / halfWidth;
            projection[1u][1u] = -1.0 / halfHeight;
            projection[2u][2u] = -1.0 / (FarDistance - NearDistance);
            projection[3u][2u] = -NearDistance / (FarDistance - NearDistance);
            return projection;
        }

        /** Returns the locked camera position after the selected control replay. */
        glm::dvec3 selectTrackballCameraPosition(const eastl::string &scenarioId)
        {
            if (scenarioId == "perspective-rotate-zoom-pan")
                return glm::dvec3(
                    -270.54712471792305,
                    -95.43029306346139,
                    17.337106066424425);
            if (scenarioId == "orthographic-key-modes")
                return glm::dvec3(
                    -369.9902850936338,
                    -255.99827026156473,
                    151.61209486998902);
            return glm::dvec3(0.0, 0.0, 500.0);
        }

        /** Returns the locked TrackballControls target after the selected replay. */
        glm::dvec3 selectTrackballTarget(const eastl::string &scenarioId)
        {
            if (scenarioId == "perspective-rotate-zoom-pan")
                return glm::dvec3(
                    1.7457235276280163,
                    60.16562021971037,
                    104.53777237853225);
            if (scenarioId == "orthographic-key-modes")
                return glm::dvec3(
                    32.83265976615728,
                    25.977791140288936,
                    60.93036633119343);
            return glm::dvec3(0.0);
        }

        /** Returns the active camera's locked TrackballControls up vector. */
        glm::dvec3 selectTrackballUp(const eastl::string &scenarioId)
        {
            if (scenarioId == "perspective-rotate-zoom-pan")
                return glm::dvec3(
                    -0.5461673552393683,
                    0.6879043684346464,
                    0.47800502085162594);
            if (scenarioId == "orthographic-key-modes")
                return glm::dvec3(
                    -0.3845943490239505,
                    0.7307839556832346,
                    0.5639521228037077);
            return glm::dvec3(0.0, 1.0, 0.0);
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendTrackballPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *data,
            size_t byteCount,
            uint32_t elementCount = 1u)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = elementCount,
            });
        }

        /** Writes one optional text artifact without changing runtime configuration. */
        void writeTrackballText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareTrackballOutput(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write misc_controls_trackball text evidence.");
        }

        /** Writes one optional tightly packed RGBA artifact. */
        void writeTrackballRgba(
            const eastl::string &path,
            const eastl::vector<uint8_t> &rgba)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareTrackballOutput(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write misc_controls_trackball RGBA evidence.");
        }
    } // namespace

    void MiscControlsTrackballRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-perspective" &&
            options.targetFrame == 0u &&
            options.inputReplayPath.empty();
        const bool replay =
            ((options.scenarioId == "perspective-rotate-zoom-pan" &&
              options.targetFrame == 30u) ||
             (options.scenarioId == "orthographic-camera" &&
              options.targetFrame == 1u) ||
             (options.scenarioId == "orthographic-key-modes" &&
              options.targetFrame == 30u)) &&
            !options.inputReplayPath.empty();
        if (options.caseId != "misc_controls_trackball" ||
            (!initial && !replay) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "misc_controls_trackball requires the locked r185 extent, seed, frame, and replay contract.");
        }
        device = inDevice;
        buildTrackballCone(vertices, indices);
        buildTrackballInstances(options.randomSeed, instances);
        const glm::dvec3 camera = selectTrackballCameraPosition(options.scenarioId);
        const glm::dvec3 target = selectTrackballTarget(options.scenarioId);
        const glm::dmat4 view = glm::lookAtRH(
            camera,
            target,
            selectTrackballUp(options.scenarioId));
        objectData.view = glm::mat4(view);
        const bool orthographic =
            options.scenarioId == "orthographic-camera" ||
            options.scenarioId == "orthographic-key-modes";
        const double zoom = options.scenarioId == "orthographic-key-modes"
            ? 1.4425016169146563
            : 1.0;
        const glm::dmat4 projection = orthographic
            ? makeTrackballOrthographicProjection(options.width, options.height, zoom)
            : makeTrackballProjection(options.width, options.height);
        objectData.viewProjection = glm::mat4(projection * view);
        objectData.whiteLightDirection = glm::vec4(glm::vec3(glm::normalize(
            glm::dvec3(view * glm::dvec4(1.0, 1.0, 1.0, 0.0)))), 0.0f);
        objectData.blueLightDirection = glm::vec4(glm::vec3(glm::normalize(
            glm::dvec3(view * glm::dvec4(-1.0, -1.0, -1.0, 0.0)))), 0.0f);
        objectData.fogAndReserved = glm::vec4(0.002f, 0.0f, 0.0f, 0.0f);
        materialData.diffuseAndShininess = glm::vec4(1.0f, 1.0f, 1.0f, 30.0f);
        materialData.specularAndReserved = glm::vec4(
            0.00560539f, 0.00560539f, 0.00560539f, 0.0f);
        materialData.ambientAndReserved = glm::vec4(
            0.0908417f, 0.0908417f, 0.0908417f, 0.0f);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("misc_controls_trackball could not create its RenderSet encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = TrackballInstanceCount;
        appendTrackballPayload(
            allocation,
            MiscControlsTrackballSceneRenderSetComponents::vertices,
            "MiscControlsTrackballVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendTrackballPayload(
            allocation,
            MiscControlsTrackballSceneRenderSetComponents::indices,
            "MiscControlsTrackballIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendTrackballPayload(
            allocation,
            MiscControlsTrackballSceneRenderSetComponents::objects,
            "MiscControlsTrackballObject",
            &objectData,
            sizeof(objectData));
        appendTrackballPayload(
            allocation,
            MiscControlsTrackballSceneRenderSetComponents::instances,
            "MiscControlsTrackballInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            TrackballInstanceCount);
        appendTrackballPayload(
            allocation,
            MiscControlsTrackballSceneRenderSetComponents::materials,
            "MiscControlsTrackballMaterial",
            &materialData,
            sizeof(materialData));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MiscControlsTrackballRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void MiscControlsTrackballRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeTrackballRgba(options.captureRgbaPath, rgba);

        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"misc_controls_trackball\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"inputReplay\":";
        if (options.scenarioId == "initial-perspective")
        {
            metadata << "null";
        }
        else
        {
            const char *sha256 = options.scenarioId == "perspective-rotate-zoom-pan"
                ? "6ae0ed9e5bfb7c883e65f3873cd9faf6dff728670933b2b3160e3f2bfc1f351c"
                : options.scenarioId == "orthographic-camera"
                    ? "98566492e07ac94514687650a8ff45f03ede3a9ecaa46619f88309a9aa406ac0"
                    : "25eff7bbdf3f0f9062a03fe0fd95c00d70f85ca1624b5c4cf6291d4bcf04414f";
            const uint32_t eventCount = options.scenarioId == "perspective-rotate-zoom-pan"
                ? 9u
                : options.scenarioId == "orthographic-camera" ? 1u : 16u;
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_trackball\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":" << options.targetFrame
                << ",\"sha256\":\"" << sha256
                << "\",\"target\":\"body > canvas\",\"eventCount\":"
                << eventCount << "}";
        }
        metadata << "}\n";
        writeTrackballText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_trackball\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":500,"
            << "\"vertexCount\":19,\"indexCount\":24,"
            << "\"scenePassCount\":1,\"screenPassCount\":0,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"MiscControlsTrackballSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"MiscControlsTrackballSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":"
            << "\"instanced-cones\",\"instanceCount\":500}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-main\",\"renderClass\":"
            << "\"MiscControlsTrackballSceneMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"scene-main\"}]}\n";
        writeTrackballText(options.sceneSnapshotPath, snapshot.str());
        writeTrackballText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void MiscControlsTrackballRuntimeAdapter::shutdown(
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
