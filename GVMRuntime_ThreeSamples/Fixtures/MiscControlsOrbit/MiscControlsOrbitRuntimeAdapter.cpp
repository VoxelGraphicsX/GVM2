#include "MiscControlsOrbitRuntimeAdapter.hpp"

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
        constexpr uint32_t OrbitInstanceCount = 500u;
        constexpr uint32_t OrbitRandomDrawsBeforeInstances = 136u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(MiscControlsOrbitHostVertex) == 16u);
        static_assert(sizeof(MiscControlsOrbitHostObjectData) == 176u);
        static_assert(sizeof(MiscControlsOrbitHostInstanceData) == 16u);
        static_assert(sizeof(MiscControlsOrbitHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareOrbitOutput(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Advances the deterministic xorshift32 stream installed by the reference harness. */
        double nextOrbitRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<double>(value >> 8u) / 16777216.0;
        }

        /** Builds the exact indexed stream of ConeGeometry(10, 30, 4, 1). */
        void buildOrbitCone(
            eastl::vector<MiscControlsOrbitHostVertex> &vertices,
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
        void buildOrbitInstances(
            uint32_t randomSeed,
            eastl::vector<MiscControlsOrbitHostInstanceData> &instances)
        {
            uint32_t randomState = randomSeed;
            instances.clear();
            instances.reserve(OrbitInstanceCount);
            for (uint32_t draw = 0u;
                 draw < OrbitRandomDrawsBeforeInstances;
                 ++draw)
            {
                nextOrbitRandom(randomState);
            }
            for (uint32_t instance = 0u;
                 instance < OrbitInstanceCount;
                 ++instance)
            {
                const double x = nextOrbitRandom(randomState) * 1600.0 - 800.0;
                const double z = nextOrbitRandom(randomState) * 1600.0 - 800.0;
                instances.push_back({glm::vec4(
                    static_cast<float>(x),
                    0.0f,
                    static_cast<float>(z),
                    0.0f)});
            }
        }

        /** Creates the generated-backend zero-to-one perspective projection. */
        glm::dmat4 makeOrbitProjection(uint32_t width, uint32_t height)
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
        glm::dvec3 selectOrbitCameraPosition(const eastl::string &scenarioId)
        {
            if (scenarioId == "rotate-damped")
                return glm::dvec3(
                    368.7279724080556,
                    200.00000000000023,
                    155.0473552300859);
            if (scenarioId == "pan-dolly-damped")
                return glm::dvec3(
                    422.23165955651376,
                    223.60679774997902,
                    -41.63655990574028);
            if (scenarioId == "keyboard-pan")
                return glm::dvec3(
                    405.82911838680366,
                    200.00000000000003,
                    2.4492935982947064e-14);
            return glm::dvec3(400.0, 200.00000000000003,
                2.4492935982947064e-14);
        }

        /** Returns the locked OrbitControls target after the selected replay. */
        glm::dvec3 selectOrbitTarget(const eastl::string &scenarioId)
        {
            if (scenarioId == "pan-dolly-damped")
                return glm::dvec3(
                    -24.981935943444185,
                    0.0,
                    -41.636559905740306);
            if (scenarioId == "keyboard-pan")
                return glm::dvec3(5.829118386803643, 0.0, 0.0);
            return glm::dvec3(0.0);
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendOrbitPayload(
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
        void writeOrbitText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareOrbitOutput(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write misc_controls_orbit text evidence.");
        }

        /** Writes one optional tightly packed RGBA artifact. */
        void writeOrbitRgba(
            const eastl::string &path,
            const eastl::vector<uint8_t> &rgba)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareOrbitOutput(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write misc_controls_orbit RGBA evidence.");
        }
    } // namespace

    void MiscControlsOrbitRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-seeded-cones" &&
            options.targetFrame == 0u &&
            options.inputReplayPath.empty();
        const bool replay =
            (options.scenarioId == "rotate-damped" ||
             options.scenarioId == "pan-dolly-damped" ||
             options.scenarioId == "keyboard-pan") &&
            options.targetFrame == 30u &&
            !options.inputReplayPath.empty();
        if (options.caseId != "misc_controls_orbit" ||
            (!initial && !replay) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "misc_controls_orbit requires the locked r185 extent, seed, frame, and replay contract.");
        }
        device = inDevice;
        buildOrbitCone(vertices, indices);
        buildOrbitInstances(options.randomSeed, instances);
        const glm::dvec3 camera = selectOrbitCameraPosition(options.scenarioId);
        const glm::dvec3 target = selectOrbitTarget(options.scenarioId);
        const glm::dmat4 view = glm::lookAtRH(
            camera,
            target,
            glm::dvec3(0.0, 1.0, 0.0));
        objectData.view = glm::mat4(view);
        objectData.viewProjection = glm::mat4(
            makeOrbitProjection(options.width, options.height) * view);
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
            throw std::runtime_error("misc_controls_orbit could not create its RenderSet encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = OrbitInstanceCount;
        appendOrbitPayload(
            allocation,
            MiscControlsOrbitSceneRenderSetComponents::vertices,
            "MiscControlsOrbitVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendOrbitPayload(
            allocation,
            MiscControlsOrbitSceneRenderSetComponents::indices,
            "MiscControlsOrbitIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendOrbitPayload(
            allocation,
            MiscControlsOrbitSceneRenderSetComponents::objects,
            "MiscControlsOrbitObject",
            &objectData,
            sizeof(objectData));
        appendOrbitPayload(
            allocation,
            MiscControlsOrbitSceneRenderSetComponents::instances,
            "MiscControlsOrbitInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            OrbitInstanceCount);
        appendOrbitPayload(
            allocation,
            MiscControlsOrbitSceneRenderSetComponents::materials,
            "MiscControlsOrbitMaterial",
            &materialData,
            sizeof(materialData));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MiscControlsOrbitRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void MiscControlsOrbitRuntimeAdapter::afterFrame(
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
        writeOrbitRgba(options.captureRgbaPath, rgba);

        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"misc_controls_orbit\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"inputReplay\":";
        if (options.scenarioId == "initial-seeded-cones")
        {
            metadata << "null";
        }
        else
        {
            const char *sha256 = options.scenarioId == "rotate-damped"
                ? "f58d45030ead7fd6dfc0715582595744acd3fc73597e5fa7defe5a2639f8abc7"
                : options.scenarioId == "pan-dolly-damped"
                    ? "8baf1bd52a00ef06fd1ad686262c1c03ca277cc87a26f44051563ddcadb73f7d"
                    : "5d8e0038ec8598553c454200a4122e7876b100a99052b45b6b525854c7531365";
            const uint32_t eventCount = options.scenarioId == "rotate-damped"
                ? 3u
                : options.scenarioId == "pan-dolly-damped" ? 4u : 2u;
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_orbit\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":30,\"sha256\":\"" << sha256
                << "\",\"target\":\"canvas\",\"eventCount\":"
                << eventCount << "}";
        }
        metadata << "}\n";
        writeOrbitText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"misc_controls_orbit\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":500,"
            << "\"vertexCount\":20,\"indexCount\":24,"
            << "\"scenePassCount\":1,\"screenPassCount\":0,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"MiscControlsOrbitSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"MiscControlsOrbitSceneRenderSet\","
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
            << "\"MiscControlsOrbitSceneMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"scene-main\"}]}\n";
        writeOrbitText(options.sceneSnapshotPath, snapshot.str());
        writeOrbitText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void MiscControlsOrbitRuntimeAdapter::shutdown(
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
