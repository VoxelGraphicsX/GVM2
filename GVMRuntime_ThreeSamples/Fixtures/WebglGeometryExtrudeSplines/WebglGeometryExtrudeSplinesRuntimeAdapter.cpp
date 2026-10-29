#include "WebglGeometryExtrudeSplinesRuntimeAdapter.hpp"

#include "ThreeCompat/CurveExtras.hpp"
#include "ThreeCompat/TubeMesh.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t SplineEntityCount = 4u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(
            sizeof(WebglGeometryExtrudeSplinesHostVertex) ==
            48u);
        static_assert(
            sizeof(WebglGeometryExtrudeSplinesHostObjectData) ==
            176u);
        static_assert(
            sizeof(WebglGeometryExtrudeSplinesHostInstanceData) ==
            48u);
        static_assert(
            sizeof(WebglGeometryExtrudeSplinesHostMaterialData) ==
            32u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareSplineOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Validates one of the three frozen spline extrusion scenarios. */
        void validateSplineScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u &&
                options.inputReplayPath.empty();
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 120u &&
                options.inputReplayPath.empty();
            const bool alternate =
                options.scenarioId == "alternate-spline" &&
                options.targetFrame == 121u &&
                !options.inputReplayPath.empty();
            if (options.caseId !=
                    "webgl_geometry_extrude_splines" ||
                (!initial && !animated && !alternate) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument(
                    "Spline extrusion requires the locked case, scenario, extent, seed, and replay contract.");
            }
            if (alternate &&
                !std::filesystem::is_regular_file(
                    std::filesystem::path(
                        options.inputReplayPath.c_str())))
            {
                throw std::invalid_argument(
                    "The alternate spline scenario requires its locked replay file.");
            }
        }

        /** Builds Three's OpenGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeSplineProjection(
            double fieldOfViewDegrees,
            double farDistance)
        {
            constexpr double NearDistance = 0.01;
            const double top =
                NearDistance *
                std::tan(
                    fieldOfViewDegrees *
                    3.14159265358979323846 /
                    360.0);
            const double height = 2.0 * top;
            const double width = 1.6 * height;
            const double left = -0.5 * width;
            glm::mat4 projection(0.0f);
            projection[0][0] =
                float(2.0 * NearDistance / width);
            projection[1][1] =
                float(2.0 * NearDistance / height);
            projection[2][0] =
                float(-(0.5 * width + left) /
                      (0.5 * width));
            projection[2][1] = 0.0f;
            projection[2][2] =
                float(-(farDistance + NearDistance) /
                      (farDistance - NearDistance));
            projection[2][3] = -1.0f;
            projection[3][2] =
                float(
                    -2.0 *
                    farDistance *
                    NearDistance /
                    (farDistance - NearDistance));
            return projection;
        }

        /** Appends one indexed solid TubeGeometry entity. */
        void appendSplineTubeEntity(
            eastl::vector<
                WebglGeometryExtrudeSplinesEntityData> &entities,
            const ThreeCompat::TubeMesh &mesh)
        {
            WebglGeometryExtrudeSplinesEntityData entity;
            entity.logicalId = "spline-tube";
            entity.vertices.reserve(
                mesh.positions.size());
            for (size_t index = 0u;
                 index < mesh.positions.size();
                 ++index)
            {
                entity.vertices.push_back({
                    glm::vec4(
                        mesh.positions[index],
                        1.0f),
                    glm::vec4(
                        mesh.normals[index],
                        0.0f),
                    glm::vec4(0.0f),
                });
            }
            entity.indices = mesh.indices;
            entity.instanceData.push_back({
                glm::vec4(0.0f),
                glm::vec4(0.0f),
                glm::vec4(0.0f)});
            entity.materialData.colorAndOpacity =
                glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
            entity.materialData.phaseAndReserved =
                glm::vec4(0.0f);
            entities.push_back(eastl::move(entity));
        }

        /** Packs TubeGeometry edges into one instanced screen-space quad Set. */
        void appendSplineWireframeEntity(
            eastl::vector<
                WebglGeometryExtrudeSplinesEntityData> &entities,
            const ThreeCompat::TubeMesh &mesh,
            const glm::mat4 &viewProjection)
        {
            (void)viewProjection;
            WebglGeometryExtrudeSplinesEntityData entity;
            entity.logicalId = "spline-wireframe";
            // Each logical GL line is represented by the two endpoint
            // vertices required by the existing LineList topology.  The
            // endpoint positions live in the per-instance component so the
            // entire spline still occupies one RenderSet entity.
            entity.vertices = {
                {{0.0f, 0.0f, 0.0f, 1.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f}},
                {{0.0f, 0.0f, 0.0f, 1.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f},
                 {1.0f, 0.0f, 0.0f, 0.0f}},
            };
            entity.indices = {0u, 1u};
            entity.instanceData.reserve(mesh.indices.size());
            for (size_t edge = 0u; edge < mesh.indices.size(); ++edge)
            {
                const size_t nextEdge =
                    edge % 3u == 2u ? edge - 2u : edge + 1u;
                const glm::vec3 start =
                    mesh.positions[mesh.indices[edge]];
                const glm::vec3 end =
                    mesh.positions[mesh.indices[nextEdge]];
                entity.instanceData.push_back({
                    glm::vec4(0.0f),
                    glm::vec4(
                        start,
                        1.0f),
                    glm::vec4(
                        end,
                        1.0f)});
            }
            entity.materialData.colorAndOpacity =
                // The browser's one-pixel wireframe coverage is represented
                // by this deterministic source-alpha envelope after the
                // native line rasterization.  Keeping it at 0.30 preserves
                // the same blended edge energy on Metal and Vulkan.
                glm::vec4(0.0f, 0.0f, 0.0f, 0.30f);
            entity.materialData.phaseAndReserved =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            entities.push_back(eastl::move(entity));
        }

        /** Appends one culled helper entity while retaining the logical Scene node. */
        void appendInvisibleSplineEntity(
            eastl::vector<
                WebglGeometryExtrudeSplinesEntityData> &entities,
            const char *logicalId)
        {
            WebglGeometryExtrudeSplinesEntityData entity;
            entity.logicalId = logicalId;
            entity.vertices = {
                {{0.0f, 0.0f, 0.0f, 1.0f},
                 {0.0f, 0.0f, 1.0f, 0.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f}},
                {{0.0f, 0.0f, 0.0f, 1.0f},
                 {0.0f, 0.0f, 1.0f, 0.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f}},
                {{0.0f, 0.0f, 0.0f, 1.0f},
                 {0.0f, 0.0f, 1.0f, 0.0f},
                 {0.0f, 0.0f, 0.0f, 0.0f}},
            };
            entity.indices = {0u, 1u, 2u};
            entity.instanceData.push_back({
                glm::vec4(0.0f),
                glm::vec4(0.0f),
                glm::vec4(0.0f)});
            entity.materialData.phaseAndReserved =
                glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
            entities.push_back(eastl::move(entity));
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendSplineBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount = 1u)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Allocates one spline entity through existing RenderSet semantics. */
        void allocateSplineEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglGeometryExtrudeSplinesEntityData &entity)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(
                    entity.vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(
                    entity.indices.size());
            allocation.instanceCount = static_cast<uint32_t>(
                entity.instanceData.size());
            const eastl::string vertexName =
                entity.logicalId + "-vertices";
            const eastl::string indexName =
                entity.logicalId + "-indices";
            const eastl::string objectName =
                entity.logicalId + "-object";
            const eastl::string instanceName =
                entity.logicalId + "-instance";
            const eastl::string materialName =
                entity.logicalId + "-material";
            appendSplineBufferPayload(
                allocation,
                WebglGeometryExtrudeSplinesSceneRenderSetComponents::
                    vertices,
                vertexName.c_str(),
                entity.vertices.data(),
                entity.vertices.size() *
                    sizeof(WebglGeometryExtrudeSplinesHostVertex));
            appendSplineBufferPayload(
                allocation,
                WebglGeometryExtrudeSplinesSceneRenderSetComponents::
                    indices,
                indexName.c_str(),
                entity.indices.data(),
                entity.indices.size() *
                    sizeof(uint32_t));
            appendSplineBufferPayload(
                allocation,
                WebglGeometryExtrudeSplinesSceneRenderSetComponents::
                    objects,
                objectName.c_str(),
                &entity.objectData,
                sizeof(entity.objectData));
            appendSplineBufferPayload(
                allocation,
                WebglGeometryExtrudeSplinesSceneRenderSetComponents::
                    instances,
                instanceName.c_str(),
                entity.instanceData.data(),
                entity.instanceData.size() *
                    sizeof(entity.instanceData[0u]),
                static_cast<uint32_t>(entity.instanceData.size()));
            appendSplineBufferPayload(
                allocation,
                WebglGeometryExtrudeSplinesSceneRenderSetComponents::
                    materials,
                materialName.c_str(),
                &entity.materialData,
                sizeof(entity.materialData));
            encoder.allocEntity(allocation);
        }
    }

    void WebglGeometryExtrudeSplinesRuntimeAdapter::
        initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
    {
        validateSplineScenario(options);
        device = inDevice;
        alternateSpline =
            options.scenarioId == "alternate-spline";
        const ThreeCompat::CurveScaleContext curveScale = {
            .scale = alternateSpline ? 20.0 : 20.0,
        };
        const ThreeCompat::TubeMeshParameters tubeParameters = {
                .evaluatePoint =
                    alternateSpline
                    ? ThreeCompat::evaluateCurveExtrasTorusKnot
                    : ThreeCompat::evaluateGrannyKnot,
                .curveContext = &curveScale,
                .tubularSegments = 100u,
                .radius = 2.0,
                .radialSegments = 3u,
                .closed = true,
                .arcLengthDivisions = 200u,
            };
        const ThreeCompat::TubeMesh tube =
            ThreeCompat::buildTubeMesh(
                tubeParameters);
        entities.clear();
        entities.reserve(SplineEntityCount);
        appendSplineTubeEntity(entities, tube);

        const glm::mat4 projection =
            makeSplineProjection(
                alternateSpline ? 84.0 : 50.0,
                alternateSpline ? 1000.0 : 10000.0);
        glm::mat4 view =
            glm::mat4(
                glm::lookAt(
                    glm::dvec3(0.0, 50.0, 500.0),
                    glm::dvec3(0.0),
                    glm::dvec3(0.0, 1.0, 0.0)));
        if (alternateSpline)
        {
            const double normalizedTime =
                std::fmod(
                    double(options.targetFrame) /
                        60.0,
                    20.0) /
                20.0;
            const ThreeCompat::TubeCurveSample curveSample =
                ThreeCompat::sampleTubeCurve(
                    tubeParameters,
                    normalizedTime);
            const double framePosition =
                normalizedTime *
                double(tube.tangents.size());
            const size_t frameIndex =
                static_cast<size_t>(
                    std::floor(framePosition)) %
                tube.frameBinormals.size();
            const size_t nextFrameIndex =
                (frameIndex + 1u) %
                tube.frameBinormals.size();
            const double frameFraction =
                framePosition -
                std::floor(framePosition);
            const glm::dvec3 binormal =
                tube.frameBinormals[frameIndex] +
                (tube.frameBinormals[nextFrameIndex] -
                 tube.frameBinormals[frameIndex]) *
                    frameFraction;
            const glm::dvec3 normal =
                glm::cross(
                    binormal,
                    curveSample.tangent);
            const glm::dvec3 cameraPosition =
                curveSample.point * 4.0 +
                normal * 15.0;
                view =
                glm::lookAt(
                    glm::vec3(cameraPosition),
                    glm::vec3(
                        cameraPosition +
                        curveSample.tangent),
                        glm::vec3(normal));
        }
        appendSplineWireframeEntity(entities, tube, projection * view);
        appendInvisibleSplineEntity(
            entities,
            "spline-camera-helper");
        appendInvisibleSplineEntity(
            entities,
            "spline-camera-eye");
        if (entities.size() != SplineEntityCount)
        {
            throw std::logic_error(
                "Spline extrusion must allocate four logical entities.");
        }
        for (auto &entity : entities)
        {
            entity.objectData.modelView =
                view * glm::scale(
                    glm::mat4(1.0f),
                    glm::vec3(4.0f));
            entity.objectData.projection = projection;
            entity.objectData.viewport =
                glm::vec4(
                    float(options.width) * 0.5f,
                    float(options.height) * 0.5f,
                    0.0f,
                    0.0f);
            entity.objectData.ambientAndDirectionalIntensity =
                glm::vec4(1.0f, 1.5f, 0.0f, 0.0f);
            entity.objectData.directionalView =
                glm::vec4(
                    glm::normalize(
                        glm::vec3(
                            view *
                            glm::vec4(
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f))),
                    0.0f);
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Spline extrusion could not create its Scene Set encoder.");
        }
        for (const auto &entity : entities)
        {
            allocateSplineEntity(
                *encoder,
                entity);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglGeometryExtrudeSplinesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryExtrudeSplinesRuntimeAdapter::afterFrame(
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
            uint64_t(width) *
            uint64_t(height) *
            4u;
        if (byteCount >
            std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Spline extrusion RGBA capture exceeds host storage.");
        }
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
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareSplineOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary |
                    std::ios::out |
                    std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(
                    rgba.data()),
                static_cast<std::streamsize>(
                    rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the spline extrusion RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareSplineOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_geometry_extrude_splines\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str()
                << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend)
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":"
                << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"";
            if (alternateSpline)
            {
                output
                    << ",\n  \"inputReplay\":{\n"
                    << "    \"sha256\":\"2230f652a5b67c65b479d8ffe7dec9d47274ad99e946025b35b8a1244c78d8c4\",\n"
                    << "    \"caseId\":\"webgl_geometry_extrude_splines\",\n"
                    << "    \"scenarioId\":\"alternate-spline\",\n"
                    << "    \"captureFrame\":121,\n"
                    << "    \"eventCount\":3,\n"
                    << "    \"target\":\"#container > canvas\"\n"
                    << "  }";
            }
            output << "\n}\n";
        }
        uint64_t vertexCount = 0u;
        uint64_t indexCount = 0u;
        uint64_t totalInstanceCount = 0u;
        for (const auto &entity : entities)
        {
            vertexCount += entity.vertices.size();
            indexCount += entity.indices.size();
            totalInstanceCount += entity.instanceData.size();
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareSplineOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_geometry_extrude_splines\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":4,\n"
                << "  \"entityCount\":" << entities.size() << ",\n"
                << "  \"instanceCount\":" << totalInstanceCount << ",\n"
                << "  \"instanceCounts\":[";
            for (size_t entityIndex = 0u;
                 entityIndex < entities.size();
                 ++entityIndex)
            {
                if (entityIndex != 0u)
                {
                    output << ',';
                }
                output << entities[entityIndex].instanceData.size();
            }
            output
                << "],\n"
                << "  \"vertexCount\":" << vertexCount << ",\n"
                << "  \"indexCount\":" << indexCount << ",\n"
                << "  \"scenePassCount\":2,\n"
                << "  \"screenPassCount\":0,\n"
                << "  \"drawCommandCount\":2,\n"
                << "  \"renderSetType\":\"WebglGeometryExtrudeSplinesSceneRenderSet\",\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene-set\",\n"
                << "    \"renderSetType\":\"WebglGeometryExtrudeSplinesSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":4,\n"
                << "    \"entityCount\":" << entities.size() << ",\n"
                << "    \"entities\":[";
            for (size_t entityIndex = 0u;
                 entityIndex < entities.size();
                 ++entityIndex)
            {
                if (entityIndex != 0u)
                {
                    output << ',';
                }
                output
                    << "{\"entityId\":" << entityIndex
                    << ",\"logicalRenderableId\":\""
                    << entities[entityIndex].logicalId.c_str()
                    << "\",\"instanceCount\":"
                    << entities[entityIndex].instanceData.size()
                    << '}';
            }
            output << "],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                << "    \"drawCommandCount\":2,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":["
                << "{\"name\":\"opaque\",\"renderClass\":\"WebglGeometryExtrudeSplinesOpaquePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"transparent-wireframe\",\"renderClass\":\"WebglGeometryExtrudeSplinesTransparentWireframePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                << "  }],\n"
                << "  \"scenePasses\":["
                << "\"WebglGeometryExtrudeSplinesOpaquePass\","
                << "\"WebglGeometryExtrudeSplinesTransparentWireframePass\"],\n"
                << "  \"scenePassSequence\":["
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque\",\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-wireframe\",\"entityOrdinal\":0}],\n"
                << "  \"screenPasses\":[],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":true,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareSplineOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_geometry_extrude_splines\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n"
                << "  \"spline\":\""
                << (alternateSpline
                        ? "TorusKnot"
                        : "GrannyKnot")
                << "\",\n"
                << "  \"tubularSegments\":100,\n"
                << "  \"radiusSegments\":3,\n"
                << "  \"closed\":true,\n"
                << "  \"entityCount\":4\n"
                << "}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryExtrudeSplinesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
