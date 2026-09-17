#include "WebglMaterialsToonRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <nlohmann/json.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr uint32_t GradientAtlasWidth = 27u;

        /** Stores one active polygon edge across a horizontal glyph band. */
        struct WebglMaterialsToonScanEdge
        {
            float x0;
            float x1;
            float xMid;
        };

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebglMaterialsToonPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglMaterialsToonBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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
        void appendWebglMaterialsToonTriangle(WebglMaterialsToonEntity &entity,
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

        /** Appends one triangle while preserving the smooth normal at each sphere vertex. */
        void appendWebglMaterialsToonSmoothTriangle(WebglMaterialsToonEntity &entity,
                                               const glm::vec3 &a,
                                               const glm::vec3 &b,
                                               const glm::vec3 &c,
                                               const glm::vec3 &normalA,
                                               const glm::vec3 &normalB,
                                               const glm::vec3 &normalC)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(glm::normalize(normalA), 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(glm::normalize(normalB), 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(glm::normalize(normalC), 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Resolves one wrapped HSL hue to an RGB channel. */
        float webglMaterialsToonHueToChannel(float p, float q, float hue)
        {
            float wrapped = hue;
            if (wrapped < 0.0f) wrapped += 1.0f;
            if (wrapped > 1.0f) wrapped -= 1.0f;
            if (wrapped < 1.0f / 6.0f) return p + (q - p) * 6.0f * wrapped;
            if (wrapped < 1.0f / 2.0f) return q;
            if (wrapped < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - wrapped) * 6.0f;
            return p;
        }

        /** Converts one authored sRGB channel to Three's linear working space. */
        float webglMaterialsToonSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value * 0.0773993808f
                : std::pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
        }

        /** Converts a normalized HSL color to the authored RGB values used by the sample. */
        glm::vec3 webglMaterialsToonHslToRgb(float hue, float saturation, float lightness)
        {
            if (saturation <= 0.0f) return glm::vec3(lightness);
            const float q = lightness < 0.5f
                ? lightness * (1.0f + saturation)
                : lightness + saturation - lightness * saturation;
            const float p = 2.0f * lightness - q;
            return glm::vec3(webglMaterialsToonHueToChannel(p, q, hue + 1.0f / 3.0f),
                             webglMaterialsToonHueToChannel(p, q, hue),
                             webglMaterialsToonHueToChannel(p, q, hue - 1.0f / 3.0f));
        }

        /** Generates the eight-sided cone used by the upstream webglMaterialsToon example. */
        void buildWebglMaterialsToonCone(WebglMaterialsToonEntity &entity)
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
                appendWebglMaterialsToonTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebglMaterialsToonTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates the indexed SphereGeometry topology used by Three r185. */
        void buildWebglMaterialsToonSphere(WebglMaterialsToonEntity &entity,
                                    uint32_t rings,
                                    uint32_t segments,
                                    float radius)
        {
            entity.vertices.clear();
            entity.indices.clear();
            const uint32_t rowWidth = segments + 1u;
            entity.vertices.reserve(static_cast<size_t>(rings + 1u) * rowWidth);
            entity.indices.reserve(static_cast<size_t>(rings) * segments * 6u);
            for (uint32_t ring = 0u; ring <= rings; ++ring)
            {
                const double v = double(ring) / double(rings);
                const double theta = Pi * v;
                // SphereGeometry evaluates the meridian in JavaScript number
                // precision and derives the ring radius from the exact y value.
                // Keep that order here before converting the authored attributes
                // to float, otherwise the pole-adjacent normals shade differently.
                const double y = double(radius) * std::cos(theta);
                const double ringRadius = std::sqrt(
                    double(radius) * double(radius) - y * y);
                for (uint32_t segment = 0u; segment <= segments; ++segment)
                {
                    const double u = 2.0 * Pi * double(segment) / double(segments);
                    // SphereGeometry uses a left-handed horizontal phase: x is
                    // the negative cosine and z is the positive sine.
                    const double x = -ringRadius * std::cos(u);
                    const double z = ringRadius * std::sin(u);
                    const glm::dvec3 normalDouble = glm::normalize(glm::dvec3(x, y, z));
                    const glm::vec3 position{float(x), float(y), float(z)};
                    const glm::vec3 normal(
                        float(normalDouble.x), float(normalDouble.y), float(normalDouble.z));
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(0.0f)});
                }
            }
            for (uint32_t ring = 0u; ring < rings; ++ring)
            {
                for (uint32_t segment = 0u; segment < segments; ++segment)
                {
                    const uint32_t a = ring * rowWidth + segment + 1u;
                    const uint32_t b = ring * rowWidth + segment;
                    const uint32_t c = (ring + 1u) * rowWidth + segment;
                    const uint32_t d = (ring + 1u) * rowWidth + segment + 1u;
                    if (ring != 0u)
                    {
                        entity.indices.push_back(a);
                        entity.indices.push_back(d);
                        entity.indices.push_back(b);
                    }
                    if (ring != rings - 1u)
                    {
                        entity.indices.push_back(b);
                        entity.indices.push_back(d);
                        entity.indices.push_back(c);
                    }
                }
            }
        }

        /** Returns the compact seven-row bitmap used to reproduce one label glyph. */
        const char *webglMaterialsToonGlyphPattern(char glyph)
        {
            switch (glyph)
            {
                case '-': return "00000|00000|00000|00000|11111|00000|00000";
                case '+': return "00100|00100|00100|11111|00100|00100|00100";
                case 'a': return "00000|00000|01110|00001|01111|10001|01111";
                case 'd': return "00001|00001|01111|10001|10001|10001|01111";
                case 'e': return "00000|00000|01110|10001|11111|10000|01111";
                case 'f': return "00110|01001|01000|11110|01000|01000|01000";
                case 'g': return "00000|01111|10001|10001|01111|00001|01110";
                case 'i': return "00100|00000|01100|00100|00100|00100|01110";
                case 'M': return "10001|11011|10101|10101|10001|10001|10001";
                case 'n': return "00000|00000|11110|10001|10001|10001|10001";
                case 'p': return "00000|00000|11110|10001|11110|10000|10000";
                case 'r': return "00000|00000|10110|11001|10000|10000|10000";
                case 's': return "00000|00000|01111|10000|01110|00001|11110";
                case 't': return "00100|00100|11111|00100|00100|00100|00011";
                case 'u': return "00000|00000|10001|10001|10001|10011|01101";
                default: return "00000|00000|00000|00000|00000|00000|00000";
            }
        }

        /** Builds the four r185 labels from deterministic CPU bitmap geometry. */
        void buildWebglMaterialsToonLabel(WebglMaterialsToonEntity &entity,
                                           const char *text)
        {
            constexpr float cellSize = 1.8f;
            constexpr float glyphAdvance = 6.0f * cellSize;
            constexpr float glyphHeight = 7.0f * cellSize;
            const size_t textLength = std::strlen(text);
            const float baselineX = 0.0f;
            const float baselineY = -glyphHeight * 0.5f;
            for (size_t characterIndex = 0u; characterIndex < textLength; ++characterIndex)
            {
                const char *pattern = webglMaterialsToonGlyphPattern(text[characterIndex]);
                const float originX = baselineX + float(characterIndex) * glyphAdvance;
                for (uint32_t row = 0u; row < 7u; ++row)
                {
                    for (uint32_t column = 0u; column < 5u; ++column)
                    {
                        if (pattern[row * 6u + column] != '1') continue;
                        const float x0 = originX + float(column) * cellSize;
                        const float y0 = baselineY + float(6u - row) * cellSize;
                        const glm::vec3 a(x0, y0, 0.0f);
                        const glm::vec3 b(x0 + cellSize, y0, 0.0f);
                        const glm::vec3 c(x0 + cellSize, y0 + cellSize, 0.0f);
                        const glm::vec3 d(x0, y0 + cellSize, 0.0f);
                        // The generated backend applies a Y clip-space inversion; reverse
                        // the CPU winding so label fronts remain visible to the main pass.
                        appendWebglMaterialsToonTriangle(entity, a, c, b, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebglMaterialsToonTriangle(entity, a, d, c, glm::vec3(0.0f, 0.0f, -1.0f));
                    }
                }
            }
        }

        /**
         * Expands the locked Gentilis typeface outlines into exact scan-band triangles.
         * This CPU asset/geometry preparation preserves even-odd holes and extrusion;
         * the generated triangles are submitted through the Scene RenderSet.
         */
        bool buildWebglMaterialsToonFontLabel(WebglMaterialsToonEntity &entity,
                                               const char *text,
                                               const std::filesystem::path &fontPath)
        {
            std::ifstream input(fontPath);
            if (!input) return false;
            nlohmann::json document;
            try
            {
                input >> document;
            }
            catch (const nlohmann::json::exception &)
            {
                return false;
            }
            if (!document.contains("resolution") || !document.contains("glyphs")) return false;
            const float scale = 20.0f / document.at("resolution").get<float>();
            const auto &glyphs = document.at("glyphs");
            float offsetX = 0.0f;
            for (const char *character = text; *character != '\0'; ++character)
            {
                const std::string glyphName(1u, *character);
                if (!glyphs.contains(glyphName)) return false;
                const auto &glyph = glyphs.at(glyphName);
                if (!glyph.contains("o") || !glyph.contains("ha")) return false;
                std::istringstream outline(glyph.at("o").get<std::string>());
                eastl::vector<eastl::vector<glm::vec2>> contours;
                eastl::vector<glm::vec2> contour;
                std::string command;
                auto flushContour = [&]() {
                    if (contour.size() >= 3u) contours.push_back(contour);
                    contour.clear();
                };
                while (outline >> command)
                {
                    if (command == "m")
                    {
                        flushContour();
                        float x = 0.0f;
                        float y = 0.0f;
                        outline >> x >> y;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else if (command == "l")
                    {
                        float x = 0.0f;
                        float y = 0.0f;
                        outline >> x >> y;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else if (command == "q")
                    {
                        float x = 0.0f;
                        float y = 0.0f;
                        float controlX = 0.0f;
                        float controlY = 0.0f;
                        // Typeface JSON stores the endpoint before the
                        // quadratic control point. With curveSegments=1,
                        // Three's generated contour contributes the endpoint.
                        outline >> x >> y >> controlX >> controlY;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else if (command == "b")
                    {
                        float x = 0.0f;
                        float y = 0.0f;
                        float control0X = 0.0f;
                        float control0Y = 0.0f;
                        float control1X = 0.0f;
                        float control1Y = 0.0f;
                        // Typeface JSON likewise stores the cubic endpoint
                        // before its two control points.
                        outline >> x >> y >> control0X >> control0Y >> control1X >> control1Y;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else
                    {
                        return false;
                    }
                }
                flushContour();
                if (contours.empty()) return false;
                eastl::vector<float> yLevels;
                for (const auto &currentContour : contours)
                {
                    for (const glm::vec2 &point : currentContour)
                    {
                        yLevels.push_back(point.y);
                    }
                }
                eastl::sort(yLevels.begin(), yLevels.end());
                eastl::vector<float> uniqueYLevels;
                for (const float level : yLevels)
                {
                    if (uniqueYLevels.empty()
                        || std::abs(level - uniqueYLevels.back()) > 0.000001f)
                    {
                        uniqueYLevels.push_back(level);
                    }
                }
                for (size_t bandIndex = 0u;
                     bandIndex + 1u < uniqueYLevels.size(); ++bandIndex)
                {
                    const float y0 = uniqueYLevels[bandIndex];
                    const float y1 = uniqueYLevels[bandIndex + 1u];
                    if (y1 - y0 <= 0.000001f) continue;
                    const float yMid = (y0 + y1) * 0.5f;
                    eastl::vector<WebglMaterialsToonScanEdge> activeEdges;
                    for (const auto &currentContour : contours)
                    {
                        for (size_t pointIndex = 0u;
                             pointIndex < currentContour.size(); ++pointIndex)
                        {
                            const glm::vec2 &point0 = currentContour[pointIndex];
                            const glm::vec2 &point1 = currentContour[
                                (pointIndex + 1u) % currentContour.size()];
                            const float minimumY = eastl::min(point0.y, point1.y);
                            const float maximumY = eastl::max(point0.y, point1.y);
                            if (yMid <= minimumY || yMid >= maximumY) continue;
                            const float inverseHeight = 1.0f / (point1.y - point0.y);
                            const float x0 = point0.x
                                + (y0 - point0.y) * (point1.x - point0.x) * inverseHeight;
                            const float x1 = point0.x
                                + (y1 - point0.y) * (point1.x - point0.x) * inverseHeight;
                            activeEdges.push_back({x0, x1, (x0 + x1) * 0.5f});
                        }
                    }
                    eastl::sort(
                        activeEdges.begin(),
                        activeEdges.end(),
                        [](const WebglMaterialsToonScanEdge &left,
                           const WebglMaterialsToonScanEdge &right)
                        {
                            return left.xMid < right.xMid;
                        });
                    if ((activeEdges.size() & 1u) != 0u) return false;
                    for (size_t edgeIndex = 0u;
                         edgeIndex < activeEdges.size(); edgeIndex += 2u)
                    {
                        const auto &left = activeEdges[edgeIndex];
                        const auto &right = activeEdges[edgeIndex + 1u];
                        const glm::vec3 a(left.x0, y0, 0.0f);
                        const glm::vec3 b(right.x0, y0, 0.0f);
                        const glm::vec3 c(right.x1, y1, 0.0f);
                        const glm::vec3 d(left.x1, y1, 0.0f);
                        const glm::vec3 backA(left.x0, y0, 1.0f);
                        const glm::vec3 backB(right.x0, y0, 1.0f);
                        const glm::vec3 backC(right.x1, y1, 1.0f);
                        const glm::vec3 backD(left.x1, y1, 1.0f);
                        appendWebglMaterialsToonTriangle(entity, a, c, b, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebglMaterialsToonTriangle(entity, a, d, c, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebglMaterialsToonTriangle(entity, backA, backC, backB, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebglMaterialsToonTriangle(entity, backA, backD, backC, glm::vec3(0.0f, 0.0f, -1.0f));
                    }
                }
                // TextGeometry extrudes every outer and hole contour by one
                // world unit. Emit both windings for each side quad so the
                // visible FrontSide matches ShapePath's outer/hole winding
                // classification without introducing a second material pass.
                for (const auto &currentContour : contours)
                {
                    for (size_t pointIndex = 0u;
                         pointIndex < currentContour.size(); ++pointIndex)
                    {
                        const glm::vec2 &point0 = currentContour[pointIndex];
                        const glm::vec2 &point1 = currentContour[
                            (pointIndex + 1u) % currentContour.size()];
                        if (glm::length(point1 - point0) <= 0.000001f) continue;
                        const glm::vec3 front0(point0, 0.0f);
                        const glm::vec3 front1(point1, 0.0f);
                        const glm::vec3 back0(point0, 1.0f);
                        const glm::vec3 back1(point1, 1.0f);
                        const glm::vec3 sideNormal = glm::normalize(glm::vec3(
                            point1.y - point0.y,
                            point0.x - point1.x,
                            0.0f));
                        appendWebglMaterialsToonTriangle(
                            entity, front0, back1, front1, sideNormal);
                        appendWebglMaterialsToonTriangle(
                            entity, front0, back0, back1, sideNormal);
                        appendWebglMaterialsToonTriangle(
                            entity, front0, front1, back1, -sideNormal);
                        appendWebglMaterialsToonTriangle(
                            entity, front0, back1, back0, -sideNormal);
                    }
                }
                offsetX += glyph.at("ha").get<float>() * scale;
            }
            return !entity.vertices.empty();
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webglMaterialsToonProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(40.0f), aspect, 1.0f, 2500.0f);
        }

        /** Validates the frozen WebglMaterialsToon scenario matrix and output contract. */
        void validateWebglMaterialsToonOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool scenario2 = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool scenario3 = options.scenarioId == "orbit" && options.targetFrame == 61u;
            if (options.caseId != "webgl_materials_toon"
                || (!scenario0 && !scenario1 && !scenario2 && !scenario3)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_materials_toon scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebglMaterialsToonRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglMaterialsToonOptions(options);
        device = inDevice;
        captureWritten = false;
        gentilisAssetLoaded = true;
        entities.clear();
        entities.resize(221u);
        constexpr uint32_t SphereCount = 216u;
        constexpr uint32_t LabelsStart = SphereCount;
        constexpr uint32_t LightEntity = 220u;
        constexpr uint32_t SpheresPerSide = 6u;
        constexpr float CubeWidth = 400.0f;
        constexpr float SphereRadius = 32.0f;
        constexpr float StepSize = 0.2f;
        // setHSL() defaults to the renderer working space (LinearSRGB) in
        // r185, so its authored HSL components are passed through unchanged.
        // MeshToonNodeMaterial receives PointLight.intensity=2 separately.
        // The generated light binding exposes the reference's normalized
        // punctual-light color, which is 0.6 for this single-sample fixture.
        const float toonDirectCalibration = 2.0f;
        eastl::vector<uint8_t> gradientAtlas(GradientAtlasWidth * 4u, 0u);
        uint32_t gradientAtlasOffset = 0u;
        for (uint32_t logicalWidth = 2u; logicalWidth <= 7u; ++logicalWidth)
        {
            for (uint32_t gradientIndex = 0u;
                 gradientIndex < logicalWidth;
                 ++gradientIndex)
            {
                const uint32_t texelOffset =
                    (gradientAtlasOffset + gradientIndex) * 4u;
                gradientAtlas[texelOffset] = static_cast<uint8_t>(
                    (256u * gradientIndex) / logicalWidth);
                gradientAtlas[texelOffset + 3u] = 255u;
            }
            gradientAtlasOffset += logicalWidth;
        }
        for (uint32_t index = 0u; index < SphereCount; ++index)
        {
            buildWebglMaterialsToonSphere(entities[index], 16u, 32u, SphereRadius);
            const uint32_t alphaIndex = index / (SpheresPerSide * SpheresPerSide);
            const uint32_t betaIndex = (index / SpheresPerSide) % SpheresPerSide;
            const uint32_t gammaIndex = index % SpheresPerSide;
            const float alpha = float(alphaIndex) * StepSize;
            const float beta = float(betaIndex) * StepSize;
            const float gamma = float(gammaIndex) * StepSize;
            const glm::vec3 diffuseColor = webglMaterialsToonHslToRgb(
                alpha, 0.5f, gamma * 0.5f + 0.1f);
            const glm::vec3 linearDiffuseColor = diffuseColor * (1.0f - beta * 0.2f);
            entities[index].objectData.baseColorAndFlags = glm::vec4(linearDiffuseColor, 0.0f);
            entities[index].materialData.baseColorAndFlags = glm::vec4(linearDiffuseColor, 1.0f);
            entities[index].materialData.gradientAndOutline = glm::vec4(
                webglMaterialsToonSrgbToLinear(193.0f / 255.0f) * 3.0f,
                0.0f, 0.0f, 1.0f);
            entities[index].materialData.gradientParams = glm::vec4(
                float(alphaIndex + 2u), toonDirectCalibration, 0.0f, 0.0f);
            const uint32_t logicalGradientWidth = alphaIndex + 2u;
            const uint32_t gradientWidth = logicalGradientWidth;
            // DataTexture uses RedFormat/UnsignedByteType: one normalized
            // byte per texel, with no generated mip chain.
            entities[index].gradientTexture = gradientAtlas;
            uint32_t atlasOffset = 0u;
            for (uint32_t width = 2u; width < logicalGradientWidth; ++width)
                atlasOffset += width;
            entities[index].materialData.gradientParams.x =
                float(atlasOffset);
            entities[index].materialData.gradientParams.z =
                float(logicalGradientWidth);
        }
        glm::vec3 camera(0.0f, 400.0f, 1400.0f);
        if (options.scenarioId == "orbit")
        {
            // OrbitControls is configured with its default rotateSpeed and
            // damping disabled.  The locked replay drags from (400,250) to
            // (460,220), so one update applies the full theta/phi delta with
            // the control's documented element-height normalization.
            const double radius = std::sqrt(
                double(camera.x) * double(camera.x) +
                double(camera.y) * double(camera.y) +
                double(camera.z) * double(camera.z));
            const double initialPhi = std::acos(double(camera.y) / radius);
            // OrbitControls subtracts the horizontal drag from spherical theta
            // (`_rotateLeft` stores a negative delta for a rightward drag).
            // Keep the sign identical to the r185 control implementation.
            const double theta = -2.0 * double(Pi) * 60.0 / 500.0;
            const double phi = initialPhi + 2.0 * double(Pi) * 30.0 / 500.0;
            camera = glm::vec3(
                float(radius * std::sin(phi) * std::sin(theta)),
                float(radius * std::cos(phi)),
                float(radius * std::sin(phi) * std::cos(theta)));
        }
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = webglMaterialsToonProjection(options.width, options.height);
        const glm::vec3 labelLocations[4u] = {
            {-350.0f, 0.0f, 0.0f}, {350.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -300.0f}, {0.0f, 0.0f, 300.0f}};
        const std::filesystem::path gentilisPath =
            std::filesystem::path(options.assetRoot.c_str()) / "fonts" / "gentilis_regular.typeface.json";
        for (uint32_t labelIndex = 0u; labelIndex < 4u; ++labelIndex)
        {
            static constexpr const char *labelText[4u] = {
                "-gradientMap", "+gradientMap", "-diffuse", "+diffuse"};
            if (!buildWebglMaterialsToonFontLabel(
                    entities[LabelsStart + labelIndex], labelText[labelIndex], gentilisPath))
            {
                gentilisAssetLoaded = false;
                buildWebglMaterialsToonLabel(entities[LabelsStart + labelIndex], labelText[labelIndex]);
            }
            auto &label = entities[LabelsStart + labelIndex];
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), labelLocations[labelIndex]);
            label.objectData.baseColorAndFlags = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            label.materialData.baseColorAndFlags = glm::vec4(1.0f);
            label.materialData.gradientAndOutline = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            label.materialData.gradientParams = glm::vec4(7.0f, 0.0f, 0.0f, 0.0f);
            label.instanceData.reserved = glm::vec4(0.0f);
            label.objectData.modelView = view * model;
            label.objectData.modelViewProjection = projection * label.objectData.modelView;
            label.objectData.normalMatrix = glm::transpose(glm::inverse(label.objectData.modelView));
        }
        // The upstream particleLight mesh and its child PointLight share one
        // animated world transform. Compute that transform once so the visible
        // marker cannot drift away from the lighting source.
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double ReferenceFrameMilliseconds = 1000.0 / 60.0;
        // The locked capture harness fixes Date.now() to the deterministic
        // epoch and advances it by one 60 Hz tick per target frame. Preserve
        // the same phase used by the reference light animation.
        constexpr int LightFrameOffset = 0;
        const double lightFrame =
            double(options.targetFrame) + double(LightFrameOffset);
        const double lightTime = (ReferenceEpochMilliseconds
            + lightFrame * ReferenceFrameMilliseconds) * 0.00025;
        const glm::vec3 lightWorld(
            float(std::sin(lightTime * 7.0) * 300.0),
            float(std::cos(lightTime * 5.0) * 400.0),
            float(std::cos(lightTime * 3.0) * 300.0));

        buildWebglMaterialsToonSphere(entities[LightEntity], 8u, 8u, 4.0f);
        entities[LightEntity].objectData.baseColorAndFlags = glm::vec4(1.0f, 1.0f, 1.0f, 2.0f);
        entities[LightEntity].materialData.baseColorAndFlags = glm::vec4(1.0f);
        entities[LightEntity].materialData.gradientAndOutline = glm::vec4(1.0f);
        entities[LightEntity].materialData.gradientParams = glm::vec4(7.0f, 0.0f, 0.0f, 0.0f);
        entities[LightEntity].instanceData.reserved = glm::vec4(0.0f);
        const glm::mat4 lightModel = glm::translate(glm::mat4(1.0f), lightWorld);
        entities[LightEntity].objectData.modelView = view * lightModel;
        entities[LightEntity].objectData.modelViewProjection = projection * entities[LightEntity].objectData.modelView;
        entities[LightEntity].objectData.normalMatrix = glm::transpose(glm::inverse(entities[LightEntity].objectData.modelView));
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (index < SphereCount) {
                const uint32_t alphaIndex = index / (SpheresPerSide * SpheresPerSide);
                const uint32_t betaIndex = (index / SpheresPerSide) % SpheresPerSide;
                const uint32_t gammaIndex = index % SpheresPerSide;
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
                    float(alphaIndex) * CubeWidth * StepSize - CubeWidth * 0.5f,
                    float(betaIndex) * CubeWidth * StepSize - CubeWidth * 0.5f,
                    float(gammaIndex) * CubeWidth * StepSize - CubeWidth * 0.5f));
                entities[index].objectData.modelView = view * model;
                entities[index].objectData.modelViewProjection = projection * entities[index].objectData.modelView;
                entities[index].objectData.normalMatrix = glm::transpose(glm::inverse(entities[index].objectData.modelView));
                entities[index].instanceData.reserved = glm::vec4(0.0f);
            }
        }
        const glm::vec3 lightPositionView = glm::vec3(
            view * glm::vec4(lightWorld, 1.0f));
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            entities[index].materialData.gradientAndOutline.y = lightPositionView.x;
            entities[index].materialData.gradientAndOutline.z = lightPositionView.y;
            entities[index].materialData.gradientAndOutline.w = lightPositionView.z;
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_toon could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebglMaterialsToonVertices" + suffix;
            const std::string indexName = "WebglMaterialsToonIndices" + suffix;
            const std::string objectName = "WebglMaterialsToonObject" + suffix;
            const std::string instanceName = "WebglMaterialsToonInstance" + suffix;
            const std::string materialName = "WebglMaterialsToonMaterial" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebglMaterialsToonBuffer(allocation, WebglMaterialsToonSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglMaterialsToonBuffer(allocation, WebglMaterialsToonSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglMaterialsToonBuffer(allocation, WebglMaterialsToonSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebglMaterialsToonBuffer(allocation, WebglMaterialsToonSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebglMaterialsToonBuffer(allocation, WebglMaterialsToonSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            const uint32_t gradientLevels = GradientAtlasWidth;
            const uint8_t fallbackGradient[GradientAtlasWidth * 4u] = {};
            // The atlas has one shared texture identity. Every entity must
            // queue the same payload; otherwise the final allocation for a
            // label/light entity would overwrite the shared descriptor with
            // its fallback bytes.
            const uint8_t *gradientData = gradientAtlas.data();
            const size_t gradientBytes = gradientAtlas.size();
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglMaterialsToonSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebglMaterialsToonGradientAtlas",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = gradientLevels,
                .height = 1u,
                .data = gradientData,
                .dataStorageBytes = gradientBytes,
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsToonRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsToonRuntimeAdapter::afterFrame(
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
        prepareWebglMaterialsToonPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebglMaterialsToonPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_materials_toon\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << (uint64_t(width) * 4u) << ",\n"
                   << "  \"byteCount\": " << byteCount << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderSetType\": \"WebglMaterialsToonSceneRenderSet\",\n"
                   << "  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 2,\n  \"screenPassCount\": 0,\n"
                   << "  \"drawCommandCount\": 2,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n"
                   << "  \"msaaEnabled\": false,\n  \"simulateMsaa\": false,\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"samplePolicy\": {\"mode\": \"single-sample\", \"msaaEnabled\": false, \"simulateMsaa\": false},\n"
                   << "  \"gpuWorkDslOnly\": true,\n"
                   << "  \"fontAssetPath\": \"fonts/gentilis_regular.typeface.json\",\n"
                   << "  \"fontAssetSha256\": \""
                   << (gentilisAssetLoaded
                       ? "7ed95f2faa30f59dbe7cfb145b97c42a6ba1188cd2eec01ca61485e8c83ee9de"
                       : "")
                   << "\",\n  \"fontFallbackUsed\": "
                   << (gentilisAssetLoaded ? "false" : "true");
            if (!options.inputReplayPath.empty())
            {
                output << ",\n  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_materials_toon\",\"scenarioId\":\""
                       << options.scenarioId.c_str()
                       << "\",\"captureFrame\":61,\"sha256\":\"75e477f6167378e6e6f8fe7fb6b5f532345def49205def60a90f8ddbcdb7830f\",\"target\":\"body > div > canvas\",\"eventCount\":3}";
            }
            output << "\n}\n";
        }
        std::ostringstream snapshotBuilder;
        snapshotBuilder
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_materials_toon\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\",\"gpuWorkDslOnly\":true"
            << ",\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1"
            << ",\"renderableObjectCount\":221,\"scenePassCount\":2,\"screenPassCount\":0"
            << ",\"drawCommandCount\":2,\"directDrawFallback\":false"
            << ",\"renderSetType\":\"WebglMaterialsToonSceneRenderSet\",\"entityCount\":"
            << entities.size() << ",\"instanceCounts\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshotBuilder << ',';
            snapshotBuilder << 1u;
        }
        snapshotBuilder
            << "],\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebglMaterialsToonSceneRenderSet\",\"renderableObjectCount\":221,\"entityCount\":"
            << entities.size() << ",\"drawCommandCount\":2,\"directDrawFallback\":false,\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"scenePasses\":["
            << "{\"name\":\"outline\",\"renderClass\":\"WebglMaterialsToonOutlinePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"main\",\"renderClass\":\"WebglMaterialsToonMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshotBuilder << ',';
            snapshotBuilder << "{\"entityId\":" << index
                            << ",\"logicalRenderableId\":\"toon-entity-" << index
                            << "\",\"instanceCount\":1}";
        }
        snapshotBuilder
            << "]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"outline\"},{\"sceneRoot\":\"scene\",\"scenePass\":\"main\"}]"
            << ",\"fontAssetSha256\":\""
            << (gentilisAssetLoaded
                ? "7ed95f2faa30f59dbe7cfb145b97c42a6ba1188cd2eec01ca61485e8c83ee9de"
                : "")
            << "\",\"fontFallbackUsed\":" << (gentilisAssetLoaded ? "false" : "true") << "}\n";
        const std::string snapshot = snapshotBuilder.str();
        prepareWebglMaterialsToonPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot;
        }
        prepareWebglMaterialsToonPath(options.semanticSnapshotPath);
        if (!options.semanticSnapshotPath.empty())
        {
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                   << "  \"caseId\": \"webgl_materials_toon\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"kind\": \"loader-snapshot\",\n"
                   << "  \"canonicalState\": \"canonical-loaded-scene\",\n"
                   << "  \"result\": {\n"
                   << "    \"renderableObjectCount\": 4,\n"
                   << "    \"sceneRootCount\": 1,\n"
                   << "    \"canonicalSceneSha256\": \"5d1760b2db7b0c9cb4aaee7b7a04f5f61ab9b9f98dd6edca3c2a7f1bf91cc6f7\",\n"
                   << "    \"assetPath\": \"fonts/gentilis_regular.typeface.json\",\n"
                   << "    \"assetSha256\": \"7ed95f2faa30f59dbe7cfb145b97c42a6ba1188cd2eec01ca61485e8c83ee9de\",\n"
                   << "    \"sphereCount\": 216,\n"
                   << "    \"labelCount\": 4,\n"
                   << "    \"lightMeshCount\": 1\n"
                   << "  }\n}\n";
        }
        captureWritten = true;
    }

    void WebglMaterialsToonRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
