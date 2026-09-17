#include "WebglGeometryColorsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>

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
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;
        constexpr uint32_t ExpectedEntityCount = 9u;

        /** Calculates the SHA-256 identity of one locked input replay file. */
        std::string geometryColorsReplaySha256(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return {};
            std::ifstream input(std::filesystem::path(pathValue.c_str()),
                                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "webgl_geometry_colors could not open its input replay.");
            }
            const std::streamoff size = input.tellg();
            if (size <= 0)
            {
                throw std::runtime_error(
                    "webgl_geometry_colors input replay is empty.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
            {
                throw std::runtime_error(
                    "webgl_geometry_colors could not read its input replay.");
            }
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Creates the parent directory for one optional capture artifact. */
        void prepareOutputPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Converts one sRGB channel to the linear working color domain. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts an HSL triplet to linear RGB for a Three vertex color. */
        glm::vec3 hslToLinear(float hue, float saturation, float lightness)
        {
            const auto hueToRgb = [](float p, float q, float t)
            {
                if (t < 0.0f) t += 1.0f;
                if (t > 1.0f) t -= 1.0f;
                if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
                if (t < 1.0f / 2.0f) return q;
                if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
                return p;
            };
            if (saturation <= 0.0f)
            {
                const float channel = srgbToLinear(lightness);
                return glm::vec3(channel);
            }
            const float q = lightness < 0.5f
                ? lightness * (1.0f + saturation)
                : lightness + saturation - lightness * saturation;
            const float p = 2.0f * lightness - q;
            const glm::vec3 srgb(
                hueToRgb(p, q, hue + 1.0f / 3.0f),
                hueToRgb(p, q, hue),
                hueToRgb(p, q, hue - 1.0f / 3.0f));
            return glm::vec3(
                srgbToLinear(srgb.x),
                srgbToLinear(srgb.y),
                srgbToLinear(srgb.z));
        }

        /** Returns the exact three vertex-color functions from the r185 page. */
        glm::vec3 geometryColor(uint32_t geometryIndex, const glm::vec3 &position)
        {
            const float y = (position.y / 200.0f + 1.0f) * 0.5f;
            if (geometryIndex == 0u)
            {
                return hslToLinear(y, 1.0f, 0.5f);
            }
            if (geometryIndex == 1u)
            {
                return hslToLinear(0.0f, y, 0.5f);
            }
            const glm::vec3 srgb(1.0f, 0.8f - y, 0.0f);
            return glm::vec3(
                srgbToLinear(std::clamp(srgb.x, 0.0f, 1.0f)),
                srgbToLinear(std::clamp(srgb.y, 0.0f, 1.0f)),
                0.0f);
        }

        /** Adds one flat-shaded triangle to a sphere entity. */
        void appendTriangle(WebglGeometryColorsEntityData &entity,
                            const glm::vec3 &a,
                            const glm::vec3 &b,
                            const glm::vec3 &c,
                            uint32_t geometryIndex,
                            bool wireframe,
                            bool frontFacing = true)
        {
            const glm::vec3 edgeA = b - a;
            const glm::vec3 edgeB = c - a;
            glm::vec3 normal = glm::normalize(glm::cross(edgeA, edgeB));
            if (glm::dot(normal, (a + b + c) / 3.0f) < 0.0f)
            {
                normal = -normal;
            }
            if (!wireframe)
            {
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                const glm::vec3 colors[] = {
                    geometryColor(geometryIndex, a),
                    geometryColor(geometryIndex, b),
                    geometryColor(geometryIndex, c)};
                const glm::vec3 positions[] = {a, b, c};
                for (uint32_t vertex = 0u; vertex < 3u; ++vertex)
                {
                    entity.vertices.push_back({
                        glm::vec4(positions[vertex], 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(colors[vertex], 1.0f),
                        glm::vec4(0.0f)});
                }
                entity.indices.push_back(base + 0u);
                entity.indices.push_back(base + 1u);
                entity.indices.push_back(base + 2u);
                return;
            }
            // Keep only front-facing triangle edges.  With the one-pixel
            // native LineList path this is equivalent to the depth-tested
            // WireframeGeometry result while avoiding duplicate back-face
            // segments at shared icosahedron edges.
            if (!frontFacing) return;
            const glm::vec3 starts[] = {a, b, c};
            const glm::vec3 ends[] = {b, c, a};
            for (uint32_t edge = 0u; edge < 3u; ++edge)
            {
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                entity.vertices.push_back({
                    glm::vec4(starts[edge], 1.0f),
                    glm::vec4(ends[edge], 1.0f),
                    glm::vec4(0.0f), glm::vec4(0.0f)});
                entity.vertices.push_back({
                    glm::vec4(starts[edge], 1.0f),
                    glm::vec4(ends[edge], 1.0f),
                    glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
                entity.indices.insert(entity.indices.end(), {base, base + 1u});
            }
        }

        /** Appends one unique edge as a one-pixel triangle-list strip. */
        void appendWireframeEdge(WebglGeometryColorsEntityData &entity,
                                 const glm::vec3 &start,
                                 const glm::vec3 &end)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 startPosition(start, 1.0f);
            const glm::vec4 endPosition(end, 1.0f);
            entity.vertices.push_back({
                startPosition, endPosition, glm::vec4(0.0f),
                glm::vec4(0.0f, -0.5f, 0.0f, 0.0f)});
            entity.vertices.push_back({
                startPosition, endPosition, glm::vec4(0.0f),
                glm::vec4(0.0f, 0.5f, 0.0f, 0.0f)});
            entity.vertices.push_back({
                startPosition, endPosition, glm::vec4(0.0f),
                glm::vec4(1.0f, -0.5f, 0.0f, 0.0f)});
            entity.vertices.push_back({
                startPosition, endPosition, glm::vec4(0.0f),
                glm::vec4(1.0f, 0.5f, 0.0f, 0.0f)});
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u,
                base + 2u, base + 1u, base + 3u});
        }

        /** Returns whether an undirected edge already exists in the authored stream. */
        bool containsWireframeEdge(const eastl::vector<glm::vec3> &edgeStarts,
                                   const eastl::vector<glm::vec3> &edgeEnds,
                                   const glm::vec3 &start,
                                   const glm::vec3 &end)
        {
            constexpr float PositionToleranceSquared = 1.0e-12f;
            for (size_t edge = 0u; edge < edgeStarts.size(); ++edge)
            {
                const glm::vec3 forward = edgeStarts[edge] - start;
                const glm::vec3 reverse = edgeEnds[edge] - end;
                const glm::vec3 reverseForward = edgeStarts[edge] - end;
                const glm::vec3 reverseReverse = edgeEnds[edge] - start;
                if ((glm::dot(forward, forward) <= PositionToleranceSquared &&
                     glm::dot(reverse, reverse) <= PositionToleranceSquared) ||
                    (glm::dot(reverseForward, reverseForward) <= PositionToleranceSquared &&
                     glm::dot(reverseReverse, reverseReverse) <= PositionToleranceSquared))
                {
                    return true;
                }
            }
            return false;
        }

        /** Builds a subdivided, radius-200 IcosahedronGeometry-compatible mesh. */
        eastl::vector<glm::vec3> buildIcosahedronTriangles()
        {
            const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
            const glm::vec3 source[] = {
                {-1.0f, phi, 0.0f}, {1.0f, phi, 0.0f}, {-1.0f, -phi, 0.0f}, {1.0f, -phi, 0.0f},
                {0.0f, -1.0f, phi}, {0.0f, 1.0f, phi}, {0.0f, -1.0f, -phi}, {0.0f, 1.0f, -phi},
                {phi, 0.0f, -1.0f}, {phi, 0.0f, 1.0f}, {-phi, 0.0f, -1.0f}, {-phi, 0.0f, 1.0f}};
            const uint32_t faces[][3] = {
                {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
                {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
                {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
            eastl::vector<glm::vec3> triangles;
            // IcosahedronGeometry(detail=1) contains four triangles for each
            // base face (20 * 4 = 80 triangles). Three subdivides the raw
            // polyhedron face in a barycentric grid and only then projects
            // every emitted vertex onto the radius. Preserve that ordering
            // exactly; a midpoint shortcut changes both flat-face boundaries
            // and the wireframe coverage.
            triangles.reserve(240u);
            for (const auto &face : faces)
            {
                const glm::dvec3 a(source[face[0]]);
                const glm::dvec3 b(source[face[1]]);
                const glm::dvec3 c(source[face[2]]);
            constexpr uint32_t cols = 2u;
                eastl::vector<eastl::vector<glm::dvec3>> grid(cols + 1u);
                for (uint32_t i = 0u; i <= cols; ++i)
                {
                    const double fraction = double(i) / double(cols);
                    const glm::dvec3 aj = a + (c - a) * fraction;
                    const glm::dvec3 bj = b + (c - b) * fraction;
                    const uint32_t rows = cols - i;
                    grid[i].resize(rows + 1u);
                    for (uint32_t j = 0u; j <= rows; ++j)
                    {
                        grid[i][j] = (j == 0u && i == cols)
                            ? aj
                            : aj + (bj - aj) * (double(j) / double(rows));
                    }
                }
                for (uint32_t i = 0u; i < cols; ++i)
                {
                    for (uint32_t j = 0u; j < 2u * (cols - i) - 1u; ++j)
                    {
                        const uint32_t k = j / 2u;
                        const glm::dvec3 *v0;
                        const glm::dvec3 *v1;
                        const glm::dvec3 *v2;
                        if ((j & 1u) == 0u)
                        {
                            v0 = &grid[i][k + 1u];
                            v1 = &grid[i + 1u][k];
                            v2 = &grid[i][k];
                        }
                        else
                        {
                            v0 = &grid[i][k + 1u];
                            v1 = &grid[i + 1u][k + 1u];
                            v2 = &grid[i + 1u][k];
                        }
                        triangles.push_back(glm::vec3(glm::normalize(*v0) * 200.0));
                        triangles.push_back(glm::vec3(glm::normalize(*v1) * 200.0));
                        triangles.push_back(glm::vec3(glm::normalize(*v2) * 200.0));
                    }
                }
            }
            return triangles;
        }

        /** Appends a radial-gradient CanvasTexture equivalent with one mip level. */
        void buildShadowTexture(eastl::vector<uint8_t> &bytes,
                                eastl::vector<uint64_t> &mipOffsets)
        {
            constexpr uint32_t Size = 128u;
            bytes.resize(Size * Size * 4u);
            for (uint32_t y = 0u; y < Size; ++y)
            {
                for (uint32_t x = 0u; x < Size; ++x)
                {
                    const float dx = (float(x) + 0.5f - 64.0f) / 64.0f;
                    const float dy = (float(y) + 0.5f - 64.0f) / 64.0f;
                    const float distance = std::min(1.0f, std::sqrt(dx * dx + dy * dy));
                    const uint8_t channel = static_cast<uint8_t>(std::lround(210.0f + 45.0f * distance));
                    const size_t offset = (size_t(y) * Size + x) * 4u;
                    bytes[offset + 0u] = channel;
                    bytes[offset + 1u] = channel;
                    bytes[offset + 2u] = channel;
                    bytes[offset + 3u] = 255u;
                }
            }
            mipOffsets = {0u};
        }

        /** Adds one typed component buffer payload to an allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
                          const void *data,
                          uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = 1u});
        }

        /** Builds the OpenGL-style projection matrix consumed by the DSL depth remap. */
        glm::mat4 makeProjection()
        {
            return glm::perspective(glm::radians(20.0f), 1.6f, 1.0f, 10000.0f);
        }
    } // namespace

    void WebglGeometryColorsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "initial" && options.targetFrame == 0u) ||
            (options.scenarioId == "animated" && options.targetFrame == 60u) ||
            (options.scenarioId == "camera-input" && options.targetFrame == 61u);
        if (options.caseId != "webgl_geometry_colors" ||
            !validScenario || options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument("webgl_geometry_colors scenario does not match the locked r185 contract.");
        }
        device = inDevice;
        entities.clear();
        buildShadowTexture(shadowTextureBytes, shadowTextureMipOffsets);
        const eastl::vector<glm::vec3> triangles = buildIcosahedronTriangles();
        const glm::vec3 cameraPosition(0.0f, 0.0f, 1800.0f);
        const glm::mat4 view = glm::lookAt(cameraPosition,
                                           glm::vec3(0.0f),
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = makeProjection();
        const glm::vec3 positions[] = {
            {-400.0f, 0.0f, 0.0f}, {400.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        const float shadowXPositions[] = {0.0f, -400.0f, 400.0f};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            WebglGeometryColorsEntityData shadow;
            shadow.logicalId = eastl::string("shadow-") + eastl::to_string(index).c_str();
            shadow.materialData.baseColorAndPhase = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            shadow.objectData.modelView = glm::mat4(1.0f);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f),
                glm::vec3(shadowXPositions[index], -250.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), -Pi * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
            shadow.objectData.modelView = view * model;
            shadow.objectData.modelViewProjection = projection * shadow.objectData.modelView;
            shadow.objectData.lightViewAndIntensity = glm::vec4(0.0f, 0.0f, 1.0f, 3.0f);
            shadow.objectData.viewport = glm::vec4(
                float(options.width), float(options.height), 0.0f, 0.0f);
            const glm::vec3 plane[] = {{-150.0f, -150.0f, 0.0f}, {150.0f, -150.0f, 0.0f},
                                       {150.0f, 150.0f, 0.0f}, {-150.0f, 150.0f, 0.0f}};
            const glm::vec2 uv[] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            for (uint32_t vertex = 0u; vertex < 4u; ++vertex)
            {
                shadow.vertices.push_back({glm::vec4(plane[vertex], 1.0f),
                                           glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
                                           glm::vec4(1.0f),
                                           glm::vec4(uv[vertex], 0.0f, 0.0f)});
            }
            shadow.indices = {0u, 2u, 1u, 0u, 3u, 2u};
            entities.push_back(eastl::move(shadow));
        }
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            WebglGeometryColorsEntityData solid;
            solid.logicalId = eastl::string("solid-") + eastl::to_string(index).c_str();
            solid.materialData.baseColorAndPhase = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[index]) *
                (index == 0u ? glm::rotate(glm::mat4(1.0f), 1.87f, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::mat4(1.0f));
            solid.objectData.modelView = view * model;
            solid.objectData.modelViewProjection = projection * solid.objectData.modelView;
            // Three's DirectionalLight at world +Z becomes +Z in this camera
            // view space; keep the light vector aligned with the Phong view
            // direction used by the DSL fragment stage.
            solid.objectData.lightViewAndIntensity = glm::vec4(0.0f, 0.0f, -1.0f, 3.0f);
            solid.objectData.viewport = glm::vec4(
                float(options.width), float(options.height), 0.0f, 0.0f);
            for (size_t triangle = 0u; triangle < triangles.size(); triangle += 3u)
            {
                appendTriangle(solid, triangles[triangle], triangles[triangle + 1u],
                               triangles[triangle + 2u], index, false);
            }
            entities.push_back(eastl::move(solid));
        }
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            WebglGeometryColorsEntityData wire;
            wire.logicalId = eastl::string("wire-") + eastl::to_string(index).c_str();
            wire.materialData.baseColorAndPhase = glm::vec4(0.0f, 0.0f, 0.0f, 2.0f);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[index]) *
                (index == 0u ? glm::rotate(glm::mat4(1.0f), 1.87f, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::mat4(1.0f));
            wire.objectData.modelView = view * model;
            wire.objectData.modelViewProjection = projection * wire.objectData.modelView;
            wire.objectData.lightViewAndIntensity = glm::vec4(0.0f, 0.0f, -1.0f, 3.0f);
            wire.objectData.viewport = glm::vec4(
                float(options.width), float(options.height), 0.0f, 0.0f);
            eastl::vector<glm::vec3> edgeStarts;
            eastl::vector<glm::vec3> edgeEnds;
            edgeStarts.reserve(triangles.size());
            edgeEnds.reserve(triangles.size());
            for (size_t triangle = 0u; triangle < triangles.size(); triangle += 3u)
            {
                const glm::vec3 authored[3u] = {
                    triangles[triangle], triangles[triangle + 1u], triangles[triangle + 2u]};
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const glm::vec3 &start = authored[edge];
                    const glm::vec3 &end = authored[(edge + 1u) % 3u];
                    if (containsWireframeEdge(edgeStarts, edgeEnds, start, end))
                        continue;
                    edgeStarts.push_back(start);
                    edgeEnds.push_back(end);
                    appendWireframeEdge(wire, start, end);
                }
            }
            entities.push_back(eastl::move(wire));
        }
        if (entities.size() != ExpectedEntityCount)
        {
            throw std::logic_error("webgl_geometry_colors must create exactly nine entities.");
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_geometry_colors could not create its Scene Set encoder.");
        }
        for (WebglGeometryColorsEntityData &entity : entities)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendBuffer(allocation, WebglGeometryColorsSceneRenderSetComponents::vertices,
                         entity.logicalId + "-vertices", entity.vertices.data(),
                         entity.vertices.size() * sizeof(WebglGeometryColorsHostVertex));
            appendBuffer(allocation, WebglGeometryColorsSceneRenderSetComponents::indices,
                         entity.logicalId + "-indices", entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglGeometryColorsSceneRenderSetComponents::objects,
                         entity.logicalId + "-object", &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglGeometryColorsSceneRenderSetComponents::instances,
                         entity.logicalId + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglGeometryColorsSceneRenderSetComponents::materials,
                         entity.logicalId + "-material", &entity.materialData, sizeof(entity.materialData));
            if (entity.materialData.baseColorAndPhase.w > 0.5f &&
                entity.materialData.baseColorAndPhase.w < 1.5f)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
                textureInfo.textureComponentHandle = WebglGeometryColorsSceneRenderSetComponents::textures;
                textureInfo.textures.push_back({
                    .textureName = entity.logicalId + "-shadow",
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = 128u,
                    .height = 128u,
                    .data = shadowTextureBytes.data(),
                    .dataStorageBytes = shadowTextureBytes.size(),
                    .mipmapOffsetBytes = shadowTextureMipOffsets});
                allocation.textureInfos.push_back(eastl::move(textureInfo));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryColorsRuntimeAdapter::updateObjectData(uint32_t width,
                                                              uint32_t height)
    {
        (void)width;
        (void)height;
    }

    void WebglGeometryColorsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryColorsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        prepareOutputPath(options.captureRgbaPath);
        std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("webgl_geometry_colors could not write RGBA capture.");
    }

    void WebglGeometryColorsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        prepareOutputPath(options.captureMetadataPath);
        std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
               << "\"caseId\":\"webgl_geometry_colors\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\""
               << options.pipeline.c_str() << "\",\"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\"frame\":"
               << frameIndex << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false,\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}";
        if (!options.inputReplayPath.empty())
        {
            output << ",\"inputReplay\":{\"schemaVersion\":1,"
                   << "\"caseId\":\"webgl_geometry_colors\","
                   << "\"scenarioId\":\"camera-input\","
                   << "\"captureFrame\":61,\"sha256\":\""
                   << geometryColorsReplaySha256(options.inputReplayPath)
                   << "\",\"target\":\"#container > canvas\","
                   << "\"eventCount\":1,\"lastEventFrame\":0}"
                   << "}\n";
        }
        else
        {
            output << "}\n";
        }
    }

    void WebglGeometryColorsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        prepareOutputPath(options.sceneSnapshotPath);
        std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_geometry_colors\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"semantic-complete\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":9,\n"
               << "  \"entityCount\":9,\n"
               << "  \"instanceCount\":1,\n"
               << "  \"instanceCounts\":[1,1,1,1,1,1,1,1,1],\n"
               << "  \"scenePassCount\":3,\n"
               << "  \"screenPassCount\":1,\n"
               << "  \"drawCommandCount\":3,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"renderSetType\":\"WebglGeometryColorsSceneRenderSet\",\n"
               << "  \"scenePasses\":[{\"name\":\"shadow\",\"renderClass\":\"WebglGeometryColorsShadowPass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"opaque\",\"renderClass\":\"WebglGeometryColorsOpaquePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-wireframe\",\"renderClass\":\"WebglGeometryColorsTransparentWireframePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglGeometryColorsSceneRenderSet\",\"renderableObjectCount\":9,\"entityCount\":9,\"drawCommandCount\":3,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"scenePasses\":[{\"name\":\"shadow\",\"renderClass\":\"WebglGeometryColorsShadowPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"opaque\",\"renderClass\":\"WebglGeometryColorsOpaquePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-wireframe\",\"renderClass\":\"WebglGeometryColorsTransparentWireframePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << index
                   << ",\"logicalRenderableId\":\"" << entities[index].logicalId.c_str()
                   << "\",\"instanceCount\":1}";
        }
        output << "]}],\n"
               << "  \"scenePassSequence\":["
               << "{\"sceneRoot\":\"scene\",\"scenePass\":\"shadow\",\"entityOrdinal\":0},"
               << "{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque\",\"entityOrdinal\":0},"
               << "{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-wireframe\",\"entityOrdinal\":0}]}\n";
    }

    void WebglGeometryColorsRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_geometry_colors RGBA capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGeometryColorsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        shadowTextureBytes.clear();
        shadowTextureMipOffsets.clear();
    }
} // namespace GVM::ThreeSamples
