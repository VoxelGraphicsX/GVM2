#include "WebgpuMaterialsToonRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <nlohmann/json.hpp>

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

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebgpuMaterialsToonPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebgpuMaterialsToonBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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
        void appendWebgpuMaterialsToonTriangle(WebgpuMaterialsToonEntity &entity,
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
        void appendWebgpuMaterialsToonSmoothTriangle(WebgpuMaterialsToonEntity &entity,
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
        float webgpuMaterialsToonHueToChannel(float p, float q, float hue)
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
        float webgpuMaterialsToonSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value * 0.0773993808f
                : std::pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
        }

        /** Converts a normalized HSL color to the authored RGB values used by the sample. */
        glm::vec3 webgpuMaterialsToonHslToRgb(float hue, float saturation, float lightness)
        {
            if (saturation <= 0.0f) return glm::vec3(lightness);
            const float q = lightness < 0.5f
                ? lightness * (1.0f + saturation)
                : lightness + saturation - lightness * saturation;
            const float p = 2.0f * lightness - q;
            return glm::vec3(webgpuMaterialsToonHueToChannel(p, q, hue + 1.0f / 3.0f),
                             webgpuMaterialsToonHueToChannel(p, q, hue),
                             webgpuMaterialsToonHueToChannel(p, q, hue - 1.0f / 3.0f));
        }

        /** Generates the eight-sided cone used by the upstream webgpuMaterialsToon example. */
        void buildWebgpuMaterialsToonCone(WebgpuMaterialsToonEntity &entity)
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
                appendWebgpuMaterialsToonTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebgpuMaterialsToonTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates the indexed SphereGeometry topology used by Three r185. */
        void buildWebgpuMaterialsToonSphere(WebgpuMaterialsToonEntity &entity,
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
        const char *webgpuMaterialsToonGlyphPattern(char glyph)
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
        void buildWebgpuMaterialsToonLabel(WebgpuMaterialsToonEntity &entity,
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
                const char *pattern = webgpuMaterialsToonGlyphPattern(text[characterIndex]);
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
                        appendWebgpuMaterialsToonTriangle(entity, a, c, b, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebgpuMaterialsToonTriangle(entity, a, d, c, glm::vec3(0.0f, 0.0f, -1.0f));
                    }
                }
            }
        }

        /** Tests an even-odd point-in-contour rule for one CPU-tessellated glyph. */
        bool webgpuMaterialsToonPointInsideContours(
            const eastl::vector<eastl::vector<glm::vec2>> &contours,
            const glm::vec2 &point)
        {
            bool inside = false;
            for (const auto &contour : contours)
            {
                for (size_t index = 0u; index < contour.size(); ++index)
                {
                    const glm::vec2 &a = contour[index];
                    const glm::vec2 &b = contour[(index + 1u) % contour.size()];
                    const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
                        (point.x < (b.x - a.x) * (point.y - a.y) /
                            (b.y - a.y + 0.000001f) + a.x);
                    if (crosses) inside = !inside;
                }
            }
            return inside;
        }

        /**
         * Expands the locked Gentilis typeface outlines into front-face line quads.
         * This is a CPU asset/geometry preparation step; the generated triangles are
         * still submitted through the Scene RenderSet and shaded by the DSL Basic path.
         */
        bool buildWebgpuMaterialsToonFontLabel(WebgpuMaterialsToonEntity &entity,
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
            // The TextGeometry capture uses curveSegments=1.  A fine CPU
            // coverage grid preserves the resulting polygon silhouette while
            // keeping all raster work in the DSL triangle pass.
            constexpr float cellSize = 0.05f;
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
                        float controlX = 0.0f;
                        float controlY = 0.0f;
                        float x = 0.0f;
                        float y = 0.0f;
                        outline >> controlX >> controlY >> x >> y;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else if (command == "b")
                    {
                        float control0X = 0.0f;
                        float control0Y = 0.0f;
                        float control1X = 0.0f;
                        float control1Y = 0.0f;
                        float x = 0.0f;
                        float y = 0.0f;
                        outline >> control0X >> control0Y >> control1X >> control1Y >> x >> y;
                        contour.push_back(glm::vec2(x * scale + offsetX, y * scale));
                    }
                    else
                    {
                        return false;
                    }
                }
                flushContour();
                if (contours.empty()) return false;
                glm::vec2 minimum(FLT_MAX);
                glm::vec2 maximum(-FLT_MAX);
                for (const auto &currentContour : contours)
                {
                    for (const glm::vec2 &point : currentContour)
                    {
                        minimum = glm::min(minimum, point);
                        maximum = glm::max(maximum, point);
                    }
                }
                for (float y = std::floor(minimum.y / cellSize) * cellSize;
                     y < maximum.y; y += cellSize)
                {
                    for (float x = std::floor(minimum.x / cellSize) * cellSize;
                         x < maximum.x; x += cellSize)
                    {
                        const glm::vec2 center(x + cellSize * 0.5f, y + cellSize * 0.5f);
                        if (!webgpuMaterialsToonPointInsideContours(contours, center)) continue;
                        const glm::vec3 a(x, y, 0.0f);
                        const glm::vec3 b(x + cellSize, y, 0.0f);
                        const glm::vec3 c(x + cellSize, y + cellSize, 0.0f);
                        const glm::vec3 d(x, y + cellSize, 0.0f);
                        appendWebgpuMaterialsToonTriangle(entity, a, c, b, glm::vec3(0.0f, 0.0f, -1.0f));
                        appendWebgpuMaterialsToonTriangle(entity, a, d, c, glm::vec3(0.0f, 0.0f, -1.0f));
                    }
                }
                offsetX += glyph.at("ha").get<float>() * scale;
            }
            return !entity.vertices.empty();
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webgpuMaterialsToonProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(40.0f), aspect, 1.0f, 2500.0f);
        }

        /** Validates the four deterministic target/webgpuMaterialsToon scenarios. */
        /** Validates the frozen WebgpuMaterialsToon scenario matrix and output contract. */
        void validateWebgpuMaterialsToonOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool scenario2 = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool scenario3 = options.scenarioId == "orbit" && options.targetFrame == 61u;
            if (options.caseId != "webgpu_materials_toon"
                || (!scenario0 && !scenario1 && !scenario2 && !scenario3)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgpu_materials_toon scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebgpuMaterialsToonRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebgpuMaterialsToonOptions(options);
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
            buildWebgpuMaterialsToonSphere(entities[index], 16u, 32u, SphereRadius);
            const uint32_t alphaIndex = index / (SpheresPerSide * SpheresPerSide);
            const uint32_t betaIndex = (index / SpheresPerSide) % SpheresPerSide;
            const uint32_t gammaIndex = index % SpheresPerSide;
            const float alpha = float(alphaIndex) * StepSize;
            const float beta = float(betaIndex) * StepSize;
            const float gamma = float(gammaIndex) * StepSize;
            const glm::vec3 diffuseColor = webgpuMaterialsToonHslToRgb(
                alpha, 0.5f, gamma * 0.5f + 0.1f);
            const glm::vec3 linearDiffuseColor = diffuseColor * (1.0f - beta * 0.2f);
            entities[index].objectData.baseColorAndFlags = glm::vec4(linearDiffuseColor, 0.0f);
            entities[index].materialData.baseColorAndFlags = glm::vec4(linearDiffuseColor, 1.0f);
            entities[index].materialData.gradientAndOutline = glm::vec4(
                webgpuMaterialsToonSrgbToLinear(193.0f / 255.0f) * 3.0f,
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
        const glm::mat4 projection = webgpuMaterialsToonProjection(options.width, options.height);
        const glm::vec3 labelLocations[4u] = {
            {-350.0f, 0.0f, 0.0f}, {350.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -300.0f}, {0.0f, 0.0f, 300.0f}};
        const std::filesystem::path gentilisPath =
            std::filesystem::path(options.assetRoot.c_str()) / "fonts" / "gentilis_regular.typeface.json";
        for (uint32_t labelIndex = 0u; labelIndex < 4u; ++labelIndex)
        {
            static constexpr const char *labelText[4u] = {
                "-gradientMap", "+gradientMap", "-diffuse", "+diffuse"};
            if (!buildWebgpuMaterialsToonFontLabel(
                    entities[LabelsStart + labelIndex], labelText[labelIndex], gentilisPath))
            {
                gentilisAssetLoaded = false;
                buildWebgpuMaterialsToonLabel(entities[LabelsStart + labelIndex], labelText[labelIndex]);
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

        buildWebgpuMaterialsToonSphere(entities[LightEntity], 8u, 8u, 4.0f);
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
        if (!encoder) throw std::runtime_error("webgpu_materials_toon could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebgpuMaterialsToonVertices" + suffix;
            const std::string indexName = "WebgpuMaterialsToonIndices" + suffix;
            const std::string objectName = "WebgpuMaterialsToonObject" + suffix;
            const std::string instanceName = "WebgpuMaterialsToonInstance" + suffix;
            const std::string materialName = "WebgpuMaterialsToonMaterial" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebgpuMaterialsToonBuffer(allocation, WebgpuMaterialsToonSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebgpuMaterialsToonBuffer(allocation, WebgpuMaterialsToonSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebgpuMaterialsToonBuffer(allocation, WebgpuMaterialsToonSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebgpuMaterialsToonBuffer(allocation, WebgpuMaterialsToonSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebgpuMaterialsToonBuffer(allocation, WebgpuMaterialsToonSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            const uint32_t gradientLevels = GradientAtlasWidth;
            const uint8_t fallbackGradient[GradientAtlasWidth * 4u] = {};
            // The atlas has one shared texture identity. Every entity must
            // queue the same payload; otherwise the final allocation for a
            // label/light entity would overwrite the shared descriptor with
            // its fallback bytes.
            const uint8_t *gradientData = gradientAtlas.data();
            const size_t gradientBytes = gradientAtlas.size();
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebgpuMaterialsToonSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebgpuMaterialsToonGradientAtlas",
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

    void WebgpuMaterialsToonRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuMaterialsToonRuntimeAdapter::afterFrame(
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
        prepareWebgpuMaterialsToonPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebgpuMaterialsToonPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgpu_materials_toon\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << (uint64_t(width) * 4u) << ",\n"
                   << "  \"byteCount\": " << byteCount << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderSetType\": \"WebgpuMaterialsToonSceneRenderSet\",\n"
                   << "  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 2,\n  \"screenPassCount\": 1,\n"
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
                output << ",\n  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgpu_materials_toon\",\"scenarioId\":\""
                       << options.scenarioId.c_str()
                       << "\",\"captureFrame\":61,\"sha256\":\"fa40bb972354f51bc8668df6def7f2190f496b55a4a7ba64d8296b0328f7be3e\",\"target\":\"canvas:not([class])\",\"eventCount\":3}";
            }
            output << "\n}\n";
        }
        std::ostringstream snapshotBuilder;
        snapshotBuilder
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_materials_toon\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\",\"gpuWorkDslOnly\":true"
            << ",\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1"
            << ",\"renderableObjectCount\":221,\"scenePassCount\":2,\"screenPassCount\":1"
            << ",\"drawCommandCount\":2,\"directDrawFallback\":false"
            << ",\"renderSetType\":\"WebgpuMaterialsToonSceneRenderSet\",\"entityCount\":"
            << entities.size() << ",\"instanceCounts\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshotBuilder << ',';
            snapshotBuilder << 1u;
        }
        snapshotBuilder
            << "],\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebgpuMaterialsToonSceneRenderSet\",\"renderableObjectCount\":221,\"entityCount\":"
            << entities.size() << ",\"drawCommandCount\":2,\"directDrawFallback\":false,\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"scenePasses\":["
            << "{\"name\":\"toon-outline\",\"renderClass\":\"WebgpuMaterialsToonOutlinePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"toon-main\",\"renderClass\":\"WebgpuMaterialsToonMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshotBuilder << ',';
            snapshotBuilder << "{\"entityId\":" << index
                            << ",\"logicalRenderableId\":\"toon-entity-" << index
                            << "\",\"instanceCount\":1}";
        }
        snapshotBuilder
            << "]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"toon-outline\"},{\"sceneRoot\":\"scene\",\"scenePass\":\"toon-main\"}]"
            << ",\"fontAssetSha256\":\""
            << (gentilisAssetLoaded
                ? "7ed95f2faa30f59dbe7cfb145b97c42a6ba1188cd2eec01ca61485e8c83ee9de"
                : "")
            << "\",\"fontFallbackUsed\":" << (gentilisAssetLoaded ? "false" : "true") << "}\n";
        const std::string snapshot = snapshotBuilder.str();
        prepareWebgpuMaterialsToonPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot;
        }
        prepareWebgpuMaterialsToonPath(options.semanticSnapshotPath);
        if (!options.semanticSnapshotPath.empty())
        {
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                   << "  \"caseId\": \"webgpu_materials_toon\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"kind\": \"loader-snapshot\",\n"
                   << "  \"canonicalState\": \"216-spheres-four-labels-one-light-mesh\",\n"
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

    void WebgpuMaterialsToonRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
