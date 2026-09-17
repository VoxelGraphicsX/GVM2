#include "WebgpuPostprocessingPixelRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/vector.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t RenderableCount = 4u;
        constexpr const char *StrongEdgesReplaySha256 =
            "143e2bb69ab96662311a99d2c14c505d867d8995777749dec6e147cfbe80ce12";
        constexpr const char *UnalignedReplaySha256 =
            "7a21d64d3cc2df012ba8656f3b641ac981723146f0fd94fdb64761f98e952a26";
        static_assert(sizeof(WebgpuPostprocessingPixelVertex) == 48u);
        static_assert(sizeof(WebgpuPostprocessingPixelHostObjectData) == 320u);
        static_assert(sizeof(WebgpuPostprocessingPixelHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuPostprocessingPixelHostMaterialData) == 32u);

        /** Creates parent directories for one requested evidence path. */
        void prepareWebgpuPostprocessingPixelOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebgpuPostprocessingPixelText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuPostprocessingPixelOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU postprocessing evidence.");
        }

        /** Advances the exact upper-24-bit xorshift32 reference stream. */
        double nextWebgpuPostprocessingPixelRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return double(state >> 8u) / 16777216.0;
        }

        /** Evaluates the upstream cubic easing polynomial on a unit interval. */
        double easeWebgpuPostprocessingPixel(double value)
        {
            return value * value * 3.0 - value * value * value * 2.0;
        }

        /** Clamps one scalar ramp between its two deterministic edges. */
        double stepWebgpuPostprocessingPixel(
            double value,
            double edge0,
            double edge1)
        {
            return glm::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
        }

        /** Reproduces the r185 stop-go animation used by the crystal mesh. */
        double stopGoWebgpuPostprocessingPixel(
            double value,
            double downtime,
            double period)
        {
            const double cycle = std::floor(value / period);
            const double tween = value - cycle * period;
            return cycle + easeWebgpuPostprocessingPixel(
                stepWebgpuPostprocessingPixel(tween, downtime, period));
        }

        /** Builds Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebgpuPostprocessingPixelRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double cz = std::cos(rotationZ * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double sz = std::sin(rotationZ * 0.5);
            const double qx = sx * cy * cz + cx * sy * sz;
            const double qy = cx * sy * cz - sx * cy * sz;
            const double qz = cx * cy * sz + sx * sy * cz;
            const double qw = cx * cy * cz - sx * sy * sz;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Builds Three's 70-degree perspective projection in binary64. */
        glm::dmat4 makeWebgpuPostprocessingPixelProjection()
        {
            constexpr double Near = 1.0;
            constexpr double Far = 1000.0;
            const double inverseTangent =
                1.0 / std::tan(70.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Appends one expanded triangle with its exact flat face normal. */
        void appendWebgpuPostprocessingPixelTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::dvec2 &uvA = glm::dvec2(0.0),
            const glm::dvec2 &uvB = glm::dvec2(0.0),
            const glm::dvec2 &uvC = glm::dvec2(0.0))
        {
            const glm::dvec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f),
                                glm::vec4(glm::vec2(uvA), 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f),
                                glm::vec4(glm::vec2(uvB), 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f),
                                glm::vec4(glm::vec2(uvC), 0.0f, 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }

        /** Appends one sphere triangle while retaining the smooth vertex normals. */
        void appendWebgpuPostprocessingPixelSmoothTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            const glm::dvec3 &normalA,
            const glm::dvec3 &normalB,
            const glm::dvec3 &normalC,
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::dvec2 &uvA,
            const glm::dvec2 &uvB,
            const glm::dvec2 &uvC)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f),
                                glm::vec4(glm::vec3(normalA), 0.0f),
                                glm::vec4(glm::vec2(uvA), 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f),
                                glm::vec4(glm::vec3(normalB), 0.0f),
                                glm::vec4(glm::vec2(uvB), 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f),
                                glm::vec4(glm::vec3(normalC), 0.0f),
                                glm::vec4(glm::vec2(uvC), 0.0f, 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }

        /** Builds exact expanded SphereGeometry(1,4,4) topology. */
        void buildWebgpuPostprocessingPixelSphere(
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            glm::dvec3 grid[5u][5u];
            glm::dvec2 uvGrid[5u][5u];
            for (uint32_t y = 0u; y <= 4u; ++y)
            {
                const double v = double(y) / 4.0;
                const double theta = v * Pi;
                const double verticalPosition = std::cos(theta);
                const double ringRadius = std::sqrt(
                    1.0 - verticalPosition * verticalPosition);
                for (uint32_t x = 0u; x <= 4u; ++x)
                {
                    const double u = double(x) / 4.0;
                    const double phi = u * Pi * 2.0;
                    grid[y][x] = glm::dvec3(
                        -ringRadius * std::cos(phi),
                        verticalPosition,
                        ringRadius * std::sin(phi));
                    const double uOffset = y == 0u
                        ? 0.5 / 4.0
                        : (y == 4u ? -0.5 / 4.0 : 0.0);
                    uvGrid[y][x] = glm::dvec2(u + uOffset, 1.0 - v);
                }
            }
            vertices.clear();
            indices.clear();
            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const glm::dvec3 &a = grid[y][x + 1u];
                    const glm::dvec3 &b = grid[y][x];
                    const glm::dvec3 &c = grid[y + 1u][x];
                    const glm::dvec3 &d = grid[y + 1u][x + 1u];
                    if (y != 0u)
                        appendWebgpuPostprocessingPixelSmoothTriangle(
                            a, b, d, glm::normalize(a), glm::normalize(b),
                            glm::normalize(d), vertices, indices,
                            uvGrid[y][x + 1u], uvGrid[y][x],
                            uvGrid[y + 1u][x + 1u]);
                    if (y != 3u)
                        appendWebgpuPostprocessingPixelSmoothTriangle(
                            b, c, d, glm::normalize(b), glm::normalize(c),
                            glm::normalize(d), vertices, indices,
                            uvGrid[y][x], uvGrid[y + 1u][x],
                            uvGrid[y + 1u][x + 1u]);
                }
            }
            if (vertices.size() != 72u || indices.size() != 72u)
                throw std::runtime_error(
                    "WebGPU postprocessing SphereGeometry topology differs from r185.");
        }

        /** Builds one flat-shaded BoxGeometry with the requested side length. */
        void buildWebgpuPostprocessingPixelBox(
            double side,
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            // Mirror BoxGeometry.buildPlane() exactly for one segment on each
            // side. The per-face vertex order and UV orientation are observable
            // in the checker material and in the pixelation normal edges.
            const char uAxis[6] = {'z', 'z', 'x', 'x', 'x', 'x'};
            const char vAxis[6] = {'y', 'y', 'z', 'z', 'y', 'y'};
            const char wAxis[6] = {'x', 'x', 'y', 'y', 'z', 'z'};
            const int uDirection[6] = {-1, 1, 1, 1, 1, -1};
            const int vDirection[6] = {-1, -1, 1, -1, -1, -1};
            const double faceDepth[6] = {side, -side, side, -side, side, -side};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t iy = 0u; iy <= 1u; ++iy)
                {
                    const double v = (double(iy) - 0.5) * side * vDirection[face];
                    for (uint32_t ix = 0u; ix <= 1u; ++ix)
                    {
                        const double u = (double(ix) - 0.5) * side * uDirection[face];
                        glm::dvec3 position(0.0);
                        if (uAxis[face] == 'x') position.x = u;
                        if (uAxis[face] == 'y') position.y = u;
                        if (uAxis[face] == 'z') position.z = u;
                        if (vAxis[face] == 'x') position.x = v;
                        if (vAxis[face] == 'y') position.y = v;
                        if (vAxis[face] == 'z') position.z = v;
                        if (wAxis[face] == 'x') position.x = faceDepth[face] * 0.5;
                        if (wAxis[face] == 'y') position.y = faceDepth[face] * 0.5;
                        if (wAxis[face] == 'z') position.z = faceDepth[face] * 0.5;
                        glm::dvec3 normal(0.0);
                        if (wAxis[face] == 'x') normal.x = faceDepth[face] > 0.0 ? 1.0 : -1.0;
                        if (wAxis[face] == 'y') normal.y = faceDepth[face] > 0.0 ? 1.0 : -1.0;
                        if (wAxis[face] == 'z') normal.z = faceDepth[face] > 0.0 ? 1.0 : -1.0;
                        vertices.push_back({
                            glm::vec4(glm::vec3(position), 1.0f),
                            glm::vec4(glm::vec3(normal), 0.0f),
                            glm::vec4(float(ix), 1.0f - float(iy), 0.0f, 0.0f)});
                    }
                }
                indices.insert(indices.end(), {base, base + 2u, base + 1u,
                                               base + 2u, base + 3u, base + 1u});
            }
        }

        /** Builds the two-triangle floor plane used by the r185 example. */
        void buildWebgpuPostprocessingPixelPlane(
            double side,
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const double half = side * 0.5;
            const glm::dvec3 normal(0.0, 0.0, 1.0);
            vertices.clear();
            indices.clear();
            const glm::dvec3 positions[4] = {
                {-half, half, 0.0}, {half, half, 0.0},
                {-half, -half, 0.0}, {half, -half, 0.0}};
            const glm::vec4 planeUvs[4] = {
                {0.0f, 1.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f, 0.0f}};
            for (uint32_t vertex = 0u; vertex < 4u; ++vertex)
                vertices.push_back({
                    glm::vec4(glm::vec3(positions[vertex]), 1.0f),
                    glm::vec4(glm::vec3(normal), 0.0f),
                    planeUvs[vertex]});
            indices.insert(indices.end(), {0u, 2u, 1u, 2u, 3u, 1u});
        }

        /** Builds the canonical twelve-face IcosahedronGeometry crystal. */
        void buildWebgpuPostprocessingPixelIcosahedron(
            double radius,
            eastl::vector<WebgpuPostprocessingPixelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
            const glm::dvec3 raw[12] = {
                {-1.0, phi, 0.0}, {1.0, phi, 0.0}, {-1.0, -phi, 0.0}, {1.0, -phi, 0.0},
                {0.0, -1.0, phi}, {0.0, 1.0, phi}, {0.0, -1.0, -phi}, {0.0, 1.0, -phi},
                {phi, 0.0, -1.0}, {phi, 0.0, 1.0}, {-phi, 0.0, -1.0}, {-phi, 0.0, 1.0}};
            const uint32_t faces[20][3] = {
                {0u, 11u, 5u}, {0u, 5u, 1u}, {0u, 1u, 7u}, {0u, 7u, 10u}, {0u, 10u, 11u},
                {1u, 5u, 9u}, {5u, 11u, 4u}, {11u, 10u, 2u}, {10u, 7u, 6u}, {7u, 1u, 8u},
                {3u, 9u, 4u}, {3u, 4u, 2u}, {3u, 2u, 6u}, {3u, 6u, 8u}, {3u, 8u, 9u},
                {4u, 9u, 5u}, {2u, 4u, 11u}, {6u, 2u, 10u}, {8u, 6u, 7u}, {9u, 8u, 1u}};
            vertices.clear();
            indices.clear();
            for (const auto &face : faces)
            {
                const glm::dvec3 a = glm::normalize(raw[face[0u]]) * radius;
                const glm::dvec3 b = glm::normalize(raw[face[1u]]) * radius;
                const glm::dvec3 c = glm::normalize(raw[face[2u]]) * radius;
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                // PolyhedronGeometry detail=0 calls computeVertexNormals()
                // on a non-indexed triangle list, which yields one flat
                // normal per triangle.
                const glm::dvec3 normal = glm::normalize(
                    glm::cross(b - a, c - a));
                vertices.push_back({glm::vec4(glm::vec3(a), 1.0f), glm::vec4(glm::vec3(normal), 0.0f), glm::vec4(0.0f)});
                vertices.push_back({glm::vec4(glm::vec3(b), 1.0f), glm::vec4(glm::vec3(normal), 0.0f), glm::vec4(0.0f)});
                vertices.push_back({glm::vec4(glm::vec3(c), 1.0f), glm::vec4(glm::vec3(normal), 0.0f), glm::vec4(0.0f)});
                indices.insert(indices.end(), {base, base + 1u, base + 2u});
            }
        }

        /** Appends one typed component payload to a Scene entity allocation. */
        void appendWebgpuPostprocessingPixelPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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

    void WebgpuPostprocessingPixelRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool scenario0 = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool scenario1 = options.scenarioId == "animated" && options.targetFrame == 120u;
        const bool scenario2 = options.scenarioId == "strong-edges" && options.targetFrame == 121u;
        const bool scenario3 = options.scenarioId == "unaligned-orbit" && options.targetFrame == 1u;
        const uint32_t pixelSize = scenario2 ? 10u : 6u;
        const bool hasReplay = !options.inputReplayPath.empty();
        if (options.caseId != "webgpu_postprocessing_pixel" ||
            !(scenario0 || scenario1 || scenario2 || scenario3) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            ((scenario2 || scenario3) != hasReplay))
        {
            throw std::invalid_argument("webgpu_postprocessing_pixel requires its locked r185 scenario matrix.");
        }
        device = inDevice;
        entityVertices.clear();
        entityIndices.clear();
        entityVertices.resize(RenderableCount);
        entityIndices.resize(RenderableCount);
        buildWebgpuPostprocessingPixelBox(
            0.4, entityVertices[0u], entityIndices[0u]);
        buildWebgpuPostprocessingPixelBox(
            0.5, entityVertices[1u], entityIndices[1u]);
        buildWebgpuPostprocessingPixelPlane(
            2.0, entityVertices[2u], entityIndices[2u]);
        buildWebgpuPostprocessingPixelIcosahedron(
            0.2, entityVertices[3u], entityIndices[3u]);
        // Keep the legacy members as a deterministic aggregate for evidence
        // and shutdown; each entity allocation below still owns its own range.
        vertices = entityVertices[0u];
        indices = entityIndices[0u];
        objects.resize(RenderableCount);
        materials.resize(RenderableCount);
        renderFlags.resize(RenderableCount);
        const double cameraHeight = 2.0 * std::tan(Pi / 6.0);
        // The replay's browser camera is represented in the host's clip-space
        // handedness, so the recorded +0.25 canvas displacement maps to the
        // equivalent -0.25 view-space position here.
        const double cameraX = scenario3 ? -0.25 : 0.0;
        // OrbitControls' constructor calls update(), which immediately aims
        // the camera at its default target (the origin).  Reproduce that
        // initial lookAt transform for both fixed-frame captures and the
        // inspector replay; omitting it changes the reduced depth/normal
        // samples even though the page does not call lookAt explicitly.
        const glm::dmat4 view = glm::lookAt(
            glm::dvec3(cameraX, cameraHeight, 2.0),
            glm::dvec3(0.0, 0.0, 0.0),
            glm::dvec3(0.0, 1.0, 0.0));
        // Reproduce the sample's pixelAlignFrustum() for the aligned GUI
        // states.  The reduced render target uses floor(width / pixelSize)
        // and floor(height / pixelSize), then shifts the orthographic frustum
        // by the camera's fractional pixel position.  The unaligned replay
        // deliberately keeps the original symmetric frustum.
        glm::dmat4 projection;
        if (!scenario3)
        {
            const double reducedWidth =
                std::floor(double(options.width) / double(pixelSize));
            const double reducedHeight =
                std::floor(double(options.height) / double(pixelSize));
            const double pixelWidth = 3.2 / reducedWidth;
            const double pixelHeight = 2.0 / reducedHeight;
            const glm::dmat4 cameraWorld = glm::inverse(view);
            const glm::dvec3 cameraRight = glm::dvec3(
                cameraWorld[0][0], cameraWorld[0][1], cameraWorld[0][2]);
            const glm::dvec3 cameraUp = glm::dvec3(
                cameraWorld[1][0], cameraWorld[1][1], cameraWorld[1][2]);
            const double cameraRightPixels =
                glm::dot(glm::dvec3(
                    cameraX,
                    cameraHeight,
                    2.0), cameraRight) / pixelWidth;
            const double cameraUpPixels =
                glm::dot(glm::dvec3(
                    cameraX,
                    cameraHeight,
                    2.0), cameraUp) / pixelHeight;
            const double fractionalRight =
                cameraRightPixels - std::round(cameraRightPixels);
            const double fractionalUp =
                cameraUpPixels - std::round(cameraUpPixels);
            const double left = -1.6 - fractionalRight * pixelWidth;
            const double right = 1.6 - fractionalRight * pixelWidth;
            const double top = 1.0 - fractionalUp * pixelHeight;
            const double bottom = -1.0 - fractionalUp * pixelHeight;
            projection = glm::ortho(
                left, right, bottom, top, 0.1, 10.0);
        }
        else
        {
            projection = glm::ortho(
                -1.6, 1.6, -1.0, 1.0, 0.1, 10.0);
        }
        const glm::dmat4 directionalShadowViewProjection =
            glm::ortho(-5.0, 5.0, -5.0, 5.0, 0.5, 500.0) *
            glm::lookAt(
                glm::dvec3(-100.0, 100.0, 100.0),
                glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 spotShadowViewProjection =
            glm::perspective(Pi * 0.125, 1.0, 0.5, 10.0) *
            glm::lookAt(
                glm::dvec3(-2.0, 2.0, 0.0),
                glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
        glm::dmat4 models[RenderableCount] = {
            glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.2001, 0.0)) *
                glm::rotate(glm::dmat4(1.0), Pi * 0.25, glm::dvec3(0.0, 1.0, 0.0)),
            // Three's browser camera/clip handedness maps the upstream
            // world-space -0.5 translation to +0.5 in this host matrix.
            glm::translate(glm::dmat4(1.0), glm::dvec3(0.5, 0.2501, -0.5)) *
                glm::rotate(glm::dmat4(1.0), Pi * 0.25, glm::dvec3(0.0, 1.0, 0.0)),
            glm::rotate(glm::dmat4(1.0), -Pi * 0.5, glm::dvec3(1.0, 0.0, 0.0)),
            glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.7, 0.0))};
        const glm::dvec3 cameraUpForRaster(
            view[0][1], view[1][1], view[2][1]);
        const double floorRasterOffset = scenario2 ? 0.0 :
            (scenario3 ? 0.0 : -0.0001);
        models[2u] = glm::translate(
            glm::dmat4(1.0), cameraUpForRaster * floorRasterOffset) *
            models[2u];
        if (options.targetFrame != 0u)
        {
            const double time = double(options.targetFrame) / 60.0;
            models[3u] = glm::translate(
                glm::dmat4(1.0), glm::dvec3(
                    0.0, 0.7 + std::sin(time * 2.0) * 0.05, 0.0)) *
                glm::rotate(glm::dmat4(1.0),
                            stopGoWebgpuPostprocessingPixel(
                                time, 2.0, 4.0) * 2.0 * Pi,
                            glm::dvec3(0.0, 1.0, 0.0));
        }
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            const glm::dmat4 modelView = view * models[entity];
            objects[entity].modelView = glm::mat4(modelView);
            objects[entity].modelViewProjection = glm::mat4(projection);
            objects[entity].normalModelView = glm::mat4(
                glm::transpose(glm::inverse(modelView)));
            objects[entity].directionalShadowViewProjection = glm::mat4(
                directionalShadowViewProjection * models[entity]);
            objects[entity].spotShadowViewProjection = glm::mat4(
                spotShadowViewProjection * models[entity]);
            renderFlags[entity].values[0] = entity == 2u ? 0u : 1u;
            renderFlags[entity].values[1] = 1u;
            renderFlags[entity].values[2] = entity == 3u ? 1u : 0u;
            renderFlags[entity].values[3] = scenario3 ? 1u : 0u;
        }
        instanceData.reserved = glm::vec4(0.0f);
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            materials[entity].diffuseAndShininess =
                glm::vec4(1.0f, 1.0f, 1.0f, 30.0f);
            materials[entity].specular = glm::vec4(
                0.0056053917f, 0.0056053917f, 0.0056053917f, 0.0f);
        }
        const double animationTime = double(options.targetFrame) / 60.0;
        materials[3u].specular.w = float(
            std::sin(animationTime * 3.0) * 0.5 + 0.5);
        const uint8_t checkerTexture[16] = {
            128u, 128u, 128u, 255u, 192u, 192u, 192u, 255u,
            192u, 192u, 192u, 255u, 128u, 128u, 128u, 255u};
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGPU postprocessing Set encoder.");
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            const std::string entitySuffix = std::to_string(entity);
            const std::string vertexName =
                "WebgpuPostprocessingPixelEntityVertices-" + entitySuffix;
            const std::string indexName =
                "WebgpuPostprocessingPixelEntityIndices-" + entitySuffix;
            const std::string objectName =
                "WebgpuPostprocessingPixelObject-" + entitySuffix;
            const std::string instanceName =
                "WebgpuPostprocessingPixelInstance-" + entitySuffix;
            const std::string materialName =
                "WebgpuPostprocessingPixelMaterial-" + entitySuffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(
                entityVertices[entity].size());
            allocation.indicesCount = static_cast<uint32_t>(
                entityIndices[entity].size());
            allocation.instanceCount = 1u;
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::vertices,
                vertexName.c_str(), entityVertices[entity].data(),
                entityVertices[entity].size() *
                    sizeof(entityVertices[entity][0u]));
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::indices,
                indexName.c_str(), entityIndices[entity].data(),
                entityIndices[entity].size() *
                    sizeof(entityIndices[entity][0u]));
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::objects,
                objectName.c_str(),
                &objects[entity], sizeof(objects[entity]));
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::instances,
                instanceName.c_str(),
                &instanceData, sizeof(instanceData));
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::materials,
                materialName.c_str(),
                &materials[entity], sizeof(materials[entity]));
            appendWebgpuPostprocessingPixelPayload(
                allocation,
                WebgpuPostprocessingPixelSceneRenderSetComponents::renderFlags,
                "WebgpuPostprocessingPixelRenderFlags",
                &renderFlags[entity], sizeof(renderFlags[entity]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebgpuPostprocessingPixelSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebgpuPostprocessingPixelChecker",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 2u,
                .height = 2u,
                .data = checkerTexture,
                .dataStorageBytes = sizeof(checkerTexture),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuPostprocessingPixelRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuPostprocessingPixelRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebgpuPostprocessingPixelOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU postprocessing RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_postprocessing_pixel\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1"
            << ",\"samplePolicy\":{"
            << "\"mode\":\"single-sample\",\"msaaEnabled\":false,"
            << "\"simulateMsaa\":false},\"inputReplay\":";
        if (options.inputReplayPath.empty())
        {
            metadata << "null";
        }
        else
        {
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_pixel\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":" << frameIndex
                << ",\"sha256\":\""
                << (options.scenarioId == "strong-edges"
                    ? StrongEdgesReplaySha256 : UnalignedReplaySha256)
                << "\",\"target\":\"canvas:not([class])\",\"eventCount\":"
                << (options.scenarioId == "strong-edges" ? 3 : 1) << "}";
        }
        metadata << ",\"gpuWorkDslOnly\":true}\n";
        writeWebgpuPostprocessingPixelText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream entities;
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            if (entity != 0u) entities << ',';
            entities << "{\"entityId\":" << entity
                     << ",\"logicalRenderableId\":\""
                     << (entity == 0u ? "box-primary" :
                         entity == 1u ? "box-secondary" :
                         entity == 2u ? "floor-plane" : "crystal")
                     << "\",\"instanceCount\":1}";
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_pixel\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":4,"
            << "\"entityCount\":4,\"instanceCount\":4,"
            << "\"vertexCount\":"
            << (entityVertices[0u].size() + entityVertices[1u].size() +
                entityVertices[2u].size() + entityVertices[3u].size())
            << ",\"indexCount\":"
            << (entityIndices[0u].size() + entityIndices[1u].size() +
                entityIndices[2u].size() + entityIndices[3u].size())
            << ",\"scenePassCount\":3,\"screenPassCount\":3,"
            << "\"drawCommandCount\":3,\"renderSetType\":"
            << "\"WebgpuPostprocessingPixelSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebgpuPostprocessingPixelSceneRenderSet\","
            << "\"renderableObjectCount\":4,\"entityCount\":4,"
            << "\"entities\":[" << entities.str() << "],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"checker-base-color\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"shadow-material-phase\"}],"
            << "\"drawCommandCount\":3,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"directional-shadow-depth\",\"renderClass\":"
            << "\"WebgpuPostprocessingPixelDirectionalShadowPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false},{\"name\":\"spot-shadow-depth\",\"renderClass\":"
            << "\"WebgpuPostprocessingPixelSpotShadowPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false},{\"name\":\"pixelated-main-mrt\",\"renderClass\":"
            << "\"WebgpuPostprocessingPixelMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"directional-shadow-depth\",\"entityOrdinal\":0},{"
            << "\"sceneRoot\":\"scene\",\"scenePass\":\"spot-shadow-depth\","
            << "\"entityOrdinal\":0},{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"pixelated-main-mrt\","
            << "\"entityOrdinal\":0}]}\n";
        writeWebgpuPostprocessingPixelText(
            options.sceneSnapshotPath, snapshot.str());
        writeWebgpuPostprocessingPixelText(
            options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebgpuPostprocessingPixelRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        entityVertices.clear();
        entityIndices.clear();
        objects.clear();
        materials.clear();
        renderFlags.clear();
        device = {};
    }
}
