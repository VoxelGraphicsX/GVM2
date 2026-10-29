#include "WebglGeometryNurbsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "ThreeCompat/Nurbs.hpp"
#include "ThreeCompat/ParametricMesh.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ExpectedEntityCount = 8u;
        constexpr uint32_t ExpectedCurveRandomState = 3461401442u;
        constexpr uint32_t PreCurveRandomCallCount = 104u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Provides immutable state to one NURBS surface evaluator callback. */
        struct NurbsSurfaceEvaluationContext
        {
            const ThreeCompat::NurbsAxis *uAxis = nullptr;
            const ThreeCompat::NurbsAxis *vAxis = nullptr;
            const eastl::vector<ThreeCompat::NurbsControlPoint> *
                controlPoints = nullptr;
            uint32_t vControlPointCount = 0u;
        };

        /** Selects one fixed-coordinate surface from the canonical NURBS volume. */
        enum class NurbsVolumeSurfaceMode : uint32_t
        {
            FixedW,
            FixedV,
            FixedU
        };

        /** Provides immutable state to one NURBS volume-surface callback. */
        struct NurbsVolumeEvaluationContext
        {
            const ThreeCompat::NurbsAxis *uAxis = nullptr;
            const ThreeCompat::NurbsAxis *vAxis = nullptr;
            const ThreeCompat::NurbsAxis *wAxis = nullptr;
            const eastl::vector<ThreeCompat::NurbsControlPoint> *
                controlPoints = nullptr;
            uint32_t vControlPointCount = 0u;
            uint32_t wControlPointCount = 0u;
            NurbsVolumeSurfaceMode mode =
                NurbsVolumeSurfaceMode::FixedW;
            double fixedParameter = 0.0;
        };

        static_assert(sizeof(WebglGeometryNurbsHostVertex) == 48u);
        static_assert(sizeof(WebglGeometryNurbsHostObjectData) == 176u);
        static_assert(sizeof(WebglGeometryNurbsHostInstanceData) == 16u);
        static_assert(sizeof(WebglGeometryNurbsHostMaterialData) == 32u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareNurbsOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Validates one of the three frozen NURBS Manifest scenarios. */
        void validateNurbsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u &&
                options.inputReplayPath.empty();
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u &&
                options.inputReplayPath.empty();
            const bool rotated =
                options.scenarioId == "rotated-input" &&
                options.targetFrame == 61u &&
                !options.inputReplayPath.empty();
            if (options.caseId != "webgl_geometry_nurbs" ||
                (!initial && !animated && !rotated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "NURBS requires the locked case, scenario, extent, seed, assets, and replay contract.");
            }
            if (rotated &&
                !std::filesystem::is_regular_file(
                    std::filesystem::path(
                        options.inputReplayPath.c_str())))
            {
                throw std::invalid_argument(
                    "The rotated NURBS scenario requires its locked replay file.");
            }
        }

        /** Returns one exact JavaScript Math.random binary64 value. */
        double nextNurbsRandom(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) /
                16777216.0;
        }

        /** Decodes one sRGB material constant into linear working space. */
        float nurbsSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow(
                    (value + 0.055f) / 1.055f,
                    2.4f);
        }

        /** Builds Three's OpenGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeNurbsProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2000.0;
            const double top =
                NearDistance *
                std::tan(50.0 * 3.14159265358979323846 / 360.0);
            const double height = 2.0 * top;
            const double width = (800.0 / 500.0) * height;
            const double depth =
                FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                float(2.0 * NearDistance / width);
            projection[1u][1u] =
                float(2.0 * NearDistance / height);
            projection[2u][2u] =
                float(
                    -(FarDistance + NearDistance) /
                    depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                float(
                    -2.0 * FarDistance * NearDistance /
                    depth);
            return projection;
        }

        /** Evaluates the canonical NURBS tensor-product surface. */
        glm::dvec3 evaluateNurbsSurfacePoint(
            double u,
            double v,
            const void *contextValue)
        {
            const auto &context =
                *static_cast<
                    const NurbsSurfaceEvaluationContext *>(
                        contextValue);
            return ThreeCompat::evaluateNurbsSurface(
                *context.uAxis,
                *context.vAxis,
                *context.controlPoints,
                context.vControlPointCount,
                u,
                v);
        }

        /** Evaluates one fixed-coordinate surface from the NURBS volume. */
        glm::dvec3 evaluateNurbsVolumeSurfacePoint(
            double first,
            double second,
            const void *contextValue)
        {
            const auto &context =
                *static_cast<
                    const NurbsVolumeEvaluationContext *>(
                        contextValue);
            double u = first;
            double v = second;
            double w = context.fixedParameter;
            if (context.mode ==
                NurbsVolumeSurfaceMode::FixedV)
            {
                v = context.fixedParameter;
                w = second;
            }
            else if (context.mode ==
                     NurbsVolumeSurfaceMode::FixedU)
            {
                u = context.fixedParameter;
                v = first;
                w = second;
            }
            return ThreeCompat::evaluateNurbsVolume(
                *context.uAxis,
                *context.vAxis,
                *context.wAxis,
                *context.controlPoints,
                context.vControlPointCount,
                context.wControlPointCount,
                u,
                v,
                w);
        }

        /** Appends one explicitly two-sided ParametricGeometry surface entity. */
        void appendNurbsSurfaceEntity(
            eastl::vector<WebglGeometryNurbsEntityData> &entities,
            const char *logicalId,
            const ThreeCompat::ParametricMesh &mesh,
            const glm::mat4 &localTransform)
        {
            WebglGeometryNurbsEntityData entity;
            entity.logicalId = logicalId;
            entity.vertices.reserve(mesh.indices.size() * 2u);
            entity.indices.reserve(mesh.indices.size() * 2u);
            for (size_t triangle = 0u;
                 triangle + 2u < mesh.indices.size();
                 triangle += 3u)
            {
                const uint32_t frontBase =
                    static_cast<uint32_t>(entity.vertices.size());
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const uint32_t index =
                        mesh.indices[triangle + corner];
                    entity.vertices.push_back({
                        glm::vec4(mesh.positions[index], 1.0f),
                        glm::vec4(-mesh.normals[index], 0.0f),
                        glm::vec4(
                            mesh.textureCoordinates[index],
                            0.0f,
                            0.0f),
                    });
                }
                entity.indices.insert(
                    entity.indices.end(),
                    {frontBase, frontBase + 1u, frontBase + 2u});

                const uint32_t backBase =
                    static_cast<uint32_t>(entity.vertices.size());
                for (uint32_t reversedCorner = 0u;
                     reversedCorner < 3u;
                     ++reversedCorner)
                {
                    const uint32_t corner = 2u - reversedCorner;
                    const uint32_t index =
                        mesh.indices[triangle + corner];
                    entity.vertices.push_back({
                        glm::vec4(mesh.positions[index], 1.0f),
                        glm::vec4(mesh.normals[index], 0.0f),
                        glm::vec4(
                            mesh.textureCoordinates[index],
                            0.0f,
                            0.0f),
                    });
                }
                entity.indices.insert(
                    entity.indices.end(),
                    {backBase, backBase + 1u, backBase + 2u});
            }
            entity.localTransform = localTransform;
            entity.instanceData.reserved =
                glm::vec4(0.0f);
            entity.materialData.colorAndOpacity =
                glm::vec4(1.0f);
            entity.materialData.phaseAndReserved =
                glm::vec4(0.0f);
            entities.push_back(eastl::move(entity));
        }

        /** Appends one line strip as deterministic screen-space triangle segments. */
        void appendNurbsLineEntity(
            eastl::vector<WebglGeometryNurbsEntityData> &entities,
            const char *logicalId,
            const eastl::vector<glm::dvec3> &points,
            const glm::mat4 &localTransform,
            float opacity,
            float phase)
        {
            if (points.size() < 2u)
            {
                throw std::invalid_argument(
                    "A NURBS line entity requires at least two points.");
            }
            WebglGeometryNurbsEntityData entity;
            entity.logicalId = logicalId;
            entity.vertices.reserve(
                (points.size() - 1u) * 4u);
            entity.indices.reserve(
                (points.size() - 1u) * 6u);
            for (size_t segment = 0u;
                 segment + 1u < points.size();
                 ++segment)
            {
                const glm::vec4 start(
                    glm::vec3(points[segment]),
                    1.0f);
                const glm::vec4 end(
                    glm::vec3(points[segment + 1u]),
                    1.0f);
                const uint32_t base =
                    static_cast<uint32_t>(
                        entity.vertices.size());
                entity.vertices.push_back(
                    {start, end, {0.0f, 0.0f, 0.0f, -0.43f}});
                entity.vertices.push_back(
                    {start, end, {0.0f, 0.0f, 0.0f, 0.43f}});
                entity.vertices.push_back(
                    {start, end, {0.0f, 0.0f, 1.0f, 0.43f}});
                entity.vertices.push_back(
                    {start, end, {0.0f, 0.0f, 1.0f, -0.43f}});
                entity.indices.insert(
                    entity.indices.end(),
                    {
                        base,
                        base + 1u,
                        base + 2u,
                        base,
                        base + 2u,
                        base + 3u,
                    });
            }
            const float lineChannel =
                nurbsSrgbToLinear(0x33u / 255.0f);
            entity.localTransform = localTransform;
            entity.instanceData.reserved =
                glm::vec4(0.0f);
            entity.materialData.colorAndOpacity =
                glm::vec4(
                    lineChannel,
                    lineChannel,
                    lineChannel,
                    opacity);
            entity.materialData.phaseAndReserved =
                glm::vec4(phase, 0.0f, 0.0f, 0.0f);
            entities.push_back(eastl::move(entity));
        }

        /** Packs one complete explicit mip chain into texture upload storage. */
        void buildNurbsTextureUpload(
            const std::filesystem::path &assetPath,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &mipOffsets)
        {
            const eastl::vector<RgbaImageData> mips =
                buildSrgbMipChain(
                    decodeJpegRgba8(assetPath));
            bytes.clear();
            mipOffsets.clear();
            for (const RgbaImageData &mip : mips)
            {
                mipOffsets.push_back(bytes.size());
                bytes.insert(
                    bytes.end(),
                    mip.pixels.begin(),
                    mip.pixels.end());
            }
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendNurbsBufferPayload(
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

        /** Allocates one NURBS entity through existing RenderSet semantics. */
        void allocateNurbsEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglGeometryNurbsEntityData &entity,
            uint32_t textureWidth,
            uint32_t textureHeight,
            const eastl::vector<uint8_t> &textureBytes,
            const eastl::vector<uint64_t> &textureMipOffsets)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            const eastl::string vertexBufferName =
                entity.logicalId + "-vertices";
            const eastl::string indexBufferName =
                entity.logicalId + "-indices";
            const eastl::string objectBufferName =
                entity.logicalId + "-object";
            const eastl::string instanceBufferName =
                entity.logicalId + "-instance";
            const eastl::string materialBufferName =
                entity.logicalId + "-material";
            allocation.verticesCount =
                static_cast<uint32_t>(
                    entity.vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(
                    entity.indices.size());
            allocation.instanceCount = 1u;
            appendNurbsBufferPayload(
                allocation,
                WebglGeometryNurbsSceneRenderSetComponents::
                    vertices,
                vertexBufferName.c_str(),
                entity.vertices.data(),
                entity.vertices.size() *
                    sizeof(WebglGeometryNurbsHostVertex),
                1u);
            appendNurbsBufferPayload(
                allocation,
                WebglGeometryNurbsSceneRenderSetComponents::
                    indices,
                indexBufferName.c_str(),
                entity.indices.data(),
                entity.indices.size() *
                    sizeof(uint32_t),
                1u);
            appendNurbsBufferPayload(
                allocation,
                WebglGeometryNurbsSceneRenderSetComponents::
                    objects,
                objectBufferName.c_str(),
                &entity.objectData,
                sizeof(entity.objectData),
                1u);
            appendNurbsBufferPayload(
                allocation,
                WebglGeometryNurbsSceneRenderSetComponents::
                    instances,
                instanceBufferName.c_str(),
                &entity.instanceData,
                sizeof(entity.instanceData),
                1u);
            appendNurbsBufferPayload(
                allocation,
                WebglGeometryNurbsSceneRenderSetComponents::
                    materials,
                materialBufferName.c_str(),
                &entity.materialData,
                sizeof(entity.materialData),
                1u);

            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle =
                WebglGeometryNurbsSceneRenderSetComponents::
                    textures;
            textureInfo.textures.push_back({
                .textureName = "WebglGeometryNurbsUvGrid",
                .format =
                    GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = textureWidth,
                .height = textureHeight,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = textureMipOffsets,
            });
            allocation.textureInfos.push_back(
                eastl::move(textureInfo));
            encoder.allocEntity(allocation);
        }
    }

    void WebglGeometryNurbsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateNurbsScenario(options);
        device = inDevice;
        groupRotation = 0.0;
        if (options.scenarioId == "rotated-input")
        {
            constexpr double TargetRotation = 1.6;
            for (uint32_t frame = 0u;
                 frame <= options.targetFrame;
                 ++frame)
            {
                groupRotation +=
                    (TargetRotation - groupRotation) *
                    0.05;
            }
        }

        ThreeCompat::DeterministicRandom random(
            options.randomSeed);
        for (uint32_t randomCall = 0u;
             randomCall < PreCurveRandomCallCount;
             ++randomCall)
        {
            (void)random.nextUint32();
        }
        eastl::vector<ThreeCompat::NurbsControlPoint>
            curveControlPoints;
        curveControlPoints.reserve(20u);
        for (uint32_t index = 0u; index < 20u; ++index)
        {
            curveControlPoints.push_back({
                {
                    nextNurbsRandom(random) * 400.0 - 200.0,
                    nextNurbsRandom(random) * 400.0,
                    nextNurbsRandom(random) * 400.0 - 200.0,
                },
                1.0,
            });
        }
        finalRandomState = random.getState();
        if (finalRandomState != ExpectedCurveRandomState)
        {
            throw std::runtime_error(
                "The NURBS random stream diverged from r185.");
        }

        ThreeCompat::NurbsAxis curveAxis;
        curveAxis.degree = 3u;
        curveAxis.knots = {0.0, 0.0, 0.0, 0.0};
        for (uint32_t index = 0u; index < 20u; ++index)
        {
            curveAxis.knots.push_back(
                std::clamp(
                    double(index + 1u) / 17.0,
                    0.0,
                    1.0));
        }
        eastl::vector<glm::dvec3> curvePoints;
        curvePoints.reserve(201u);
        for (uint32_t index = 0u; index <= 200u; ++index)
        {
            curvePoints.push_back(
                ThreeCompat::evaluateNurbsCurve(
                    curveAxis,
                    curveControlPoints,
                    double(index) / 200.0));
        }

        entities.clear();
        entities.reserve(ExpectedEntityCount);
        const glm::mat4 curveTransform =
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, -100.0f, 0.0f));
        appendNurbsLineEntity(
            entities,
            "nurbs-curve",
            curvePoints,
            curveTransform,
            1.0f,
            1.0f);
        eastl::vector<glm::dvec3> controlPositions;
        controlPositions.reserve(
            curveControlPoints.size());
        for (const auto &point : curveControlPoints)
        {
            controlPositions.push_back(point.position);
        }
        appendNurbsLineEntity(
            entities,
            "nurbs-control-line",
            controlPositions,
            curveTransform,
            0.25f,
            2.0f);

        const ThreeCompat::NurbsAxis uAxis = {
            .degree = 2u,
            .knots = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0},
        };
        const ThreeCompat::NurbsAxis vAxis = {
            .degree = 3u,
            .knots = {
                0.0, 0.0, 0.0, 0.0,
                1.0, 1.0, 1.0, 1.0,
            },
        };
        const eastl::vector<ThreeCompat::NurbsControlPoint>
            surfaceControlPoints = {
                {{-200.0, -200.0, 100.0}, 1.0},
                {{-200.0, -100.0, -200.0}, 1.0},
                {{-200.0, 100.0, 250.0}, 1.0},
                {{-200.0, 200.0, -100.0}, 1.0},
                {{0.0, -200.0, 0.0}, 1.0},
                {{0.0, -100.0, -100.0}, 5.0},
                {{0.0, 100.0, 150.0}, 5.0},
                {{0.0, 200.0, 0.0}, 1.0},
                {{200.0, -200.0, -100.0}, 1.0},
                {{200.0, -100.0, 200.0}, 1.0},
                {{200.0, 100.0, -250.0}, 1.0},
                {{200.0, 200.0, 100.0}, 1.0},
            };
        const NurbsSurfaceEvaluationContext surfaceContext = {
            .uAxis = &uAxis,
            .vAxis = &vAxis,
            .controlPoints = &surfaceControlPoints,
            .vControlPointCount = 4u,
        };
        appendNurbsSurfaceEntity(
            entities,
            "nurbs-surface",
            ThreeCompat::buildParametricMesh(
                evaluateNurbsSurfacePoint,
                &surfaceContext,
                20u,
                20u),
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(-400.0f, 100.0f, 0.0f)));

        const ThreeCompat::NurbsAxis wAxis = {
            .degree = 1u,
            .knots = {0.0, 0.0, 1.0, 1.0},
        };
        const eastl::vector<ThreeCompat::NurbsControlPoint>
            volumeControlPoints = {
                {{-200.0, -200.0, -200.0}, 1.0},
                {{-200.0, -200.0, 200.0}, 1.0},
                {{-200.0, -100.0, -200.0}, 1.0},
                {{-200.0, -100.0, 200.0}, 1.0},
                {{-200.0, 100.0, -200.0}, 1.0},
                {{-200.0, 100.0, 200.0}, 1.0},
                {{-200.0, 200.0, -200.0}, 1.0},
                {{-200.0, 200.0, 200.0}, 1.0},
                {{0.0, -200.0, -200.0}, 1.0},
                {{0.0, -200.0, 200.0}, 1.0},
                {{0.0, -100.0, -200.0}, 1.0},
                {{0.0, -100.0, 200.0}, 1.0},
                {{0.0, 100.0, -200.0}, 1.0},
                {{0.0, 100.0, 200.0}, 1.0},
                {{0.0, 200.0, -200.0}, 1.0},
                {{0.0, 200.0, 200.0}, 1.0},
                {{200.0, -200.0, -200.0}, 1.0},
                {{200.0, -200.0, 200.0}, 1.0},
                {{200.0, -100.0, 0.0}, 1.0},
                {{200.0, -100.0, 100.0}, 1.0},
                {{200.0, 100.0, 0.0}, 1.0},
                {{200.0, 100.0, 100.0}, 1.0},
                {{200.0, 200.0, 0.0}, 1.0},
                {{200.0, 200.0, 100.0}, 1.0},
            };
        const glm::mat4 volumeTransform =
            glm::scale(
                glm::translate(
                    glm::mat4(1.0f),
                    glm::vec3(400.0f, 100.0f, 0.0f)),
                glm::vec3(0.5f));
        const NurbsVolumeEvaluationContext volumeContexts[5u] = {
            {&uAxis, &vAxis, &wAxis, &volumeControlPoints,
             4u, 2u, NurbsVolumeSurfaceMode::FixedW, 0.0},
            {&uAxis, &vAxis, &wAxis, &volumeControlPoints,
             4u, 2u, NurbsVolumeSurfaceMode::FixedW, 0.5},
            {&uAxis, &vAxis, &wAxis, &volumeControlPoints,
             4u, 2u, NurbsVolumeSurfaceMode::FixedW, 1.0},
            {&uAxis, &vAxis, &wAxis, &volumeControlPoints,
             4u, 2u, NurbsVolumeSurfaceMode::FixedV, 1.0},
            {&uAxis, &vAxis, &wAxis, &volumeControlPoints,
             4u, 2u, NurbsVolumeSurfaceMode::FixedU, 0.0},
        };
        const char *volumeNames[5u] = {
            "volume-front",
            "volume-middle",
            "volume-back",
            "volume-top",
            "volume-side",
        };
        for (uint32_t surface = 0u;
             surface < 5u;
             ++surface)
        {
            appendNurbsSurfaceEntity(
                entities,
                volumeNames[surface],
                ThreeCompat::buildParametricMesh(
                    evaluateNurbsVolumeSurfacePoint,
                    &volumeContexts[surface],
                    20u,
                    20u),
                volumeTransform);
        }
        if (entities.size() != ExpectedEntityCount)
        {
            throw std::logic_error(
                "The NURBS Scene must contain exactly eight entities.");
        }

        const glm::mat4 projection =
            makeNurbsProjection();
        const glm::mat4 view =
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, -150.0f, -750.0f));
        const glm::mat4 group =
            glm::rotate(
                glm::translate(
                    glm::mat4(1.0f),
                    glm::vec3(0.0f, 50.0f, 0.0f)),
                float(groupRotation),
                glm::vec3(0.0f, 1.0f, 0.0f));
        for (auto &entity : entities)
        {
            entity.objectData.modelView =
                view * group * entity.localTransform;
            entity.objectData.projection =
                projection;
            entity.objectData.viewport =
                glm::vec4(400.0f, 250.0f, 0.0f, 0.0f);
            entity.objectData.ambientAndDirectionalIntensity =
                glm::vec4(1.0f, 3.0f, 0.0f, 0.0f);
            entity.objectData.directionalView =
                glm::vec4(
                    glm::normalize(
                        glm::vec3(1.0f)),
                    0.0f);
        }

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "uv_grid_opengl.jpg";
        const RgbaImageData baseTexture =
            decodeJpegRgba8(texturePath);
        const uint32_t textureWidth =
            baseTexture.width;
        const uint32_t textureHeight =
            baseTexture.height;
        buildNurbsTextureUpload(
            texturePath,
            textureBytes,
            textureMipOffsets);

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "The NURBS example could not create its Scene Set encoder.");
        }
        for (const auto &entity : entities)
        {
            allocateNurbsEntity(
                *encoder,
                entity,
                textureWidth,
                textureHeight,
                textureBytes,
                textureMipOffsets);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglGeometryNurbsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryNurbsRuntimeAdapter::afterFrame(
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
                "The NURBS RGBA8 capture exceeds host storage.");
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
            prepareNurbsOutputPath(outputPath);
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
                    "Could not write the NURBS RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareNurbsOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_geometry_nurbs\",\n"
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
                << "  \"randomSeed\":"
                << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":"
                << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":"
                << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"";
            if (options.scenarioId == "rotated-input")
            {
                output
                    << ",\n  \"inputReplay\":{"
                    << "\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_geometry_nurbs\","
                    << "\"scenarioId\":\"rotated-input\","
                    << "\"captureFrame\":61,"
                    << "\"sha256\":\"6e74c6cf8c06cd72bf31167e7288e1fb1e082d862e252d94221b974fc1e556af\","
                    << "\"target\":\"body > div:nth-of-type(2) > canvas\","
                    << "\"eventCount\":3}";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareNurbsOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            uint64_t vertexCount = 0u;
            uint64_t indexCount = 0u;
            for (const auto &entity : entities)
            {
                vertexCount += entity.vertices.size();
                indexCount += entity.indices.size();
            }
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_geometry_nurbs\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":8,\n"
                << "  \"entityCount\":8,\n"
                << "  \"instanceCount\":8,\n"
                << "  \"vertexCount\":" << vertexCount << ",\n"
                << "  \"indexCount\":" << indexCount << ",\n"
                << "  \"scenePassCount\":2,\n"
                << "  \"screenPassCount\":0,\n"
                << "  \"drawCommandCount\":2,\n"
                << "  \"finalRandomState\":"
                << finalRandomState << ",\n"
                << "  \"groupRotation\":"
                << groupRotation << ",\n"
                << "  \"renderSetType\":\"WebglGeometryNurbsSceneRenderSet\",\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene-set\",\n"
                << "    \"renderSetType\":\"WebglGeometryNurbsSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":8,\n"
                << "    \"entityCount\":8,\n"
                << "    \"entities\":[";
            for (size_t entityIndex = 0u;
                 entityIndex < entities.size();
                 ++entityIndex)
            {
                if (entityIndex > 0u)
                {
                    output << ',';
                }
                output
                    << "{\"entityId\":" << entityIndex
                    << ",\"logicalRenderableId\":\""
                    << entities[entityIndex].logicalId.c_str()
                    << "\",\"instanceCount\":1}";
            }
            output
                << "],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "    \"drawCommandCount\":2,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":["
                << "{\"name\":\"opaque\",\"renderClass\":\"WebglGeometryNurbsOpaquePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"transparent-control-line\",\"renderClass\":\"WebglGeometryNurbsTransparentControlLinePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
                << "  \"scenePassSequence\":["
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque\",\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-control-line\",\"entityOrdinal\":0}],\n"
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
            prepareNurbsOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_geometry_nurbs\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n"
                << "  \"curveControlPointCount\":20,\n"
                << "  \"curveSampleCount\":201,\n"
                << "  \"surfaceGrid\":\"21x21\",\n"
                << "  \"volumeSurfaceCount\":5,\n"
                << "  \"entityCount\":8,\n"
                << "  \"finalRandomState\":"
                << finalRandomState << "\n"
                << "}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryNurbsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
    }
}
