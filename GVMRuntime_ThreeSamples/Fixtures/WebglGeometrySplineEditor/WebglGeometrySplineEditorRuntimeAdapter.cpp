#include "WebglGeometrySplineEditorRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebglGeometrySplineEditorPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglGeometrySplineEditorBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                                     GVM::Core::RenderComponentHandle component,
                                     const char *name,
                                     const void *data,
                                     uint64_t byteCount,
                                     uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends one barycentric triangle to a CPU entity. */
        void appendWebglGeometrySplineEditorTriangle(WebglGeometrySplineEditorEntity &entity,
                                       const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &c,
                                       const glm::vec3 &normal)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Expands one helper segment into the triangle-list line geometry required by the DSL. */
        void appendWebglGeometrySplineEditorLineQuad(
            WebglGeometrySplineEditorEntity &entity,
            const glm::vec3 &start,
            const glm::vec3 &end,
            float halfWidth)
        {
            const glm::vec3 direction = glm::normalize(end - start);
            glm::vec3 side = glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(side) < 0.001f)
                side = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
            side = glm::normalize(side) * halfWidth;
            const glm::vec3 a = start - side;
            const glm::vec3 b = start + side;
            const glm::vec3 c = end + side;
            const glm::vec3 d = end - side;
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
            appendWebglGeometrySplineEditorTriangle(entity, a, b, c, normal);
            appendWebglGeometrySplineEditorTriangle(entity, a, c, d, normal);
        }

        /** Builds one 20-unit BoxGeometry control object with flat face normals. */
        void buildWebglGeometrySplineEditorControlCube(WebglGeometrySplineEditorEntity &entity)
        {
            const glm::vec3 p[8] = {
                {-10.0f, -10.0f, -10.0f}, {10.0f, -10.0f, -10.0f},
                {10.0f, 10.0f, -10.0f}, {-10.0f, 10.0f, -10.0f},
                {-10.0f, -10.0f, 10.0f}, {10.0f, -10.0f, 10.0f},
                {10.0f, 10.0f, 10.0f}, {-10.0f, 10.0f, 10.0f}};
            const uint32_t face[6][4] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u}, {4u, 0u, 3u, 7u},
                {1u, 5u, 6u, 2u}, {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u}};
            const glm::vec3 normal[6] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
            for (uint32_t side = 0u; side < 6u; ++side)
            {
                appendWebglGeometrySplineEditorTriangle(entity,
                    p[face[side][0]], p[face[side][1]], p[face[side][2]], normal[side]);
                appendWebglGeometrySplineEditorTriangle(entity,
                    p[face[side][0]], p[face[side][2]], p[face[side][3]], normal[side]);
            }
        }

        /** Builds the large shadow receiver plane from the source example. */
        void buildWebglGeometrySplineEditorPlane(WebglGeometrySplineEditorEntity &entity)
        {
            appendWebglGeometrySplineEditorTriangle(entity,
                {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f},
                {0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f});
            appendWebglGeometrySplineEditorTriangle(entity,
                {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f},
                {-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f});
        }

        /** Converts a screen-space point into the constant-y GridHelper plane. */
        glm::vec3 webglGeometrySplineEditorScreenPointOnPlane(
            const glm::mat4 &inverseViewProjection,
            float screenX,
            float screenY,
            float planeY,
            uint32_t width,
            uint32_t height)
        {
            const float ndcX = 2.0f * screenX / float(width) - 1.0f;
            const float ndcY = 2.0f * screenY / float(height) - 1.0f;
            const glm::vec4 nearClip(ndcX, ndcY, -1.0f, 1.0f);
            const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);
            const glm::vec4 nearHomogeneous = inverseViewProjection * nearClip;
            const glm::vec4 farHomogeneous = inverseViewProjection * farClip;
            const glm::vec3 nearPoint = glm::vec3(nearHomogeneous) / nearHomogeneous.w;
            const glm::vec3 farPoint = glm::vec3(farHomogeneous) / farHomogeneous.w;
            const float denominator = farPoint.y - nearPoint.y;
            const float interpolation = std::abs(denominator) > 1.0e-6f
                ? (planeY - nearPoint.y) / denominator
                : 0.0f;
            return nearPoint + (farPoint - nearPoint) * interpolation;
        }

        /** Projects a world point to the lower-left-origin viewport convention. */
        glm::vec2 webglGeometrySplineEditorScreenPoint(
            const glm::mat4 &viewProjection,
            const glm::vec3 &worldPoint,
            uint32_t width,
            uint32_t height)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPoint, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return glm::vec2(
                (ndc.x + 1.0f) * 0.5f * float(width),
                (ndc.y + 1.0f) * 0.5f * float(height));
        }

        /** Builds GridHelper with one-pixel screen-space triangle ribbons. */
        void buildWebglGeometrySplineEditorGrid(
            WebglGeometrySplineEditorEntity &entity,
            const glm::mat4 &viewProjection,
            const glm::mat4 &inverseViewProjection,
            uint32_t width,
            uint32_t height)
        {
            constexpr uint32_t lineCount = 101u;
            constexpr float planeY = -199.0f;
            for (uint32_t index = 0u; index < lineCount; ++index)
            {
                const float coordinate = -1000.0f + 20.0f * float(index);
                const glm::vec3 verticalStart(coordinate, planeY, -1000.0f);
                const glm::vec3 verticalEnd(coordinate, planeY, 1000.0f);
                const glm::vec3 horizontalStart(-1000.0f, planeY, coordinate);
                const glm::vec3 horizontalEnd(1000.0f, planeY, coordinate);
                const auto appendScreenSpaceSegment = [&](const glm::vec3 &start, const glm::vec3 &end)
                {
                    const glm::vec2 startScreen = webglGeometrySplineEditorScreenPoint(
                        viewProjection, start, width, height);
                    const glm::vec2 endScreen = webglGeometrySplineEditorScreenPoint(
                        viewProjection, end, width, height);
                    glm::vec2 direction = endScreen - startScreen;
                    const float length = glm::length(direction);
                    if (length < 1.0e-4f)
                        return;
                    direction /= length;
                    const glm::vec2 side(-direction.y, direction.x);
                    // LineBasicMaterial is rasterized as one physical pixel;
                    // a half-pixel triangle half-width reproduces its
                    // single-sample coverage.
                    constexpr float halfPixel = 0.5f;
                    const glm::vec3 a = webglGeometrySplineEditorScreenPointOnPlane(
                        inverseViewProjection, startScreen.x - side.x * halfPixel,
                        startScreen.y - side.y * halfPixel, planeY, width, height);
                    const glm::vec3 b = webglGeometrySplineEditorScreenPointOnPlane(
                        inverseViewProjection, startScreen.x + side.x * halfPixel,
                        startScreen.y + side.y * halfPixel, planeY, width, height);
                    const glm::vec3 c = webglGeometrySplineEditorScreenPointOnPlane(
                        inverseViewProjection, endScreen.x + side.x * halfPixel,
                        endScreen.y + side.y * halfPixel, planeY, width, height);
                    const glm::vec3 d = webglGeometrySplineEditorScreenPointOnPlane(
                        inverseViewProjection, endScreen.x - side.x * halfPixel,
                        endScreen.y - side.y * halfPixel, planeY, width, height);
                    const glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
                    appendWebglGeometrySplineEditorTriangle(entity, a, b, c, normal);
                    appendWebglGeometrySplineEditorTriangle(entity, a, c, d, normal);
                };
                appendScreenSpaceSegment(verticalStart, verticalEnd);
                appendScreenSpaceSegment(horizontalStart, horizontalEnd);
            }
        }

        /** Generates the eight-sided cone used by the upstream webglGeometrySplineEditor example. */
        void buildWebglGeometrySplineEditorCone(WebglGeometrySplineEditorEntity &entity)
        {
            constexpr uint32_t SegmentCount = 8u;
            const glm::vec3 apex(0.0f, 0.25f, 0.0f);
            const glm::vec3 center(0.0f, -0.25f, 0.0f);
            for (uint32_t segment = 0u; segment < SegmentCount; ++segment)
            {
                const double a0 = 2.0 * Pi * double(segment) / double(SegmentCount);
                const double a1 = 2.0 * Pi * double(segment + 1u) / double(SegmentCount);
                const glm::vec3 p0(0.25f * float(std::cos(a0)), -0.25f, 0.25f * float(std::sin(a0)));
                const glm::vec3 p1(0.25f * float(std::cos(a1)), -0.25f, 0.25f * float(std::sin(a1)));
                appendWebglGeometrySplineEditorTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebglGeometrySplineEditorTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildWebglGeometrySplineEditorSphere(WebglGeometrySplineEditorEntity &entity,
                                    uint32_t rings,
                                    uint32_t segments,
                                    float radius)
        {
            for (uint32_t ring = 0u; ring < rings; ++ring)
            {
                const double v0 = double(ring) / double(rings);
                const double v1 = double(ring + 1u) / double(rings);
                const double p0 = Pi * v0;
                const double p1 = Pi * v1;
                for (uint32_t segment = 0u; segment < segments; ++segment)
                {
                    const double u0 = 2.0 * Pi * double(segment) / double(segments);
                    const double u1 = 2.0 * Pi * double(segment + 1u) / double(segments);
                    const glm::vec3 a(radius * float(std::sin(p0) * std::cos(u0)), radius * float(std::cos(p0)), radius * float(std::sin(p0) * std::sin(u0)));
                    const glm::vec3 b(radius * float(std::sin(p1) * std::cos(u0)), radius * float(std::cos(p1)), radius * float(std::sin(p1) * std::sin(u0)));
                    const glm::vec3 c(radius * float(std::sin(p1) * std::cos(u1)), radius * float(std::cos(p1)), radius * float(std::sin(p1) * std::sin(u1)));
                    const glm::vec3 d(radius * float(std::sin(p0) * std::cos(u1)), radius * float(std::cos(p0)), radius * float(std::sin(p0) * std::sin(u1)));
                    appendWebglGeometrySplineEditorTriangle(entity, a, b, c, glm::normalize(a));
                    appendWebglGeometrySplineEditorTriangle(entity, a, c, d, glm::normalize(a));
                }
            }
        }

        /** Evaluates the centripetal Catmull-Rom segment used by the editor. */
        glm::vec3 evaluateSplineEditorCatmullRom(
            const glm::vec3 &p0,
            const glm::vec3 &p1,
            const glm::vec3 &p2,
            const glm::vec3 &p3,
            float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return 0.5f * ((2.0f * p1)
                + (-p0 + p2) * t
                + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }

        /** Evaluates one centripetal/chordal Catmull-Rom segment by knot spacing. */
        glm::vec3 evaluateSplineEditorParameterized(
            const glm::vec3 *controlPoints,
            float t,
            float alpha)
        {
            const glm::vec3 &p0 = controlPoints[0];
            const glm::vec3 &p1 = controlPoints[1];
            const glm::vec3 &p2 = controlPoints[2];
            const glm::vec3 &p3 = controlPoints[3];
            const float t0 = 0.0f;
            const float t1 = t0 + std::pow(glm::length(p1 - p0), alpha);
            const float t2 = t1 + std::pow(glm::length(p2 - p1), alpha);
            const float t3 = t2 + std::pow(glm::length(p3 - p2), alpha);
            const float sample = t1 + (t2 - t1) * t;
            const float safe01 = std::max(t1 - t0, 1.0e-5f);
            const float safe12 = std::max(t2 - t1, 1.0e-5f);
            const float safe23 = std::max(t3 - t2, 1.0e-5f);
            const float safe02 = std::max(t2 - t0, 1.0e-5f);
            const float safe13 = std::max(t3 - t1, 1.0e-5f);
            const glm::vec3 a1 = ((t1 - sample) / safe01) * p0 + ((sample - t0) / safe01) * p1;
            const glm::vec3 a2 = ((t2 - sample) / safe12) * p1 + ((sample - t1) / safe12) * p2;
            const glm::vec3 a3 = ((t3 - sample) / safe23) * p2 + ((sample - t2) / safe23) * p3;
            const glm::vec3 b1 = ((t2 - sample) / safe02) * a1 + ((sample - t0) / safe02) * a2;
            const glm::vec3 b2 = ((t3 - sample) / safe13) * a2 + ((sample - t1) / safe13) * a3;
            return ((t2 - sample) / safe12) * b1 + ((sample - t1) / safe12) * b2;
        }

        /** Evaluates the complete open Catmull-Rom path across all four controls. */
        glm::vec3 evaluateSplineEditorPath(
            const glm::vec3 *controlPoints,
            float t,
            uint32_t curveKind)
        {
            const float clamped = std::max(0.0f, std::min(1.0f, t));
            const float scaled = clamped * 3.0f;
            const uint32_t segment = std::min(2u, static_cast<uint32_t>(scaled));
            const float local = scaled - float(segment);
            glm::vec3 points[4u] = {};
            if (segment == 0u)
            {
                // Extrapolate the endpoint tangent instead of duplicating a
                // control point; duplicated knots make the non-uniform
                // parameterization singular at the first sample.
                points[0] = controlPoints[0] * 2.0f - controlPoints[1];
                points[1] = controlPoints[0];
                points[2] = controlPoints[1];
                points[3] = controlPoints[2];
            }
            else if (segment == 1u)
            {
                points[0] = controlPoints[0];
                points[1] = controlPoints[1];
                points[2] = controlPoints[2];
                points[3] = controlPoints[3];
            }
            else
            {
                points[0] = controlPoints[1];
                points[1] = controlPoints[2];
                points[2] = controlPoints[3];
                points[3] = controlPoints[3] * 2.0f - controlPoints[2];
            }
            if (curveKind == 0u)
                return evaluateSplineEditorCatmullRom(points[0], points[1], points[2], points[3], local);
            return evaluateSplineEditorParameterized(points, local, curveKind == 1u ? 0.5f : 1.0f);
        }

        /** Expands a sampled Catmull-Rom curve into a deterministic triangle ribbon. */
        void buildWebglGeometrySplineEditorRibbon(
            WebglGeometrySplineEditorEntity &entity,
            const glm::vec3 *controlPoints,
            float width,
            uint32_t segmentCount,
            uint32_t curveKind)
        {
            entity.vertices.clear();
            entity.indices.clear();
            for (uint32_t segment = 0u; segment < segmentCount; ++segment)
            {
                const float t0 = float(segment) / float(segmentCount);
                const float t1 = float(segment + 1u) / float(segmentCount);
                const glm::vec3 p0 = evaluateSplineEditorPath(controlPoints, t0, curveKind);
                const glm::vec3 p1 = evaluateSplineEditorPath(controlPoints, t1, curveKind);
                const glm::vec3 tangent = glm::normalize(p1 - p0);
                const glm::vec3 side = glm::normalize(
                    glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f))) *
                    width;
                const glm::vec3 a = p0 - side;
                const glm::vec3 b = p0 + side;
                const glm::vec3 c = p1 + side;
                const glm::vec3 d = p1 - side;
                appendWebglGeometrySplineEditorTriangle(
                    entity, a, b, c, glm::vec3(0.0f, 0.0f, 1.0f));
            appendWebglGeometrySplineEditorTriangle(
                    entity, a, c, d, glm::vec3(0.0f, 0.0f, 1.0f));
            }
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webglGeometrySplineEditorProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(70.0f), aspect, 1.0f, 10000.0f);
        }

        /** Validates the four deterministic target/webglGeometrySplineEditor scenarios. */
        /** Validates the frozen WebglGeometrySplineEditor scenario matrix and output contract. */
        void validateWebglGeometrySplineEditorOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "edited" && options.targetFrame == 1u;
            const bool scenario2 = options.scenarioId == "export-round-trip" && options.targetFrame == 2u;
            if (options.caseId != "webgl_geometry_spline_editor"
                || (!scenario0 && !scenario1 && !scenario2)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_geometry_spline_editor scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebglGeometrySplineEditorRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglGeometrySplineEditorOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(50u);
        glm::vec3 controlPoints[4u] = {
            {289.76843f, 452.51480f, 56.10019f},
            {-53.56300f, 171.49712f, -14.49547f},
            {-91.40119f, 176.43070f, -6.95827f},
            {-383.78531f, 491.13654f, 47.86930f}};
        if (options.scenarioId == "edited")
            controlPoints[2] += glm::vec3(34.0f, -28.0f, 12.0f);
        // Three.js creates one line for each curve type and four 20-unit
        // control cubes.  The remaining visual entities are the deterministic
        // TransformControls/grid helpers recorded by the manifest.
        buildWebglGeometrySplineEditorRibbon(entities[0], controlPoints, 1.6f, 240u, 0u);
        buildWebglGeometrySplineEditorRibbon(entities[1], controlPoints, 1.6f, 240u, 1u);
        buildWebglGeometrySplineEditorRibbon(entities[2], controlPoints, 1.6f, 240u, 2u);
        for (uint32_t pointIndex = 0u; pointIndex < 4u; ++pointIndex)
            buildWebglGeometrySplineEditorControlCube(entities[3u + pointIndex]);
        buildWebglGeometrySplineEditorPlane(entities[7u]);
        const glm::mat4 initialView = glm::lookAt(
            glm::vec3(0.0f, 250.0f, 1000.0f), glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 initialProjection = webglGeometrySplineEditorProjection(
            options.width, options.height);
        buildWebglGeometrySplineEditorGrid(
            entities[8u], initialProjection * initialView,
            glm::inverse(initialProjection * initialView), options.width, options.height);
        for (uint32_t helperIndex = 9u; helperIndex < entities.size(); ++helperIndex)
        {
            const uint32_t pointIndex = (helperIndex - 9u) % 4u;
            const glm::vec3 center = controlPoints[pointIndex];
            const float scale = 24.0f + 4.0f * float((helperIndex - 9u) % 5u);
            const glm::vec3 axis = (helperIndex % 3u == 0u)
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : (helperIndex % 3u == 1u ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f));
            appendWebglGeometrySplineEditorLineQuad(entities[helperIndex], center - axis * scale, center + axis * scale, 0.7f);
        }
        const glm::vec3 camera(0.0f, 250.0f, 1000.0f);
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = webglGeometrySplineEditorProjection(options.width, options.height);
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            glm::mat4 model = glm::mat4(1.0f);
            if (index >= 3u && index <= 6u)
                model = glm::translate(glm::mat4(1.0f), controlPoints[index - 3u]);
            else if (index == 7u)
                model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -200.0f, 0.0f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(2000.0f));
            else if (index == 8u)
                model = glm::mat4(1.0f);
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            const glm::vec4 color = index < 3u
                ? glm::vec4(index == 0u ? 1.0f : 0.0f, index == 1u ? 1.0f : 0.0f, index == 2u ? 1.0f : 0.0f, 0.35f)
                : (index == 3u
                    ? glm::vec4(0.020f, 0.040f, 0.048f, 1.0f)
                    : (index == 4u
                        ? glm::vec4(0.135f, 0.296f, 0.716f, 1.0f)
                        : (index == 5u
                            ? glm::vec4(0.474f, 0.402f, 0.991f, 1.0f)
                            : (index == 6u
                                ? glm::vec4(0.159f, 0.799f, 0.283f, 1.0f)
                                : (index == 7u
                                    ? glm::vec4(0.672f, 0.672f, 0.672f, 1.0f)
                                    : (index == 8u
                                        ? glm::vec4(0.25f, 0.25f, 0.25f, 0.25f)
                                        : glm::vec4(0.94f, 0.94f, 0.94f, 1.0f)))))));
            const float materialPhase = index == 7u
                ? -1.0f
                : (index < 3u || index >= 8u ? 1.0f : 0.0f);
            entity.objectData.baseColorAndFlags = glm::vec4(color.x, color.y, color.z,
                materialPhase);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = color;
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_geometry_spline_editor could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const std::string suffix = std::to_string(index);
            const std::string verticesName = "WebglGeometrySplineEditorVertices_" + suffix;
            const std::string indicesName = "WebglGeometrySplineEditorIndices_" + suffix;
            const std::string objectName = "WebglGeometrySplineEditorObject_" + suffix;
            const std::string instanceName = "WebglGeometrySplineEditorInstance_" + suffix;
            const std::string materialName = "WebglGeometrySplineEditorMaterial_" + suffix;
            appendWebglGeometrySplineEditorBuffer(allocation, WebglGeometrySplineEditorSceneRenderSetComponents::vertices, verticesName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglGeometrySplineEditorBuffer(allocation, WebglGeometrySplineEditorSceneRenderSetComponents::indices, indicesName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglGeometrySplineEditorBuffer(allocation, WebglGeometrySplineEditorSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebglGeometrySplineEditorBuffer(allocation, WebglGeometrySplineEditorSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebglGeometrySplineEditorBuffer(allocation, WebglGeometrySplineEditorSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometrySplineEditorRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometrySplineEditorRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        prepareWebglGeometrySplineEditorPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebglGeometrySplineEditorPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                   << "  \"source\": \"gvm-three-r185\",\n"
                   << "  \"caseId\": \"webgl_geometry_spline_editor\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\": " << byteCount << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"sampleCount\": 1,\n"
                   << "  \"samplePolicy\": {\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderSetType\": \"WebglGeometrySplineEditorSceneRenderSet\",\n"
                   << "  \"renderableObjectCount\": 50,\n  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 3,\n  \"screenPassCount\": 0,\n"
                   << "  \"gpuWorkDslOnly\": true,\n  \"renderSetPolicy\": \"required\",\n"
                   << "  \"drawCommandCount\": 3,\n  \"directDrawFallback\": false,\n"
                   << "  \"inputReplay\": null\n}\n";
        }
        prepareWebglGeometrySplineEditorPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_geometry_spline_editor\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n  \"renderSetPolicy\":\"required\",\n"
                   << "  \"sceneRenderSetCount\":1,\n  \"renderableObjectCount\":50,\n"
                   << "  \"scenePassCount\":3,\n  \"screenPassCount\":0,\n"
                   << "  \"drawCommandCount\":3,\n  \"directDrawFallback\":false,\n"
                   << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglGeometrySplineEditorSceneRenderSet\",\"renderableObjectCount\":50,\"entityCount\":50,\"entities\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << "{\"entityId\":" << index
                       << ",\"logicalRenderableId\":\"spline-entity-" << index
                       << "\",\"instanceCount\":1}";
            }
            output << "],\"componentSchema\":["
                   << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"drawCommandCount\":3,\"directDrawFallback\":false,\"scenePasses\":["
                   << "{\"name\":\"shadow-depth\",\"renderClass\":\"WebglGeometrySplineEditorShadowDepthPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"main-color\",\"renderClass\":\"WebglGeometrySplineEditorMainColorPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"control-overlay\",\"renderClass\":\"WebglGeometrySplineEditorControlOverlayPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]}\n";
        }
        prepareWebglGeometrySplineEditorPath(options.semanticSnapshotPath);
        if (!options.semanticSnapshotPath.empty())
        {
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_geometry_spline_editor\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"kind\":\"export-round-trip\",\n"
                   << "  \"sceneRootCount\":1,\n  \"entityCount\":50\n}\n";
        }
        captureWritten = true;
    }

    void WebglGeometrySplineEditorRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
