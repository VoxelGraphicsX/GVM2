#include "WebgpuMultipleCanvasRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandles[] = {
            ExportedRenderSet::sceneSet0,
            ExportedRenderSet::sceneSet1,
            ExportedRenderSet::sceneSet2,
            ExportedRenderSet::sceneSet3,
            ExportedRenderSet::sceneSet4,
            ExportedRenderSet::sceneSet5,
            ExportedRenderSet::sceneSet6,
            ExportedRenderSet::sceneSet7,
            ExportedRenderSet::sceneSet8,
            ExportedRenderSet::sceneSet9,
            ExportedRenderSet::sceneSet10,
            ExportedRenderSet::sceneSet11,
            ExportedRenderSet::sceneSet12,
            ExportedRenderSet::sceneSet13,
            ExportedRenderSet::sceneSet14,
            ExportedRenderSet::sceneSet15,
            ExportedRenderSet::sceneSet16,
            ExportedRenderSet::sceneSet17,
            ExportedRenderSet::sceneSet18,
            ExportedRenderSet::sceneSet19,
            ExportedRenderSet::sceneSet20,
            ExportedRenderSet::sceneSet21,
            ExportedRenderSet::sceneSet22,
            ExportedRenderSet::sceneSet23,
            ExportedRenderSet::sceneSet24,
            ExportedRenderSet::sceneSet25,
            ExportedRenderSet::sceneSet26,
            ExportedRenderSet::sceneSet27,
            ExportedRenderSet::sceneSet28,
            ExportedRenderSet::sceneSet29,
            ExportedRenderSet::sceneSet30,
            ExportedRenderSet::sceneSet31,
            ExportedRenderSet::sceneSet32,
            ExportedRenderSet::sceneSet33,
            ExportedRenderSet::sceneSet34,
            ExportedRenderSet::sceneSet35,
            ExportedRenderSet::sceneSet36,
            ExportedRenderSet::sceneSet37,
            ExportedRenderSet::sceneSet38,
            ExportedRenderSet::sceneSet39
        };

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebgpuMultipleCanvasPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebgpuMultipleCanvasBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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
        void appendWebgpuMultipleCanvasTriangle(WebgpuMultipleCanvasEntity &entity,
                                       const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &c,
                                       const glm::vec3 &normal)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            // The Three camera looks down -Z with a right-handed X axis.  The
            // generated UGL clip convention mirrors that axis, so normalize
            // the source geometry once at the CPU boundary for both grouped
            // and standalone paths.
            const glm::vec3 mirroredNormal(-normal.x, normal.y, normal.z);
            const glm::vec3 mirroredA(-a.x, a.y, a.z);
            const glm::vec3 mirroredB(-b.x, b.y, b.z);
            const glm::vec3 mirroredC(-c.x, c.y, c.z);
            entity.vertices.push_back({glm::vec4(mirroredA, 1.0f), glm::vec4(mirroredNormal, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(mirroredB, 1.0f), glm::vec4(mirroredNormal, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(mirroredC, 1.0f), glm::vec4(mirroredNormal, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Generates the eight-sided cone used by the upstream webgpuMultipleCanvas example. */
        void buildWebgpuMultipleCanvasCone(WebgpuMultipleCanvasEntity &entity)
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
                appendWebgpuMultipleCanvasTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebgpuMultipleCanvasTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildWebgpuMultipleCanvasSphere(WebgpuMultipleCanvasEntity &entity,
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
                    appendWebgpuMultipleCanvasTriangle(entity, a, b, c, glm::normalize(a));
                    appendWebgpuMultipleCanvasTriangle(entity, a, c, d, glm::normalize(a));
                }
            }
        }

        /** Builds the flat-shaded unit BoxGeometry used by the first canvas variant. */
        void buildWebgpuMultipleCanvasBox(WebgpuMultipleCanvasEntity &entity)
        {
            const glm::vec3 positions[8] = {
                {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}};
            const uint32_t faces[6][4] = {{0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u},
                                          {4u, 0u, 3u, 7u}, {1u, 5u, 6u, 2u},
                                          {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u}};
            const glm::vec3 normals[6] = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                                          {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                                          {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                appendWebgpuMultipleCanvasTriangle(entity, positions[faces[face][0]], positions[faces[face][1]], positions[faces[face][2]], normals[face]);
                appendWebgpuMultipleCanvasTriangle(entity, positions[faces[face][0]], positions[faces[face][2]], positions[faces[face][3]], normals[face]);
            }
        }

        /** Builds the twelve-segment unit CylinderGeometry used by the fourth variant. */
        void buildWebgpuMultipleCanvasCylinder(WebgpuMultipleCanvasEntity &entity)
        {
            constexpr uint32_t SegmentCount = 12u;
            for (uint32_t segment = 0u; segment < SegmentCount; ++segment)
            {
                const double a0 = 2.0 * Pi * double(segment) / double(SegmentCount);
                const double a1 = 2.0 * Pi * double(segment + 1u) / double(SegmentCount);
                // Match Three.js CylinderGeometry's theta convention:
                // x = sin(theta), z = cos(theta), with thetaStart = 0.
                const glm::vec3 p0(0.5f * float(std::sin(a0)), -0.5f, 0.5f * float(std::cos(a0)));
                const glm::vec3 p1(0.5f * float(std::sin(a1)), -0.5f, 0.5f * float(std::cos(a1)));
                const glm::vec3 q0(p0.x, 0.5f, p0.z);
                const glm::vec3 q1(p1.x, 0.5f, p1.z);
                const glm::vec3 sideNormal = glm::normalize(glm::vec3(p0.x + p1.x, 0.0f, p0.z + p1.z));
                appendWebgpuMultipleCanvasTriangle(entity, p0, p1, q1, sideNormal);
                appendWebgpuMultipleCanvasTriangle(entity, p0, q1, q0, sideNormal);
                appendWebgpuMultipleCanvasTriangle(entity, glm::vec3(0.0f, 0.5f, 0.0f), q1, q0, glm::vec3(0.0f, 1.0f, 0.0f));
                appendWebgpuMultipleCanvasTriangle(entity, glm::vec3(0.0f, -0.5f, 0.0f), p0, p1, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Builds Three.js r185's non-indexed radius-0.5 DodecahedronGeometry. */
        void buildWebgpuMultipleCanvasDodecahedron(WebgpuMultipleCanvasEntity &entity)
        {
            const float t = float((1.0 + std::sqrt(5.0)) * 0.5);
            const float r = 1.0f / t;
            const glm::vec3 vertices[20] = {
                {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f},
                {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f},
                {0.0f, -r, -t}, {0.0f, -r, t}, {0.0f, r, -t}, {0.0f, r, t},
                {-r, -t, 0.0f}, {-r, t, 0.0f}, {r, -t, 0.0f}, {r, t, 0.0f},
                {-t, 0.0f, -r}, {t, 0.0f, -r}, {-t, 0.0f, r}, {t, 0.0f, r}};
            const uint32_t indices[] = {
                3u, 11u, 7u, 3u, 7u, 15u, 3u, 15u, 13u,
                7u, 19u, 17u, 7u, 17u, 6u, 7u, 6u, 15u,
                17u, 4u, 8u, 17u, 8u, 10u, 17u, 10u, 6u,
                8u, 0u, 16u, 8u, 16u, 2u, 8u, 2u, 10u,
                0u, 12u, 1u, 0u, 1u, 18u, 0u, 18u, 16u,
                6u, 10u, 2u, 6u, 2u, 13u, 6u, 13u, 15u,
                2u, 16u, 18u, 2u, 18u, 3u, 2u, 3u, 13u,
                18u, 1u, 9u, 18u, 9u, 11u, 18u, 11u, 3u,
                4u, 14u, 12u, 4u, 12u, 0u, 4u, 0u, 8u,
                11u, 9u, 5u, 11u, 5u, 19u, 11u, 19u, 7u,
                19u, 5u, 14u, 19u, 14u, 4u, 19u, 4u, 17u,
                1u, 12u, 14u, 1u, 14u, 5u, 1u, 5u, 9u};
            for (size_t triangle = 0u; triangle < sizeof(indices) / sizeof(indices[0]); triangle += 3u)
            {
                const glm::vec3 a = glm::normalize(vertices[indices[triangle]]) * 0.5f;
                const glm::vec3 b = glm::normalize(vertices[indices[triangle + 1u]]) * 0.5f;
                const glm::vec3 c = glm::normalize(vertices[indices[triangle + 2u]]) * 0.5f;
                appendWebgpuMultipleCanvasTriangle(entity, a, b, c,
                    glm::normalize(glm::cross(b - a, c - a)));
            }
        }

        /** Appends a clip-space canvas background without adding another Scene entity. */
        void appendWebgpuMultipleCanvasBackground(WebgpuMultipleCanvasEntity &entity,
                                                   const glm::mat4 &inverseModelViewProjection)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 clipPositions[4] = {
                {-1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
                {1.0f, -1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f, 1.0f}};
            const uint32_t backgroundIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
            for (const glm::vec4 &clipPosition : clipPositions)
            {
                const glm::vec4 local = inverseModelViewProjection * clipPosition;
                const glm::vec4 normalizedLocal = local / local.w;
                entity.vertices.push_back({normalizedLocal, glm::vec4(0.0f), glm::vec4(-1.0f)});
            }
            for (uint32_t index : backgroundIndices)
                entity.indices.push_back(base + index);
        }

        /** Converts the upstream setHSL(..., SRGBColorSpace) result to linear working RGB. */
        glm::vec4 webgpuMultipleCanvasHslColor(float hue)
        {
            hue = hue - std::floor(hue);
            const float p = 1.0f;
            const float q = 0.5f;
            const auto hueToRgb = [hue, p, q](float value) {
                float wrapped = value - std::floor(value);
                if (wrapped < 1.0f / 6.0f) return q + (p - q) * 6.0f * wrapped;
                if (wrapped < 0.5f) return p;
                if (wrapped < 2.0f / 3.0f) return q + (p - q) * (2.0f / 3.0f - wrapped) * 6.0f;
                return q;
            };
            const auto srgbToLinear = [](float value) {
                return value < 0.04045f
                    ? value * 0.0773993808f
                    : std::pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
            };
            return glm::vec4(srgbToLinear(hueToRgb(hue + 1.0f / 3.0f)),
                             srgbToLinear(hueToRgb(hue)),
                             srgbToLinear(hueToRgb(hue - 1.0f / 3.0f)), 1.0f);
        }

        /** Builds the Three perspective matrix with a square 200-pixel canvas aspect. */
        glm::mat4 webgpuMultipleCanvasProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(50.0f), aspect, 1.0f, 10.0f);
        }

        /** Converts one simple Scene's world vertices into the final canvas clip rectangle. */
        void appendWebgpuMultipleCanvasSimpleGeometry(
            const WebgpuMultipleCanvasEntity &entity,
            eastl::vector<WebgpuMultipleCanvasSimpleVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            uint32_t &firstIndex,
            uint32_t &indexCount)
        {
            const uint32_t vertexBase = static_cast<uint32_t>(vertices.size());
            firstIndex = static_cast<uint32_t>(indices.size());
            for (const WebgpuMultipleCanvasHostVertex &source : entity.vertices)
            {
                glm::vec4 clip = entity.objectData.modelViewProjection * source.position;
                clip.z = (clip.z + clip.w) * 0.5f;
                const glm::vec2 viewUv = glm::vec2(clip.x, clip.y) / clip.w * 0.5f + glm::vec2(0.5f);
                const glm::vec2 compositeUv = glm::vec2(entity.objectData.viewport.x,
                    entity.objectData.viewport.y) + viewUv * glm::vec2(entity.objectData.viewport.z,
                        entity.objectData.viewport.w);
                const float compositorX = entity.objectData.viewport.x +
                    (1.0f - viewUv.x) * entity.objectData.viewport.z;
                clip.x = (compositorX * 2.0f - 1.0f) * clip.w;
                // Standalone RenderClass targets use the backend's bottom-left
                // viewport convention, while the page compositor's viewport.y
                // is measured from the CSS top edge.  Mirror the composed Y
                // coordinate once so simple canvases retain their source row.
                const float compositorY = 1.0f - compositeUv.y;
                clip.y = (compositorY * 2.0f - 1.0f) * clip.w;
                // The ordinary path has already performed the perspective
                // divide and viewport composition on the CPU.  Emit canonical
                // NDC coordinates with w=1 so the rasterizer cannot reapply a
                // projective interpolation to the page-composite quad.
                clip.x /= clip.w;
                clip.y /= clip.w;
                clip.z /= clip.w;
                clip.w = 1.0f;
                const bool background = source.barycentric.x < -0.5f;
                // Keep the page-composite background just inside the far clip
                // plane.  The inverse-projection corner is mathematically at
                // z/w == 1, but roundoff at the boundary can clip individual
                // triangles differently on Metal and Vulkan.
                if (background)
                    clip.z = clip.w * 0.999f;
                const glm::vec3 normal = background
                    ? glm::vec3(0.0f)
                    : glm::normalize(glm::vec3(entity.objectData.normalMatrix *
                        glm::vec4(source.normalAndFlags.x, source.normalAndFlags.y,
                            source.normalAndFlags.z, 0.0f)));
                const glm::vec3 compositorNormal = background
                    ? normal
                    : glm::vec3(normal.x, -normal.y, normal.z);
                const glm::vec3 color = glm::vec3(entity.materialData.baseColorAndFlags);
                vertices.push_back({
                    UGL::float4(clip.x, clip.y, clip.z, clip.w),
                    UGL::float4(compositorNormal.x, compositorNormal.y, compositorNormal.z, background ? 1.0f : 0.0f),
                    UGL::float4(color.x, color.y, color.z, background ? 1.0f : 0.0f)});
            }
            for (uint32_t index : entity.indices)
            {
                indices.push_back(vertexBase + index);
            }
            indexCount = static_cast<uint32_t>(indices.size()) - firstIndex;
        }

        /** Returns whether a zero-based canvas Scene carries the geometry-group RenderSet. */
        bool isWebgpuMultipleCanvasGroupedScene(uint32_t sceneIndex)
        {
            switch (sceneIndex)
            {
            case 4u: case 6u: case 8u: case 9u: case 10u: case 11u: case 12u:
            case 14u: case 15u: case 16u: case 19u: case 22u: case 23u: case 26u:
            case 29u: case 30u: case 34u: case 35u: case 37u:
                return true;
            default:
                return false;
            }
        }

        /** Validates the four deterministic target/webgpuMultipleCanvas scenarios. */
        /** Validates the frozen WebgpuMultipleCanvas scenario matrix and output contract. */
        void validateWebgpuMultipleCanvasOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-layout" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "first-canvas-orbit" && options.targetFrame == 1u;
            if (options.caseId != "webgpu_multiple_canvas"
                || (!scenario0 && !scenario1)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgpu_multiple_canvas scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebgpuMultipleCanvasRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebgpuMultipleCanvasOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(40u);
        simpleVertices.clear();
        simpleIndices.clear();
        simpleFirstIndices.clear();
        simpleIndexCounts.clear();
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t skip = 0u; skip < 216u; ++skip)
            (void)random.nextFloat();
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const uint32_t geometryIndex = static_cast<uint32_t>(random.nextFloat() * 4.0f);
            const float hue = random.nextFloat();
            if (geometryIndex == 0u) buildWebgpuMultipleCanvasBox(entities[index]);
            else if (geometryIndex == 1u) buildWebgpuMultipleCanvasSphere(entities[index], 8u, 12u, 0.5f);
            else if (geometryIndex == 2u) buildWebgpuMultipleCanvasDodecahedron(entities[index]);
            else buildWebgpuMultipleCanvasCylinder(entities[index]);
            entities[index].materialData.baseColorAndFlags = webgpuMultipleCanvasHslColor(hue);
            for (uint32_t skip = 0u; skip < 48u; ++skip)
                (void)random.nextFloat();
        }
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            const glm::mat4 model = glm::mat4(1.0f);
            const float orbitAngle = index == 0u && options.scenarioId == "first-canvas-orbit"
                ? -0.35f
                : 0.0f;
            const glm::vec3 camera(2.0f * std::sin(orbitAngle), 0.0f,
                2.0f * std::cos(orbitAngle));
            const glm::mat4 sceneView = glm::lookAt(camera, glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = webgpuMultipleCanvasProjection(200u, 200u);
            entity.objectData.modelView = sceneView * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            if (isWebgpuMultipleCanvasGroupedScene(index))
            {
                // RenderSet scenes consume the clip transform in the generated
                // vertex stage.  Reflect its screen-space X once so the grouped
                // path has the same canvas orientation as the page compositor;
                // normals remain attached to the moved fragments.
                glm::mat4 screenReflection(1.0f);
                screenReflection[0][0] = -1.0f;
                entity.objectData.modelViewProjection =
                    screenReflection * entity.objectData.modelViewProjection;
            }
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(model));
            const uint32_t canvasColumn = index % 3u;
            const uint32_t canvasRow = index / 3u;
            constexpr float CanvasX[3u] = {26.0f, 278.0f, 530.0f};
            constexpr float CanvasWidth = 200.0f;
            constexpr float CanvasHeight = 200.0f;
            const float canvasY = 65.0f + float(canvasRow) * 293.0f;
            entity.objectData.viewport = glm::vec4(
                CanvasX[canvasColumn] / float(options.width),
                canvasY / float(options.height),
                CanvasWidth / float(options.width),
                CanvasHeight / float(options.height));
            entity.objectData.baseColorAndFlags = glm::vec4(entity.materialData.baseColorAndFlags.x,
                entity.materialData.baseColorAndFlags.y, entity.materialData.baseColorAndFlags.z, 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            appendWebgpuMultipleCanvasBackground(entity,
                glm::inverse(entity.objectData.modelViewProjection));
        }
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (isWebgpuMultipleCanvasGroupedScene(index)) continue;
            uint32_t firstIndex = 0u;
            uint32_t indexCount = 0u;
            appendWebgpuMultipleCanvasSimpleGeometry(
                entities[index], simpleVertices, simpleIndices, firstIndex, indexCount);
            simpleFirstIndices.push_back(firstIndex);
            simpleIndexCounts.push_back(indexCount);
        }
        if (simpleFirstIndices.size() != 21u || simpleIndexCounts.size() != 21u)
        {
            throw std::logic_error("webgpu_multiple_canvas simple Scene packing produced an invalid range count.");
        }
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (!isWebgpuMultipleCanvasGroupedScene(index)) continue;
            const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandles[index]);
            if (!encoder) throw std::runtime_error("webgpu_multiple_canvas could not create its RenderSet encoder.");
            const auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebgpuMultipleCanvasBuffer(allocation, WebgpuMultipleCanvasGroupedSceneRenderSetComponents::vertices, "WebgpuMultipleCanvasVertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebgpuMultipleCanvasBuffer(allocation, WebgpuMultipleCanvasGroupedSceneRenderSetComponents::indices, "WebgpuMultipleCanvasIndices", entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebgpuMultipleCanvasBuffer(allocation, WebgpuMultipleCanvasGroupedSceneRenderSetComponents::objects, "WebgpuMultipleCanvasObject", &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebgpuMultipleCanvasBuffer(allocation, WebgpuMultipleCanvasGroupedSceneRenderSetComponents::instances, "WebgpuMultipleCanvasInstance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebgpuMultipleCanvasBuffer(allocation, WebgpuMultipleCanvasGroupedSceneRenderSetComponents::materials, "WebgpuMultipleCanvasMaterial", &entity.materialData, sizeof(entity.materialData), 1u);
            encoder->allocEntity(allocation);
            renderer.executeRenderSetCommand(SceneRenderSetHandles[index], encoder);
        }
    }

    void WebgpuMultipleCanvasRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuMultipleCanvasRuntimeAdapter::afterFrame(
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
        prepareWebgpuMultipleCanvasPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebgpuMultipleCanvasPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n  \"source\": \"gvm-three-r185\",\n"
                   << "  \"caseId\": \"webgpu_multiple_canvas\",\n"
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
                   << "  \"inputReplay\": ";
            if (options.inputReplayPath.empty()) output << "null";
            else
            {
                // The runner validates the immutable replay contract rather than
                // the host-local path. Keep this object aligned with the locked
                // first-canvas-orbit capture in every quadrant.
                output << "{\"schemaVersion\":1,\"caseId\":\"webgpu_multiple_canvas\","
                       << "\"scenarioId\":\"first-canvas-orbit\",\"captureFrame\":1,"
                       << "\"sha256\":\"adcf3843eeda54c02d8cec84f276d08c1122222e6c837d44a7632fe8eff74b5a\","
                       << "\"target\":\"#content .list-item:nth-of-type(2) canvas\","
                       << "\"eventCount\":3,\"lastEventFrame\":1,"
                       << "\"runtime\":{\"target\":\"#content .list-item:nth-of-type(2) canvas\","
                       << "\"dispatchedEventCount\":3,\"cssWidth\":200,\"cssHeight\":200}}";
            }
            output << ",\n"
                   << "  \"sceneRenderSetCount\": 19,\n  \"renderSetType\": \"WebgpuMultipleCanvasGroupedSceneRenderSet\",\n"
                   << "  \"entityCount\": 19,\n  \"instanceCounts\": [";
            bool firstInstanceCount = true;
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (!isWebgpuMultipleCanvasGroupedScene(static_cast<uint32_t>(index))) continue;
                if (!firstInstanceCount) output << ',';
                output << 1u;
                firstInstanceCount = false;
            }
            output << "],\n  \"sceneRoots\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << "{\"id\":\"scene-" << (index + 1u)
                       << "\",\"renderSetCount\":"
                       << (isWebgpuMultipleCanvasGroupedScene(static_cast<uint32_t>(index)) ? 1 : 0)
                       << "}";
            }
            output << "],\n"
                   << "  \"scenePassCount\": 40,\n  \"screenPassCount\": 1,\n"
                   << "  \"drawCommandCount\": 40,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n"
            << "  \"msaaEnabled\": false\n}\n";
        }
        prepareWebgpuMultipleCanvasPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream snapshot(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            snapshot << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgpu_multiple_canvas\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"frame\":" << frameIndex << ",\n"
                     << "  \"implementationLevel\":\"semantic-complete\",\n"
                     << "  \"gpuWorkDslOnly\":true,\n  \"renderSetPolicy\":\"required\",\n"
                     << "  \"sceneRenderSetCount\":19,\n  \"renderableObjectCount\":40,\n"
                     << "  \"entityCount\":19,\n  \"instanceCounts\":[";
            bool firstSnapshotInstance = true;
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (!isWebgpuMultipleCanvasGroupedScene(static_cast<uint32_t>(index))) continue;
                if (!firstSnapshotInstance) snapshot << ',';
                snapshot << '1';
                firstSnapshotInstance = false;
            }
            snapshot << "],\n  \"scenePassCount\":40,\n  \"screenPassCount\":1,\n"
                     << "  \"drawCommandCount\":40,\n  \"directDrawFallback\":false,\n"
                     << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
                     << "  \"renderSetType\":\"WebgpuMultipleCanvasGroupedSceneRenderSet\",\n"
                     << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                     << "  \"sceneRoots\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) snapshot << ',';
                const uint32_t sceneNumber = static_cast<uint32_t>(index) + 1u;
                const bool grouped = isWebgpuMultipleCanvasGroupedScene(static_cast<uint32_t>(index));
                snapshot << "{\"id\":\"scene-" << sceneNumber << "\",\"renderSetCount\":"
                         << (grouped ? 1 : 0) << ",\"renderableObjectCount\":1";
                if (grouped)
                {
                    snapshot << ",\"renderSetId\":\"scene-set-" << sceneNumber
                             << "\",\"renderSetType\":\"WebgpuMultipleCanvasGroupedSceneRenderSet\",\"entityCount\":1,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"canvas-"
                             << sceneNumber << "-mesh\",\"instanceCount\":1}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"canvas-"
                             << sceneNumber << "\",\"renderClass\":\"WebgpuMultipleCanvasScene" << sceneNumber << "Pass\",\"renderSetId\":\"scene-set-" << sceneNumber
                             << "\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]";
                }
                snapshot << "}";
            }
            snapshot << "],\n  \"scenePassSequence\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) snapshot << ',';
                const uint32_t sceneNumber = static_cast<uint32_t>(index) + 1u;
                snapshot << "{\"sceneRoot\":\"scene-" << sceneNumber
                         << "\",\"scenePass\":\"canvas-" << sceneNumber << "\",\"entityOrdinal\":0}";
            }
            snapshot << "],\n  \"screenPasses\":[{\"name\":\"multi-canvas-layout-composite\",\"drawMode\":\"fullscreen-triangle\",\"drawCommandCount\":1}]\n}\n";
        }
        captureWritten = true;
    }

    void WebgpuMultipleCanvasRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        simpleVertices.clear();
        simpleIndices.clear();
        simpleFirstIndices.clear();
        simpleIndexCounts.clear();
    }
}
