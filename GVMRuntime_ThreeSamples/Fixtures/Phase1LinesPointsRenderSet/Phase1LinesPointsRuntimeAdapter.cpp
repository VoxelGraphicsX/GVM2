#include "Phase1LinesPointsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>

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
        constexpr double Pi = 3.14159265358979323846;
        constexpr double FrameStepSeconds = 1.0 / 60.0;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Stores one recursive Hilbert corner permutation. */
        struct HilbertCornerOrder
        {
            uint32_t values[8u];
        };

        static_assert(sizeof(Phase1LineHostFloat4) == 16u);
        static_assert(sizeof(Phase1LineHostVertex) == 80u);
        static_assert(sizeof(Phase1LineHostObjectData) == 144u);
        static_assert(sizeof(Phase1LineHostInstanceData) == 16u);
        static_assert(sizeof(Phase1LineHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Converts one display-sRGB channel into Three's linear working space. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one packed sRGB color into Three's linear working space. */
        glm::vec3 packedSrgbToLinear(uint32_t color)
        {
            return glm::vec3(
                srgbToLinear(float((color >> 16u) & 0xffu) / 255.0f),
                srgbToLinear(float((color >> 8u) & 0xffu) / 255.0f),
                srgbToLinear(float(color & 0xffu) / 255.0f));
        }

        /** Evaluates standard display-space HSL before conversion to linear working space. */
        glm::vec3 hslToLinear(double hue, double saturation, double lightness)
        {
            hue -= std::floor(hue);
            saturation = std::clamp(saturation, 0.0, 1.0);
            lightness = std::clamp(lightness, 0.0, 1.0);
            const double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
            const double sector = hue * 6.0;
            const double intermediate = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            if (sector < 1.0)
            {
                red = chroma;
                green = intermediate;
            }
            else if (sector < 2.0)
            {
                red = intermediate;
                green = chroma;
            }
            else if (sector < 3.0)
            {
                green = chroma;
                blue = intermediate;
            }
            else if (sector < 4.0)
            {
                green = intermediate;
                blue = chroma;
            }
            else if (sector < 5.0)
            {
                red = intermediate;
                blue = chroma;
            }
            else
            {
                red = chroma;
                blue = intermediate;
            }
            const double match = lightness - chroma * 0.5;
            return glm::vec3(
                srgbToLinear(static_cast<float>(red + match)),
                srgbToLinear(static_cast<float>(green + match)),
                srgbToLinear(static_cast<float>(blue + match)));
        }

        /** Recursively appends Three r185's exact three-dimensional Hilbert corner sequence. */
        void appendHilbert3D(eastl::vector<glm::vec3> &points,
                             const glm::vec3 &center,
                             float size,
                             int32_t iterations,
                             const HilbertCornerOrder &order)
        {
            const float half = size * 0.5f;
            const glm::vec3 corners[8u] = {
                center + glm::vec3(-half, half, -half),
                center + glm::vec3(-half, half, half),
                center + glm::vec3(-half, -half, half),
                center + glm::vec3(-half, -half, -half),
                center + glm::vec3(half, -half, -half),
                center + glm::vec3(half, -half, half),
                center + glm::vec3(half, half, half),
                center + glm::vec3(half, half, -half),
            };
            glm::vec3 selected[8u];
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                selected[index] = corners[order.values[index]];
            }
            if (--iterations >= 0)
            {
                const HilbertCornerOrder childOrders[8u] = {
                    {{order.values[0u], order.values[3u], order.values[4u], order.values[7u], order.values[6u], order.values[5u], order.values[2u], order.values[1u]}},
                    {{order.values[0u], order.values[7u], order.values[6u], order.values[1u], order.values[2u], order.values[5u], order.values[4u], order.values[3u]}},
                    {{order.values[0u], order.values[7u], order.values[6u], order.values[1u], order.values[2u], order.values[5u], order.values[4u], order.values[3u]}},
                    {{order.values[2u], order.values[3u], order.values[0u], order.values[1u], order.values[6u], order.values[7u], order.values[4u], order.values[5u]}},
                    {{order.values[2u], order.values[3u], order.values[0u], order.values[1u], order.values[6u], order.values[7u], order.values[4u], order.values[5u]}},
                    {{order.values[4u], order.values[3u], order.values[2u], order.values[5u], order.values[6u], order.values[1u], order.values[0u], order.values[7u]}},
                    {{order.values[4u], order.values[3u], order.values[2u], order.values[5u], order.values[6u], order.values[1u], order.values[0u], order.values[7u]}},
                    {{order.values[6u], order.values[5u], order.values[2u], order.values[1u], order.values[0u], order.values[3u], order.values[4u], order.values[7u]}},
                };
                for (uint32_t index = 0u; index < 8u; ++index)
                {
                    appendHilbert3D(points, selected[index], half, iterations, childOrders[index]);
                }
                return;
            }
            points.insert(points.end(), selected, selected + 8u);
        }

        /** Returns the default r185 64-point Hilbert curve for one requested size. */
        eastl::vector<glm::vec3> buildHilbert3D(float size)
        {
            eastl::vector<glm::vec3> points;
            points.reserve(64u);
            appendHilbert3D(points, glm::vec3(0.0f), size, 1, {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}});
            return points;
        }

        /** Evaluates one r185 centripetal Catmull-Rom point on an open curve. */
        glm::vec3 evaluateCatmullRom(const eastl::vector<glm::vec3> &points, double normalizedPosition)
        {
            const double scaledPoint = double(points.size() - 1u) * normalizedPosition;
            int32_t pointIndex = static_cast<int32_t>(std::floor(scaledPoint));
            double weight = scaledPoint - double(pointIndex);
            if (weight == 0.0 && pointIndex == static_cast<int32_t>(points.size() - 1u))
            {
                pointIndex = static_cast<int32_t>(points.size() - 2u);
                weight = 1.0;
            }
            const glm::dvec3 p1(points[static_cast<size_t>(pointIndex)]);
            const glm::dvec3 p2(points[static_cast<size_t>(pointIndex + 1)]);
            const glm::dvec3 p0 = pointIndex > 0 ? glm::dvec3(points[static_cast<size_t>(pointIndex - 1)]) : p1 * 2.0 - p2;
            const glm::dvec3 p3 = pointIndex + 2 < static_cast<int32_t>(points.size()) ? glm::dvec3(points[static_cast<size_t>(pointIndex + 2)]) : p2 * 2.0 - p1;
            double dt0 = std::pow(glm::dot(p0 - p1, p0 - p1), 0.25);
            double dt1 = std::pow(glm::dot(p1 - p2, p1 - p2), 0.25);
            double dt2 = std::pow(glm::dot(p2 - p3, p2 - p3), 0.25);
            if (dt1 < 1.0e-4)
            {
                dt1 = 1.0;
            }
            if (dt0 < 1.0e-4)
            {
                dt0 = dt1;
            }
            if (dt2 < 1.0e-4)
            {
                dt2 = dt1;
            }
            glm::dvec3 tangent1 = (p1 - p0) / dt0 - (p2 - p0) / (dt0 + dt1) + (p2 - p1) / dt1;
            glm::dvec3 tangent2 = (p2 - p1) / dt1 - (p3 - p1) / (dt1 + dt2) + (p3 - p2) / dt2;
            tangent1 *= dt1;
            tangent2 *= dt1;
            const glm::dvec3 coefficient2 = -3.0 * p1 + 3.0 * p2 - 2.0 * tangent1 - tangent2;
            const glm::dvec3 coefficient3 = 2.0 * p1 - 2.0 * p2 + tangent1 + tangent2;
            return glm::vec3(((coefficient3 * weight + coefficient2) * weight + tangent1) * weight + p1);
        }

        /** Samples one open centripetal Catmull-Rom curve with optional endpoint inclusion. */
        eastl::vector<glm::vec3> sampleCatmullRom(const eastl::vector<glm::vec3> &controlPoints,
                                                  uint32_t divisions,
                                                  bool includeEndpoint)
        {
            const uint32_t sampleCount = includeEndpoint ? divisions + 1u : divisions;
            eastl::vector<glm::vec3> samples;
            samples.reserve(sampleCount);
            for (uint32_t index = 0u; index < sampleCount; ++index)
            {
                samples.push_back(evaluateCatmullRom(controlPoints, double(index) / double(divisions)));
            }
            return samples;
        }

        /** Appends one native line or one triangle-list quad carrying segment interpolation data. */
        void appendExpandedSegment(Phase1LineEntityState &entity,
                                   const Phase1LinePoint &start,
                                   const Phase1LinePoint &end,
                                   float startDistance,
                                   float endDistance,
                                   bool triangleExpansion)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(entity.vertices.size());
            const Phase1LineHostFloat4 startPosition = {start.position.x, start.position.y, start.position.z, 1.0f};
            const Phase1LineHostFloat4 endPosition = {end.position.x, end.position.y, end.position.z, 1.0f};
            const Phase1LineHostFloat4 startColor = {start.linearColor.r, start.linearColor.g, start.linearColor.b, 1.0f};
            const Phase1LineHostFloat4 endColor = {end.linearColor.r, end.linearColor.g, end.linearColor.b, 1.0f};
            if (triangleExpansion)
            {
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {0.0f, -0.9f, startDistance, endDistance}});
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {0.0f, 0.9f, startDistance, endDistance}});
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {1.0f, 0.9f, startDistance, endDistance}});
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {1.0f, -0.9f, startDistance, endDistance}});
                const uint32_t segmentIndices[6u] = {
                    baseVertex, baseVertex + 1u, baseVertex + 2u,
                    baseVertex, baseVertex + 2u, baseVertex + 3u};
                entity.indices.insert(entity.indices.end(), segmentIndices, segmentIndices + 6u);
            }
            else
            {
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {0.0f, 0.0f, startDistance, endDistance}});
                entity.vertices.push_back({startPosition, endPosition, startColor, endColor, {1.0f, 0.0f, startDistance, endDistance}});
                const uint32_t segmentIndices[2u] = {baseVertex, baseVertex + 1u};
                entity.indices.insert(entity.indices.end(), segmentIndices, segmentIndices + 2u);
            }
        }

        /** Expands one connected polyline while computing cumulative line distance. */
        void expandLineStrip(Phase1LineEntityState &entity,
                             const eastl::vector<Phase1LinePoint> &points,
                             bool triangleExpansion = false)
        {
            float distance = 0.0f;
            for (size_t index = 0u; index + 1u < points.size(); ++index)
            {
                const glm::dvec3 delta = glm::dvec3(points[index + 1u].position) - glm::dvec3(points[index].position);
                const float nextDistance = distance + static_cast<float>(glm::length(delta));
                appendExpandedSegment(entity, points[index], points[index + 1u],
                                      distance, nextDistance,
                                      triangleExpansion);
                distance = nextDistance;
            }
        }

        /** Expands independent line-segment pairs using Three LineSegments' cumulative distance convention. */
        void expandLineSegments(Phase1LineEntityState &entity,
                                const eastl::vector<Phase1LinePoint> &points,
                                bool triangleExpansion = false)
        {
            float distance = 0.0f;
            for (size_t index = 0u; index + 1u < points.size(); index += 2u)
            {
                const glm::dvec3 delta = glm::dvec3(points[index + 1u].position) - glm::dvec3(points[index].position);
                const float nextDistance = distance + static_cast<float>(glm::length(delta));
                appendExpandedSegment(entity, points[index], points[index + 1u],
                                      distance, nextDistance,
                                      triangleExpansion);
                distance = nextDistance;
            }
        }

        /** Returns a host reflection preserving Three's top-down canvas orientation. */
        glm::mat4 makeHostReflection()
        {
            glm::mat4 reflection(1.0f);
            reflection[1u][1u] = -1.0f;
            return reflection;
        }

        /** Builds Three's intrinsic XYZ Euler quaternion with double-precision angle reduction. */
        glm::mat4 makeThreeEulerXyz(double rotationX, double rotationY, double rotationZ)
        {
            const double halfX = rotationX * 0.5;
            const double halfY = rotationY * 0.5;
            const double halfZ = rotationZ * 0.5;
            const double cosineX = std::cos(halfX);
            const double cosineY = std::cos(halfY);
            const double cosineZ = std::cos(halfZ);
            const double sineX = std::sin(halfX);
            const double sineY = std::sin(halfY);
            const double sineZ = std::sin(halfZ);
            const glm::dquat quaternion(
                cosineX * cosineY * cosineZ - sineX * sineY * sineZ,
                sineX * cosineY * cosineZ + cosineX * sineY * sineZ,
                cosineX * sineY * cosineZ - sineX * cosineY * sineZ,
                cosineX * cosineY * sineZ + sineX * sineY * cosineZ);
            return glm::mat4(glm::toMat4(quaternion));
        }

        /** Builds the Three perspective frustum for the RHI zero-to-one clip-depth contract. */
        glm::mat4 makePerspectiveProjection(double fieldOfViewDegrees, double aspect, double nearDistance, double farDistance)
        {
            const double top = nearDistance * std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = aspect * height;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * nearDistance / width);
            projection[1u][1u] = static_cast<float>(2.0 * nearDistance / height);
            projection[2u][2u] = static_cast<float>(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-farDistance * nearDistance / depth);
            return projection;
        }

        /** Appends one typed host payload to a RenderSet allocation descriptor. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Builds the dashed Hilbert curve and box entities. */
        eastl::vector<Phase1LineEntityState> buildDashedEntities()
        {
            eastl::vector<Phase1LineEntityState> result(2u);
            const glm::vec3 white(1.0f);
            const eastl::vector<glm::vec3> hilbert = buildHilbert3D(25.0f);
            const eastl::vector<glm::vec3> spline = sampleCatmullRom(hilbert, static_cast<uint32_t>(hilbert.size()) * 6u, true);
            eastl::vector<Phase1LinePoint> splinePoints;
            splinePoints.reserve(spline.size());
            for (const glm::vec3 &position : spline)
            {
                splinePoints.push_back({position, white});
            }
            result[0u].logicalId = "hilbert-spline";
            result[0u].storagePrefix = "WebglLinesDashedSpline";
            expandLineStrip(result[0u], splinePoints);
            const glm::vec3 orange = packedSrgbToLinear(0xffaa00u);
            constexpr float half = 25.0f;
            const glm::vec3 boxPositions[24u] = {
                {-half, -half, -half}, {-half, half, -half}, {-half, half, -half}, {half, half, -half},
                {half, half, -half}, {half, -half, -half}, {half, -half, -half}, {-half, -half, -half},
                {-half, -half, half}, {-half, half, half}, {-half, half, half}, {half, half, half},
                {half, half, half}, {half, -half, half}, {half, -half, half}, {-half, -half, half},
                {-half, -half, -half}, {-half, -half, half}, {-half, half, -half}, {-half, half, half},
                {half, half, -half}, {half, half, half}, {half, -half, -half}, {half, -half, half},
            };
            eastl::vector<Phase1LinePoint> boxPoints;
            boxPoints.reserve(24u);
            for (const glm::vec3 &position : boxPositions)
            {
                boxPoints.push_back({position, white});
            }
            result[1u].logicalId = "box-segments";
            result[1u].storagePrefix = "WebglLinesDashedBox";
            expandLineSegments(result[1u], boxPoints);
            result[0u].materialData = {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.5f, 1.0f, 0.0f}, {packedSrgbToLinear(0x111111u).r, packedSrgbToLinear(0x111111u).g, packedSrgbToLinear(0x111111u).b, 1.0f}};
            result[1u].materialData = {{orange.r, orange.g, orange.b, 1.0f}, {3.0f, 1.0f, 1.0f, 0.0f}, {packedSrgbToLinear(0x111111u).r, packedSrgbToLinear(0x111111u).g, packedSrgbToLinear(0x111111u).b, 1.0f}};
            return result;
        }

        /** Builds all six colored Hilbert line entities and their linear vertex colors. */
        eastl::vector<Phase1LineEntityState> buildColorEntities()
        {
            const char *logicalIds[6u] = {
                "hilbert-spline-blue", "hilbert-spline-magenta", "hilbert-spline-rainbow",
                "hilbert-polyline-blue", "hilbert-polyline-green", "hilbert-polyline-rainbow",
            };
            const char *storagePrefixes[6u] = {
                "WebglLinesColorsSplineBlue", "WebglLinesColorsSplineMagenta", "WebglLinesColorsSplineRainbow",
                "WebglLinesColorsPolylineBlue", "WebglLinesColorsPolylineGreen", "WebglLinesColorsPolylineRainbow",
            };
            const eastl::vector<glm::vec3> hilbert = buildHilbert3D(200.0f);
            const uint32_t divisions = static_cast<uint32_t>(hilbert.size()) * 6u;
            const eastl::vector<glm::vec3> spline = sampleCatmullRom(hilbert, divisions, false);
            eastl::vector<Phase1LineEntityState> result(6u);
            for (uint32_t entityIndex = 0u; entityIndex < 6u; ++entityIndex)
            {
                eastl::vector<Phase1LinePoint> points;
                const bool splineEntity = entityIndex < 3u;
                const auto &positions = splineEntity ? spline : hilbert;
                points.reserve(positions.size());
                for (uint32_t index = 0u; index < positions.size(); ++index)
                {
                    const glm::vec3 &position = positions[index];
                    glm::vec3 color(1.0f);
                    if (entityIndex == 0u)
                    {
                        color = hslToLinear(0.6, 1.0, std::max(0.0, -double(position.x) / 200.0) + 0.5);
                    }
                    else if (entityIndex == 1u)
                    {
                        color = hslToLinear(0.9, 1.0, std::max(0.0, -double(position.y) / 200.0) + 0.5);
                    }
                    else if (entityIndex == 2u)
                    {
                        color = hslToLinear(double(index) / double(divisions), 1.0, 0.5);
                    }
                    else if (entityIndex == 3u)
                    {
                        color = hslToLinear(0.6, 1.0, std::max(0.0, (200.0 - double(position.x)) / 400.0) * 0.5 + 0.5);
                    }
                    else if (entityIndex == 4u)
                    {
                        color = hslToLinear(0.3, 1.0, std::max(0.0, (200.0 + double(position.x)) / 400.0) * 0.5);
                    }
                    else
                    {
                        color = hslToLinear(double(index) / double(hilbert.size()), 1.0, 0.5);
                    }
                    points.push_back({position, color});
                }
                result[entityIndex].logicalId = logicalIds[entityIndex];
                result[entityIndex].storagePrefix = storagePrefixes[entityIndex];
                expandLineStrip(result[entityIndex], points);
                result[entityIndex].materialData = {{1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
            }
            return result;
        }

        /** Reads the canonical pointer offsets from one explicit replay file. */
        glm::dvec2 readPointerReplay(const ThreeSampleHostOptions &options)
        {
            if (options.inputReplayPath.empty())
            {
                return glm::dvec2(0.0);
            }
            std::ifstream input(options.inputReplayPath.c_str());
            if (!input)
            {
                throw std::runtime_error("Could not open webgl_lines_colors input replay.");
            }
            nlohmann::json replay;
            input >> replay;
            if (replay.value("caseId", "") != "webgl_lines_colors" || replay.value("scenarioId", "") != "camera-input")
            {
                throw std::runtime_error("webgl_lines_colors replay identity differs from the formal scenario.");
            }
            return glm::dvec2(
                replay.at("canonicalState").at("pointerX").get<double>(),
                replay.at("canonicalState").at("pointerY").get<double>());
        }
    } // namespace

    void Phase1LinesPointsRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                                               GVM::Core::DeviceProxy inDevice,
                                                               const ThreeSampleHostOptions &options)
    {
        dashedCase = options.caseId == "webgl_lines_dashed";
        if (!dashedCase && options.caseId != "webgl_lines_colors")
        {
            throw std::invalid_argument("Phase1LinesPointsRenderSet supports only webgl_lines_dashed and webgl_lines_colors.");
        }
        const bool validDashedScenario = dashedCase &&
            ((options.scenarioId == "initial" && options.targetFrame == 0u) ||
             (options.scenarioId == "animated" && options.targetFrame == 120u));
        const bool validColorScenario = !dashedCase &&
            ((options.scenarioId == "initial" && options.targetFrame == 0u) ||
             (options.scenarioId == "animated" && options.targetFrame == 60u) ||
             (options.scenarioId == "camera-input" && options.targetFrame == 61u));
        if (!validDashedScenario && !validColorScenario)
        {
            throw std::invalid_argument("Line case scenario and target frame differ from the r185 Manifest.");
        }
        if ((options.scenarioId == "camera-input") != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only webgl_lines_colors camera-input may consume an input replay.");
        }
        device = inDevice;
        caseId = options.caseId;
        captureWidth = options.width;
        captureHeight = options.height;
        const glm::dvec2 pointer = readPointerReplay(options);
        scenarioState.pointerX = pointer.x;
        scenarioState.pointerY = pointer.y;
        entities = dashedCase ? buildDashedEntities() : buildColorEntities();
        updateFrameState(0u, captureWidth, captureHeight);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Line examples could not create their Scene RenderSet encoder.");
        }
        for (Phase1LineEntityState &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex Phase1LinesPointsRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const Phase1LineEntityState &entity) const
    {
        if (entity.vertices.empty() || entity.indices.empty() ||
            entity.vertices.size() > std::numeric_limits<uint32_t>::max() ||
            entity.indices.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::out_of_range("Line RenderSet entity geometry has an invalid draw range.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, Phase1LinesSceneRenderSetComponents::vertices, entity.storagePrefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(Phase1LineHostVertex), 1u);
        appendBufferPayload(allocation, Phase1LinesSceneRenderSetComponents::indices, entity.storagePrefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, Phase1LinesSceneRenderSetComponents::objects, entity.storagePrefix + "Object", &entity.objectData, sizeof(entity.objectData), 1u);
        appendBufferPayload(allocation, Phase1LinesSceneRenderSetComponents::instances, entity.storagePrefix + "Instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
        appendBufferPayload(allocation, Phase1LinesSceneRenderSetComponents::materials, entity.storagePrefix + "Material", &entity.materialData, sizeof(entity.materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void Phase1LinesPointsRuntimeAdapter::updateFrameState(uint32_t frameIndex, uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("Line capture dimensions must be positive.");
        }
        const double virtualMilliseconds = double(frameIndex) * FrameStepSeconds * 1000.0;
        scenarioState.timeSeconds = (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.001;
        glm::dvec3 sourceCamera(0.0, 0.0, dashedCase ? 150.0 : 1000.0);
        if (!dashedCase)
        {
            const double decay = std::pow(0.95, double(frameIndex + 1u));
            sourceCamera.x = scenarioState.pointerX * (1.0 - decay);
            sourceCamera.y = (200.0 - scenarioState.pointerY) * (1.0 - decay);
        }
        scenarioState.cameraX = sourceCamera.x;
        scenarioState.cameraY = sourceCamera.y;
        const glm::dvec3 hostCamera(sourceCamera.x, -sourceCamera.y, sourceCamera.z);
        const glm::dmat4 viewDouble = glm::lookAt(hostCamera, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
        const glm::mat4 view(viewDouble);
        const glm::mat4 projection = makePerspectiveProjection(
            dashedCase ? 60.0 : 33.0,
            double(width) / double(height),
            1.0,
            dashedCase ? 200.0 : 10000.0);
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            glm::mat4 sourceModel(1.0f);
            if (dashedCase)
            {
                const double angle =
                    scenarioState.timeSeconds * 0.25;
                sourceModel = makeThreeEulerXyz(angle, angle, 0.0);
                entities[index].objectData.viewportAndFog = {float(width), float(height), 150.0f, 200.0f};
            }
            else
            {
                constexpr float scale = 0.45f;
                constexpr float distance = 225.0f;
                const float x = (float(index % 3u) - 1.0f) * distance;
                const float y = index < 3u ? -distance * 0.5f : distance * 0.5f;
                sourceModel = glm::translate(sourceModel, glm::vec3(x, y, 0.0f));
                const double sourceRotation = (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.0005 *
                    (index % 2u != 0u ? 1.0 : -1.0);
                const double rotationY = std::fmod(sourceRotation, Pi * 2.0);
                sourceModel *= makeThreeEulerXyz(0.0, rotationY, 0.0);
                sourceModel = glm::scale(sourceModel, glm::vec3(scale));
                entities[index].objectData.viewportAndFog = {float(width), float(height), 100000.0f, 100001.0f};
            }
            const glm::mat4 model = makeHostReflection() * sourceModel;
            entities[index].objectData.modelView = view * model;
            entities[index].objectData.modelViewProjection = projection * entities[index].objectData.modelView;
            entities[index].instanceData.translation = {0.0f, 0.0f, 0.0f, 0.0f};
        }
    }

    void Phase1LinesPointsRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                       const ThreeSampleHostOptions &options,
                                                       uint32_t frameIndex)
    {
        (void)options;
        updateFrameState(frameIndex, captureWidth, captureHeight);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Line examples could not create their frame update encoder.");
        }
        for (const Phase1LineEntityState &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex, Phase1LinesSceneRenderSetComponents::objects, &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void Phase1LinesPointsRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
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
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Line RGBA8 capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void Phase1LinesPointsRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                    const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void Phase1LinesPointsRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                            const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open line RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete line RGBA capture.");
        }
    }

    void Phase1LinesPointsRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                                uint32_t frameIndex,
                                                                uint32_t width,
                                                                uint32_t height,
                                                                uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open line capture metadata path.");
        }
        output << "{\n"
               << "  \"caseId\": \"" << caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n";
        if (options.scenarioId == "camera-input")
        {
            output << "  \"inputReplay\": {\"schemaVersion\":1,\"caseId\":\"webgl_lines_colors\",\"scenarioId\":\"camera-input\",\"captureFrame\":61,\"sha256\":\"c8377a4e5b96ad87fa58889cae3c8725133a7e78ccd873d1241ad4588ed93046\",\"target\":\"body > canvas\",\"eventCount\":1}\n";
        }
        else
        {
            output << "  \"inputReplay\": null\n";
        }
        output
               << "}\n";
    }

    void Phase1LinesPointsRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                                   uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open line structural snapshot path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"" << caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": " << entities.size() << ",\n"
               << "  \"entityCount\": " << entities.size() << ",\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"triangleListLineExpansion\": false,\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"timeSeconds\": " << scenarioState.timeSeconds << ",\n"
               << "  \"cameraPosition\": [" << scenarioState.cameraX << ", " << scenarioState.cameraY << ", " << (dashedCase ? 150 : 1000) << "],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 1,\n"
               << "    \"renderSetId\": \"scene\",\n"
               << "    \"renderSetType\": \"Phase1LinesSceneRenderSet\",\n"
               << "    \"renderableObjectCount\": " << entities.size() << ",\n"
               << "    \"entityCount\": " << entities.size() << ",\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"componentSchema\": [{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
               << "    \"scenePasses\": [{\"name\": \"main\", \"renderClass\": \""
               << (dashedCase ? "WebglLinesDashedMainPass" : "WebglLinesColorsMainPass")
               << "\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false}],\n"
               << "    \"entities\": [\n";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            output << "      {\"entityId\": " << entities[index].entityIndex
                   << ", \"logicalRenderableId\": \"" << entities[index].logicalId.c_str()
                   << "\", \"instanceCount\": 1}" << (index + 1u == entities.size() ? "\n" : ",\n");
        }
        output << "    ]\n"
               << "  }]\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
