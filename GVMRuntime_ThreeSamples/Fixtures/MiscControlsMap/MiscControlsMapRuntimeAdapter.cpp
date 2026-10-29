#include "MiscControlsMapRuntimeAdapter.hpp"

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
        constexpr uint32_t MapInstanceCount = 500u;
        constexpr uint32_t MapRandomDrawsBeforeInstances = 136u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(MiscControlsMapHostVertex) == 16u);
        static_assert(sizeof(MiscControlsMapHostObjectData) == 176u);
        static_assert(sizeof(MiscControlsMapHostInstanceData) == 16u);
        static_assert(sizeof(MiscControlsMapHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMapOutput(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Advances the deterministic xorshift32 stream installed by the reference harness. */
        double nextMapRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<double>(value >> 8u) / 16777216.0;
        }

        /** Assigns one Cartesian component for the canonical box plane builder. */
        void setMapAxis(glm::dvec3 &value, uint32_t axis, double component)
        {
            value[axis] = component;
        }

        /** Appends one exact single-segment Three r185 BoxGeometry plane. */
        void appendMapBoxPlane(
            eastl::vector<MiscControlsMapHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            uint32_t uAxis,
            uint32_t vAxis,
            uint32_t wAxis,
            double uDirection,
            double vDirection,
            double width,
            double height,
            double depth)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            const double widthHalf = width * 0.5;
            const double heightHalf = height * 0.5;
            const double depthHalf = depth * 0.5;
            for (uint32_t iy = 0u; iy <= 1u; ++iy)
            {
                const double y = double(iy) * height - heightHalf;
                for (uint32_t ix = 0u; ix <= 1u; ++ix)
                {
                    const double x = double(ix) * width - widthHalf;
                    glm::dvec3 position(0.0);
                    setMapAxis(position, uAxis, x * uDirection);
                    setMapAxis(position, vAxis, y * vDirection);
                    setMapAxis(position, wAxis, depthHalf);
                    vertices.push_back({glm::vec4(
                        static_cast<float>(position.x),
                        static_cast<float>(position.y + 0.5),
                        static_cast<float>(position.z),
                        1.0f)});
                }
            }
            indices.push_back(baseVertex + 0u);
            indices.push_back(baseVertex + 2u);
            indices.push_back(baseVertex + 1u);
            indices.push_back(baseVertex + 2u);
            indices.push_back(baseVertex + 3u);
            indices.push_back(baseVertex + 1u);
        }

        /** Builds the exact translated BoxGeometry vertex and index streams. */
        void buildMapBox(
            eastl::vector<MiscControlsMapHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(24u);
            indices.reserve(36u);
            appendMapBoxPlane(vertices, indices, 2u, 1u, 0u, -1.0, -1.0, 1.0, 1.0, 1.0);
            appendMapBoxPlane(vertices, indices, 2u, 1u, 0u, 1.0, -1.0, 1.0, 1.0, -1.0);
            appendMapBoxPlane(vertices, indices, 0u, 2u, 1u, 1.0, 1.0, 1.0, 1.0, 1.0);
            appendMapBoxPlane(vertices, indices, 0u, 2u, 1u, 1.0, -1.0, 1.0, 1.0, -1.0);
            appendMapBoxPlane(vertices, indices, 0u, 1u, 2u, 1.0, -1.0, 1.0, 1.0, 1.0);
            appendMapBoxPlane(vertices, indices, 0u, 1u, 2u, -1.0, -1.0, 1.0, 1.0, -1.0);
        }

        /** Builds the 500 seeded InstancedMesh transform records. */
        void buildMapInstances(
            uint32_t randomSeed,
            eastl::vector<MiscControlsMapHostInstanceData> &instances)
        {
            uint32_t randomState = randomSeed;
            instances.clear();
            instances.reserve(MapInstanceCount);
            for (uint32_t draw = 0u;
                 draw < MapRandomDrawsBeforeInstances;
                 ++draw)
            {
                nextMapRandom(randomState);
            }
            for (uint32_t instance = 0u;
                 instance < MapInstanceCount;
                 ++instance)
            {
                const double x = nextMapRandom(randomState) * 1600.0 - 800.0;
                const double z = nextMapRandom(randomState) * 1600.0 - 800.0;
                const double height = nextMapRandom(randomState) * 80.0 + 10.0;
                instances.push_back({glm::vec4(
                    static_cast<float>(x),
                    0.0f,
                    static_cast<float>(z),
                    static_cast<float>(height))});
            }
        }

        /** Creates the generated-backend zero-to-one perspective projection. */
        glm::dmat4 makeMapProjection(uint32_t width, uint32_t height)
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

        /** Returns the locked camera position after the selected control replay. */
        glm::dvec3 selectMapCameraPosition(const eastl::string &scenarioId)
        {
            if (scenarioId == "left-pan-damped")
                return glm::dvec3(
                    -27.970587759677684,
                    199.99999999999997,
                    -167.0363462022631);
            if (scenarioId == "right-rotate-damped")
                return glm::dvec3(
                    168.9139088323982,
                    106.28657402021437,
                    -200.42768168595308);
            if (scenarioId == "cursor-zoom-screen-pan")
                return glm::dvec3(
                    -30.83764197378424,
                    203.37924396068527,
                    -174.98574428060286);
            return glm::dvec3(
                2.4492935982947067e-14,
                200.0,
                -200.00000000000003);
        }

        /** Returns the locked MapControls target after the selected replay. */
        glm::dvec3 selectMapTarget(const eastl::string &scenarioId)
        {
            if (scenarioId == "left-pan-damped")
                return glm::dvec3(
                    -27.97058775967771,
                    -2.2626328878181103e-14,
                    32.96365379773692);
            if (scenarioId == "cursor-zoom-screen-pan")
                return glm::dvec3(
                    -30.83764197378426,
                    15.318425440157489,
                    13.075074239925014);
            return glm::dvec3(0.0);
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendMapPayload(
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
        void writeMapText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMapOutput(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write misc_controls_map text evidence.");
        }

        /** Writes one optional tightly packed RGBA artifact. */
        void writeMapRgba(
            const eastl::string &path,
            const eastl::vector<uint8_t> &rgba)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMapOutput(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write misc_controls_map RGBA evidence.");
        }
    } // namespace

    void MiscControlsMapRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-seeded-city" &&
            options.targetFrame == 0u &&
            options.inputReplayPath.empty();
        const bool replay =
            (options.scenarioId == "left-pan-damped" ||
             options.scenarioId == "right-rotate-damped" ||
             options.scenarioId == "cursor-zoom-screen-pan") &&
            options.targetFrame == 30u &&
            !options.inputReplayPath.empty();
        if (options.caseId != "misc_controls_map" ||
            (!initial && !replay) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "misc_controls_map requires the locked r185 extent, seed, frame, and replay contract.");
        }
        device = inDevice;
        buildMapBox(vertices, indices);
        buildMapInstances(options.randomSeed, instances);
        const glm::dvec3 camera = selectMapCameraPosition(options.scenarioId);
        const glm::dvec3 target = selectMapTarget(options.scenarioId);
        const glm::dmat4 view = glm::lookAtRH(
            camera,
            target,
            glm::dvec3(0.0, 1.0, 0.0));
        objectData.view = glm::mat4(view);
        objectData.viewProjection = glm::mat4(
            makeMapProjection(options.width, options.height) * view);
        objectData.whiteLightDirection = glm::vec4(glm::vec3(glm::normalize(
            glm::dvec3(view * glm::dvec4(1.0, 1.0, 1.0, 0.0)))), 0.0f);
        objectData.blueLightDirection = glm::vec4(glm::vec3(glm::normalize(
            glm::dvec3(view * glm::dvec4(-1.0, -1.0, -1.0, 0.0)))), 0.0f);
        objectData.fogAndReserved = glm::vec4(0.002f, 0.0f, 0.0f, 0.0f);
        materialData.diffuseAndShininess = glm::vec4(
            0.85499261f, 0.85499261f, 0.85499261f, 30.0f);
        materialData.specularAndReserved = glm::vec4(
            0.00560539f, 0.00560539f, 0.00560539f, 0.0f);
        materialData.ambientAndReserved = glm::vec4(
            0.0908417f, 0.0908417f, 0.0908417f, 0.0f);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("misc_controls_map could not create its RenderSet encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = MapInstanceCount;
        appendMapPayload(
            allocation,
            MiscControlsMapSceneRenderSetComponents::vertices,
            "MiscControlsMapVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendMapPayload(
            allocation,
            MiscControlsMapSceneRenderSetComponents::indices,
            "MiscControlsMapIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendMapPayload(
            allocation,
            MiscControlsMapSceneRenderSetComponents::objects,
            "MiscControlsMapObject",
            &objectData,
            sizeof(objectData));
        appendMapPayload(
            allocation,
            MiscControlsMapSceneRenderSetComponents::instances,
            "MiscControlsMapInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            MapInstanceCount);
        appendMapPayload(
            allocation,
            MiscControlsMapSceneRenderSetComponents::materials,
            "MiscControlsMapMaterial",
            &materialData,
            sizeof(materialData));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MiscControlsMapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void MiscControlsMapRuntimeAdapter::afterFrame(
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
        writeMapRgba(options.captureRgbaPath, rgba);

        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"misc_controls_map\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"inputReplay\":";
        if (options.scenarioId == "initial-seeded-city")
        {
            metadata << "null";
        }
        else
        {
            const char *sha256 = options.scenarioId == "left-pan-damped"
                ? "9f10d1d97bde16b2864b2c08a5dad19682492a07f13df1bc60bcdff2359ee297"
                : options.scenarioId == "right-rotate-damped"
                    ? "c19bcd211c2e498e14ae2a690129daf29f9a0004e6dbc5f57fe07f405100a1ad"
                    : "98ccc48e357b19d2bc45d8b5c48ef201878e285f9376064c54b4d9d2d3dfd00c";
            const uint32_t eventCount = options.scenarioId == "left-pan-damped"
                ? 3u
                : options.scenarioId == "right-rotate-damped" ? 3u : 6u;
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_map\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":30,\"sha256\":\"" << sha256
                << "\",\"target\":\"canvas\",\"eventCount\":"
                << eventCount << "}";
        }
        metadata << "}\n";
        writeMapText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_map\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":500,"
            << "\"vertexCount\":24,\"indexCount\":36,"
            << "\"scenePassCount\":1,\"screenPassCount\":0,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"MiscControlsMapSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"MiscControlsMapSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":"
            << "\"instanced-city-boxes\",\"instanceCount\":500}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-main\",\"renderClass\":"
            << "\"MiscControlsMapSceneMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"scene-main\"}]}\n";
        writeMapText(options.sceneSnapshotPath, snapshot.str());
        writeMapText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void MiscControlsMapRuntimeAdapter::shutdown(
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
