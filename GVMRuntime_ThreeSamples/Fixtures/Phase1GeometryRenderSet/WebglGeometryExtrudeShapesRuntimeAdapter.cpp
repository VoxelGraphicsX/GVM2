#include "WebglGeometryExtrudeShapesRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *OrbitReplaySha256 =
            "414a4afe4ed204bfcf3a17031b7f1b044f17f1d9814167dc902bfb49f06551d7";
        constexpr const char *UpstreamGeometrySha256[3u] = {
            "cb6100f9857c733a697a7f31ffe88a58d1fccaccec796ae87518f6b196f86261",
            "7b139651a20472b76cbcbb93a431cae3b38208a4ff062c3445dbca9f35561d97",
            "dc46386681b890684359b2fa2915a9e2f91ab7660bb51aabd2f314501136dc1f",
        };
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(ExtrudeCpuVertex) == 48u);
        static_assert(sizeof(ExtrudeHostObjectData) == 176u);
        static_assert(sizeof(ExtrudeHostInstanceData) == 16u);
        static_assert(sizeof(ExtrudeHostMaterialData) == 16u);

        /** Creates parent directories for one requested capture artifact. */
        void prepareExtrudeOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Calculates a lowercase SHA-256 digest for replay validation. */
        eastl::string calculateExtrudeSha256(
            const void *bytes,
            size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "Extrude replay exceeds CommonCrypto input limits.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (uint8_t byte : digest)
            {
                stream << std::setw(2) << static_cast<uint32_t>(byte);
            }
            return eastl::string(stream.str().c_str());
        }

        /** Calculates the r185 position-then-normal Float32 geometry digest. */
        eastl::string calculateExtrudeGeometrySha256(
            const ExtrudeCpuGeometry &geometry)
        {
            eastl::vector<float> attributes;
            attributes.reserve(geometry.vertices.size() * 6u);
            for (const ExtrudeCpuVertex &vertex : geometry.vertices)
            {
                attributes.push_back(vertex.position.x);
                attributes.push_back(vertex.position.y);
                attributes.push_back(vertex.position.z);
            }
            for (const ExtrudeCpuVertex &vertex : geometry.vertices)
            {
                attributes.push_back(vertex.normal.x);
                attributes.push_back(vertex.normal.y);
                attributes.push_back(vertex.normal.z);
            }
            return calculateExtrudeSha256(
                attributes.data(),
                attributes.size() * sizeof(float));
        }

        /** Returns one upper-24-bit JavaScript unit random value. */
        double nextExtrudeRandomUnit(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Reproduces one MathUtils.randFloat call. */
        double nextExtrudeRandomRange(
            ThreeCompat::DeterministicRandom &random,
            double minimum,
            double maximum)
        {
            return minimum +
                   nextExtrudeRandomUnit(random) * (maximum - minimum);
        }

        /** Returns the exact triangle contour used by the closed extrusion. */
        eastl::vector<glm::dvec2> buildTriangleShape()
        {
            eastl::vector<glm::dvec2> points;
            points.reserve(3u);
            for (uint32_t index = 0u; index < 3u; ++index)
            {
                const double angle =
                    2.0 * double(index) / 3.0 * Pi;
                points.push_back(
                    {std::cos(angle) * 20.0,
                     std::sin(angle) * 20.0});
            }
            return points;
        }

        /** Returns the exact ten-point star contour shared by two entities. */
        eastl::vector<glm::dvec2> buildStarShape()
        {
            eastl::vector<glm::dvec2> points;
            points.reserve(10u);
            for (uint32_t index = 0u; index < 10u; ++index)
            {
                const double radius = index % 2u == 1u ? 10.0 : 20.0;
                const double angle = double(index) / 5.0 * Pi;
                points.push_back(
                    {std::cos(angle) * radius,
                     std::sin(angle) * radius});
            }
            return points;
        }

        /** Builds the exact fixed closed Catmull-Rom path. */
        PathExtrudeParameters buildClosedPathParameters()
        {
            PathExtrudeParameters parameters;
            parameters.controlPoints = {
                {-60.0, -100.0, 60.0},
                {-60.0, 20.0, 60.0},
                {-60.0, 120.0, 60.0},
                {60.0, 20.0, -60.0},
                {60.0, -100.0, -60.0},
            };
            parameters.shape = buildTriangleShape();
            parameters.steps = 100u;
            parameters.closed = true;
            parameters.uniformCatmullRom = true;
            parameters.tension = 0.5;
            parameters.materialSlot = 0u;
            return parameters;
        }

        /** Builds the deterministic ten-point random Catmull-Rom path. */
        PathExtrudeParameters buildRandomPathParameters(uint32_t randomSeed)
        {
            ThreeCompat::DeterministicRandom random(randomSeed);
            for (uint32_t draw = 0u; draw < 148u; ++draw)
            {
                (void)random.nextUint32();
            }
            PathExtrudeParameters parameters;
            parameters.controlPoints.reserve(10u);
            for (uint32_t index = 0u; index < 10u; ++index)
            {
                parameters.controlPoints.push_back({
                    (double(index) - 4.5) * 50.0,
                    nextExtrudeRandomRange(random, -50.0, 50.0),
                    nextExtrudeRandomRange(random, -50.0, 50.0),
                });
            }
            parameters.shape = buildStarShape();
            parameters.steps = 200u;
            parameters.closed = false;
            parameters.uniformCatmullRom = false;
            parameters.materialSlot = 0u;
            return parameters;
        }

        /** Builds the exact one-step, one-segment bevel configuration. */
        DepthExtrudeParameters buildBevelParameters()
        {
            DepthExtrudeParameters parameters;
            parameters.shape = buildStarShape();
            parameters.depth = 20.0;
            parameters.steps = 1u;
            parameters.bevelEnabled = true;
            parameters.bevelThickness = 2.0;
            parameters.bevelSize = 4.0;
            parameters.bevelSegments = 1u;
            parameters.lidMaterialSlot = 0u;
            parameters.sideMaterialSlot = 1u;
            return parameters;
        }

        /** Converts one sRGB byte channel to Three's linear working space. */
        float extrudeSrgbByteToLinear(uint32_t channel)
        {
            const double srgb = double(channel) / 255.0;
            return static_cast<float>(
                srgb <= 0.04045
                    ? srgb / 12.92
                    : std::pow((srgb + 0.055) / 1.055, 2.4));
        }

        /** Builds the zero-to-one perspective projection used by the host. */
        glm::mat4 makeExtrudePerspective(double fieldOfViewDegrees,
                                         double aspect,
                                         double nearDistance,
                                         double farDistance)
        {
            const double top =
                nearDistance *
                std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = aspect * height;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                static_cast<float>(2.0 * nearDistance / width);
            projection[1u][1u] =
                static_cast<float>(2.0 * nearDistance / height);
            projection[2u][2u] =
                static_cast<float>(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                static_cast<float>(
                    -farDistance * nearDistance / depth);
            return projection;
        }

        /** Returns the canonical camera produced by the locked Trackball replay. */
        ExtrudeScenarioState buildOrbitScenarioState()
        {
            ExtrudeScenarioState state;
            state.cameraPosition = {
                -382.39207392788694,
                -191.19603696394347,
                -259.26892842454464,
            };
            state.cameraUp = {
                -0.6074151427396358,
                0.6962924286301818,
                0.3823920739278874,
            };
            state.replaySha256 = OrbitReplaySha256;
            state.replayEventCount = 3u;
            return state;
        }

        /** Validates the exact Manifest scenario and locked replay identity. */
        ExtrudeScenarioState validateExtrudeOptions(
            const ThreeSampleHostOptions &options)
        {
            if (options.caseId !=
                "webgl_geometry_extrude_shapes")
            {
                throw std::invalid_argument(
                    "Extrude adapter requires its dedicated case-id.");
            }
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool orbit =
                options.scenarioId == "orbit-input" &&
                options.targetFrame == 61u;
            if (!initial && !animated && !orbit)
            {
                throw std::invalid_argument(
                    "Extrude scenario or target frame differs from the Manifest.");
            }
            if (!orbit)
            {
                if (!options.inputReplayPath.empty())
                {
                    throw std::invalid_argument(
                        "Non-interactive extrusion scenarios cannot consume replay.");
                }
                return {};
            }
            if (options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "orbit-input requires the locked Trackball replay.");
            }
            const std::filesystem::path replayPath(
                options.inputReplayPath.c_str());
            const uintmax_t fileByteCount =
                std::filesystem::file_size(replayPath);
            eastl::vector<uint8_t> replay(
                static_cast<size_t>(fileByteCount));
            std::ifstream input(replayPath, std::ios::binary);
            input.read(reinterpret_cast<char *>(replay.data()),
                       static_cast<std::streamsize>(replay.size()));
            if (!input ||
                calculateExtrudeSha256(
                    replay.data(), replay.size()) != OrbitReplaySha256)
            {
                throw std::invalid_argument(
                    "Extrude Trackball replay identity differs.");
            }
            return buildOrbitScenarioState();
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendExtrudeBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Computes tightly packed RGBA8 storage while rejecting overflow. */
        uint64_t computeExtrudeRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() /
                    BytesPerPixel)
            {
                throw std::overflow_error(
                    "Extrude RGBA8 capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglGeometryExtrudeShapesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        scenarioState = validateExtrudeOptions(options);
        device = inDevice;
        entities[0u].logicalId = "closed-triangle-path";
        entities[0u].geometry =
            buildPathExtrudeGeometry(buildClosedPathParameters());
        entities[1u].logicalId = "random-star-path";
        entities[1u].geometry =
            buildPathExtrudeGeometry(
                buildRandomPathParameters(options.randomSeed));
        entities[2u].logicalId = "beveled-star";
        entities[2u].geometry =
            buildDepthExtrudeGeometry(buildBevelParameters());
        entities[2u].model =
            glm::translate(glm::mat4(1.0f),
                           glm::vec3(50.0f, 100.0f, 50.0f));
        const uint32_t expectedCounts[3u] =
            {1806u, 12048u, 228u};
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            ExtrudeEntityState &entity = entities[index];
            if (entity.geometry.vertices.size() !=
                expectedCounts[index])
            {
                throw std::runtime_error(
                    "Extrude CPU geometry count differs from r185.");
            }
            entity.geometrySha256 =
                calculateExtrudeGeometrySha256(entity.geometry);
            entity.indices.reserve(entity.geometry.vertices.size());
            for (uint32_t vertex = 0u;
                 vertex < entity.geometry.vertices.size(); ++vertex)
            {
                entity.indices.push_back(vertex);
            }
            entity.instanceData.translation = glm::vec4(0.0f);
        }
        const float red = extrudeSrgbByteToLinear(0xb0u);
        const float orangeGreen = extrudeSrgbByteToLinear(0x80u);
        const ExtrudeHostMaterialData redMaterial = {
            glm::vec4(red, 0.0f, 0.0f, 1.0f)};
        const ExtrudeHostMaterialData orangeMaterial = {
            glm::vec4(1.0f, orangeGreen, 0.0f, 1.0f)};
        entities[0u].materials = {redMaterial, redMaterial};
        entities[1u].materials = {orangeMaterial, orangeMaterial};
        entities[2u].materials = {redMaterial, orangeMaterial};
        updateObjectData(options.width, options.height);

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Extrude host could not create its Scene Set encoder.");
        }
        for (ExtrudeEntityState &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex
    WebglGeometryExtrudeShapesRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const ExtrudeEntityState &entity) const
    {
        if (entity.geometry.vertices.empty() ||
            entity.geometry.vertices.size() != entity.indices.size())
        {
            throw std::runtime_error(
                "Extrude RenderSet geometry is incomplete.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.geometry.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendExtrudeBufferPayload(
            allocation,
            WebglGeometryExtrudeShapesSceneRenderSetComponents::vertices,
            entity.logicalId + "-vertices",
            entity.geometry.vertices.data(),
            entity.geometry.vertices.size() *
                sizeof(ExtrudeCpuVertex),
            1u);
        appendExtrudeBufferPayload(
            allocation,
            WebglGeometryExtrudeShapesSceneRenderSetComponents::indices,
            entity.logicalId + "-indices",
            entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t), 1u);
        appendExtrudeBufferPayload(
            allocation,
            WebglGeometryExtrudeShapesSceneRenderSetComponents::objects,
            entity.logicalId + "-object",
            &entity.objectData, sizeof(entity.objectData), 1u);
        appendExtrudeBufferPayload(
            allocation,
            WebglGeometryExtrudeShapesSceneRenderSetComponents::instances,
            entity.logicalId + "-instance",
            &entity.instanceData, sizeof(entity.instanceData), 1u);
        appendExtrudeBufferPayload(
            allocation,
            WebglGeometryExtrudeShapesSceneRenderSetComponents::materials,
            entity.logicalId + "-materials",
            entity.materials.data(),
            entity.materials.size() *
                sizeof(ExtrudeHostMaterialData),
            static_cast<uint32_t>(entity.materials.size()));
        return encoder.allocEntity(allocation);
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::updateObjectData(
        uint32_t width,
        uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument(
                "Extrude capture dimensions must be positive.");
        }
        const glm::mat4 view = glm::lookAt(
            glm::vec3(scenarioState.cameraPosition),
            glm::vec3(0.0f),
            glm::vec3(scenarioState.cameraUp));
        const glm::mat4 projection = makeExtrudePerspective(
            45.0, double(width) / double(height), 1.0, 1000.0);
        const float ambient =
            extrudeSrgbByteToLinear(0x66u);
        for (ExtrudeEntityState &entity : entities)
        {
            entity.objectData.modelView = view * entity.model;
            entity.objectData.modelViewProjection =
                projection * entity.objectData.modelView;
            entity.objectData.viewportAndReserved =
                glm::vec4(float(width), float(height), 0.0f, 0.0f);
            entity.objectData.ambientColorAndReserved =
                glm::vec4(ambient, ambient, ambient, 0.0f);
            entity.objectData.pointLightViewPositionAndIntensity =
                glm::vec4(
                    glm::vec3(
                        view * glm::vec4(0.0f, 0.0f, 500.0f, 1.0f)),
                    3.0f);
        }
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)frameIndex;
        updateObjectData(options.width, options.height);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Extrude host could not create its update encoder.");
        }
        for (const ExtrudeEntityState &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglGeometryExtrudeShapesSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle, encoder);
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            computeExtrudeRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(
            options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        prepareExtrudeOutputPath(outputPath);
        std::ofstream output(
            outputPath, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write extrusion RGBA8 capture.");
        }
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        prepareExtrudeOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"source\":\"gvm-three-r185\",\n"
               << "  \"caseId\":\"webgl_geometry_extrude_shapes\",\n"
               << "  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\":\""
               << options.pipeline.c_str() << "\",\n"
               << "  \"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"randomSeed\":" << options.randomSeed << ",\n"
               << "  \"width\":" << width << ",\n"
               << "  \"height\":" << height << ",\n"
               << "  \"rowStrideBytes\":"
               << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\":" << byteCount << ",\n"
               << "  \"format\":\"rgba8unorm\",\n";
        if (options.scenarioId == "orbit-input")
        {
            output << "  \"inputReplay\":{\"schemaVersion\":1,"
                   << "\"caseId\":\"webgl_geometry_extrude_shapes\","
                   << "\"scenarioId\":\"orbit-input\","
                   << "\"captureFrame\":61,\"sha256\":\""
                   << scenarioState.replaySha256.c_str()
                   << "\",\"target\":\"body > canvas\","
                   << "\"eventCount\":"
                   << scenarioState.replayEventCount << "}\n";
        }
        else
        {
            output << "  \"inputReplay\":null\n";
        }
        output << "}\n";
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareExtrudeOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_geometry_extrude_shapes\",\n"
               << "  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"semantic-complete\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n"
               << "  \"assetHashes\":[],\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":3,\n"
               << "  \"entityCount\":3,\n"
               << "  \"instanceCounts\":[1,1,1],\n"
               << "  \"vertexCounts\":[1806,12048,228],\n"
               << "  \"geometrySha256\":[\""
               << entities[0u].geometrySha256.c_str() << "\",\""
               << entities[1u].geometrySha256.c_str() << "\",\""
               << entities[2u].geometrySha256.c_str() << "\"],\n"
               << "  \"upstreamGeometrySha256\":[\""
               << UpstreamGeometrySha256[0u] << "\",\""
               << UpstreamGeometrySha256[1u] << "\",\""
               << UpstreamGeometrySha256[2u] << "\"],\n"
               << "  \"geometryGroupCounts\":[2,2,2],\n"
               << "  \"renderSetType\":"
               << "\"WebglGeometryExtrudeShapesSceneRenderSet\",\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\","
               << "\"objects\",\"instances\",\"materials\"],\n"
               << "  \"scenePassCount\":1,\n"
               << "  \"screenPassCount\":0,\n"
               << "  \"scenePasses\":["
               << "\"WebglGeometryExtrudeShapesMainPass\"],\n"
               << "  \"screenPasses\":[],\n"
               << "  \"attachmentFormats\":[\"rgba8unorm\","
               << "\"depth32float\",\"rgba8unorm\"],\n"
               << "  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"scenePassSequence\":["
               << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main\","
               << "\"entityOrdinal\":0}],\n"
               << "  \"sceneRoots\":[{\n"
               << "    \"id\":\"scene\",\n"
               << "    \"renderSetCount\":1,\n"
               << "    \"renderSetId\":\"scene\",\n"
               << "    \"renderSetType\":"
               << "\"WebglGeometryExtrudeShapesSceneRenderSet\",\n"
               << "    \"renderableObjectCount\":3,\n"
               << "    \"entityCount\":3,\n"
               << "    \"drawCommandCount\":1,\n"
               << "    \"directDrawFallback\":false,\n"
               << "    \"componentSchema\":["
               << "{\"name\":\"vertices\",\"kind\":\"buffer\","
               << "\"role\":\"vertex\"},"
               << "{\"name\":\"indices\",\"kind\":\"buffer\","
               << "\"role\":\"index\"},"
               << "{\"name\":\"objects\",\"kind\":\"buffer\","
               << "\"role\":\"object\"},"
               << "{\"name\":\"instances\",\"kind\":\"buffer\","
               << "\"role\":\"instance\"},"
               << "{\"name\":\"materials\",\"kind\":\"buffer\","
               << "\"role\":\"material\"}],\n"
               << "    \"scenePasses\":[{\"name\":\"main\","
               << "\"renderClass\":"
               << "\"WebglGeometryExtrudeShapesMainPass\","
               << "\"renderSetId\":\"scene\","
               << "\"renderSetBindingCount\":1,"
               << "\"drawMode\":\"render-set-indexed-indirect\","
               << "\"invocationCount\":1,\"drawCommandCount\":1,"
               << "\"usesStandaloneGeometry\":false,"
               << "\"usesExplicitDrawCount\":false}],\n"
               << "    \"entities\":["
               << "{\"entityId\":0,\"logicalRenderableId\":"
               << "\"closed-triangle-path\",\"instanceCount\":1},"
               << "{\"entityId\":1,\"logicalRenderableId\":"
               << "\"random-star-path\",\"instanceCount\":1},"
               << "{\"entityId\":2,\"logicalRenderableId\":"
               << "\"beveled-star\",\"instanceCount\":1}]\n"
               << "  }]\n}\n";
    }

    void WebglGeometryExtrudeShapesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        for (ExtrudeEntityState &entity : entities)
        {
            entity.geometry.vertices.clear();
            entity.indices.clear();
        }
    }
} // namespace GVM::ThreeSamples
