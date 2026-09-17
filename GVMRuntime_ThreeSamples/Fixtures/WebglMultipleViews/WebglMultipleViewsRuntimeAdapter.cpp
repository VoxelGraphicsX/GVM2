#include "WebglMultipleViewsRuntimeAdapter.hpp"

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
#include <sstream>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Extracts one mathematical row from a GLM column-major matrix. */
        glm::vec4 webglMultipleViewsMatrixRow(const glm::mat4 &matrix, uint32_t row)
        {
            return glm::vec4(matrix[0u][row], matrix[1u][row], matrix[2u][row], matrix[3u][row]);
        }


        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebglMultipleViewsPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglMultipleViewsBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Generates the deterministic 128x128 radial alpha texture used by the shadow planes. */
        void buildWebglMultipleViewsShadowTexture(eastl::vector<uint8_t> &texture)
        {
            constexpr uint32_t Size = 128u;
            texture.resize(static_cast<size_t>(Size) * Size * 4u);
            for (uint32_t y = 0u; y < Size; ++y)
            {
                for (uint32_t x = 0u; x < Size; ++x)
                {
                    const float dx = (float(x) + 0.5f - 64.0f) / 64.0f;
                    const float dy = (float(y) + 0.5f - 64.0f) / 64.0f;
                    const float radius = std::sqrt(dx * dx + dy * dy);
                    const float alpha = radius <= 0.1f
                        ? 0.15f
                        : std::clamp(0.15f * (1.0f - radius) / 0.9f, 0.0f, 0.15f);
                    const uint8_t value = static_cast<uint8_t>(std::round(alpha * 255.0f));
                    const size_t offset = (static_cast<size_t>(y) * Size + x) * 4u;
                    texture[offset + 0u] = 0u;
                    texture[offset + 1u] = 0u;
                    texture[offset + 2u] = 0u;
                    texture[offset + 3u] = value;
                }
            }
        }

        /** Appends one barycentric triangle to a CPU entity. */
        void appendWebglMultipleViewsTriangle(WebglMultipleViewsEntity &entity,
                                       const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &c,
                                       const glm::vec3 &normal,
                                       const glm::vec2 &uvA = glm::vec2(0.0f),
                                       const glm::vec2 &uvB = glm::vec2(0.0f),
                                       const glm::vec2 &uvC = glm::vec2(0.0f))
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(uvA, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(uvB, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(uvC, 0.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Appends one authored WireframeGeometry edge to the shared Set. */
        void appendWebglMultipleViewsLine(WebglMultipleViewsEntity &entity,
                                          const glm::vec3 &start,
                                          const glm::vec3 &end)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 startValue(start, 1.0f);
            const glm::vec4 endValue(end, 1.0f);
            entity.vertices.push_back({startValue, endValue,
                                       glm::vec4(0.0f), glm::vec4(0.0f)});
            entity.vertices.push_back({endValue, startValue,
                                       glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.indices.insert(entity.indices.end(), {base, base + 1u});
        }

        /** Generates one 300x300 shadow plane at the exact upstream world offset. */
        void buildWebglMultipleViewsShadow(WebglMultipleViewsEntity &entity, float xOffset)
        {
            const glm::vec3 a(-150.0f + xOffset, -250.0f, -150.0f);
            const glm::vec3 b(150.0f + xOffset, -250.0f, -150.0f);
            const glm::vec3 c(150.0f + xOffset, -250.0f, 150.0f);
            const glm::vec3 d(-150.0f + xOffset, -250.0f, 150.0f);
            appendWebglMultipleViewsTriangle(entity, a, d, b, glm::vec3(0.0f, 1.0f, 0.0f),
                                             glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f));
            appendWebglMultipleViewsTriangle(entity, b, d, c, glm::vec3(0.0f, 1.0f, 0.0f),
                                             glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 1.0f));
        }

        /** Generates Three r185's non-indexed IcosahedronGeometry(radius, 1) subdivision order. */
        void buildWebglMultipleViewsIcosahedron(WebglMultipleViewsEntity &entity, float radius)
        {
            const double t = (1.0 + std::sqrt(5.0)) * 0.5;
            const glm::dvec3 base[] = {
                {-1.0, t, 0.0}, {1.0, t, 0.0}, {-1.0, -t, 0.0}, {1.0, -t, 0.0},
                {0.0, -1.0, t}, {0.0, 1.0, t}, {0.0, -1.0, -t}, {0.0, 1.0, -t},
                {t, 0.0, -1.0}, {t, 0.0, 1.0}, {-t, 0.0, -1.0}, {-t, 0.0, 1.0}};
            const uint32_t faces[][3] = {
                {0u, 11u, 5u}, {0u, 5u, 1u}, {0u, 1u, 7u}, {0u, 7u, 10u}, {0u, 10u, 11u},
                {1u, 5u, 9u}, {5u, 11u, 4u}, {11u, 10u, 2u}, {10u, 7u, 6u}, {7u, 1u, 8u},
                {3u, 9u, 4u}, {3u, 4u, 2u}, {3u, 2u, 6u}, {3u, 6u, 8u}, {3u, 8u, 9u},
                {4u, 9u, 5u}, {2u, 4u, 11u}, {6u, 2u, 10u}, {8u, 6u, 7u}, {9u, 8u, 1u}};
            entity.vertices.clear();
            entity.indices.clear();
            constexpr uint32_t Columns = 2u;
            const auto appendTriangle = [&](const glm::dvec3 &a,
                                            const glm::dvec3 &b,
                                            const glm::dvec3 &c) {
                const glm::dvec3 normal = glm::normalize(glm::cross(b - a, c - a));
                appendWebglMultipleViewsTriangle(
                    entity, glm::vec3(a), glm::vec3(b), glm::vec3(c), glm::vec3(normal));
            };
            for (const auto &face : faces)
            {
                const glm::dvec3 a = base[face[0]];
                const glm::dvec3 b = base[face[1]];
                const glm::dvec3 c = base[face[2]];
                glm::dvec3 subdivision[Columns + 1u][Columns + 1u];
                for (uint32_t i = 0u; i <= Columns; ++i)
                {
                    const glm::dvec3 aj = glm::mix(a, c, double(i) / double(Columns));
                    const glm::dvec3 bj = glm::mix(b, c, double(i) / double(Columns));
                    const uint32_t rows = Columns - i;
                    for (uint32_t j = 0u; j <= rows; ++j)
                    {
                        if (j == 0u && i == Columns)
                            subdivision[i][j] = aj;
                        else
                            subdivision[i][j] = glm::mix(aj, bj, double(j) / double(rows));
                    }
                }
                for (uint32_t i = 0u; i < Columns; ++i)
                {
                    for (uint32_t j = 0u; j < 2u * (Columns - i) - 1u; ++j)
                    {
                        const uint32_t k = j / 2u;
                        glm::dvec3 triangle[3u];
                        if ((j & 1u) == 0u)
                        {
                            triangle[0] = subdivision[i][k + 1u];
                            triangle[1] = subdivision[i + 1u][k];
                            triangle[2] = subdivision[i][k];
                        }
                        else
                        {
                            triangle[0] = subdivision[i][k + 1u];
                            triangle[1] = subdivision[i + 1u][k + 1u];
                            triangle[2] = subdivision[i + 1u][k];
                        }
                        for (glm::dvec3 &vertex : triangle)
                            vertex = glm::normalize(vertex) * double(radius);
                        appendTriangle(triangle[0], triangle[1], triangle[2]);
                    }
                }
            }
        }

        /** Builds the MeshBasicMaterial wireframe edge stream in authored triangle order. */
        void buildWebglMultipleViewsWireframe(WebglMultipleViewsEntity &entity, float radius)
        {
            WebglMultipleViewsEntity solid;
            buildWebglMultipleViewsIcosahedron(solid, radius);
            entity.vertices.clear();
            entity.indices.clear();
            for (size_t index = 0u; index + 2u < solid.indices.size(); index += 3u)
            {
                const glm::vec3 triangle[3u] = {
                    glm::vec3(solid.vertices[solid.indices[index + 0u]].position),
                    glm::vec3(solid.vertices[solid.indices[index + 1u]].position),
                    glm::vec3(solid.vertices[solid.indices[index + 2u]].position)};
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const glm::vec3 &start = triangle[edge];
                    const glm::vec3 &end = triangle[(edge + 1u) % 3u];
                    appendWebglMultipleViewsLine(entity, start, end);
                }
            }
        }

        /** Converts one authored sRGB channel to the linear working space used by MeshPhongMaterial. */
        float webglMultipleViewsSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one HSL sample to the linear vertex colour stored by the reference. */
        glm::vec3 webglMultipleViewsHslToRgb(float hue, float saturation, float lightness)
        {
            const float chroma = (1.0f - std::abs(2.0f * lightness - 1.0f)) * saturation;
            const float segment = hue * 6.0f;
            const float x = chroma * (1.0f - std::abs(std::fmod(segment, 2.0f) - 1.0f));
            glm::vec3 prime(0.0f);
            if (segment < 1.0f) prime = glm::vec3(chroma, x, 0.0f);
            else if (segment < 2.0f) prime = glm::vec3(x, chroma, 0.0f);
            else if (segment < 3.0f) prime = glm::vec3(0.0f, chroma, x);
            else if (segment < 4.0f) prime = glm::vec3(0.0f, x, chroma);
            else if (segment < 5.0f) prime = glm::vec3(x, 0.0f, chroma);
            else prime = glm::vec3(chroma, 0.0f, x);
            const float match = lightness - chroma * 0.5f;
            const glm::vec3 srgbColor = prime + glm::vec3(match);
            return glm::vec3(
                webglMultipleViewsSrgbToLinear(srgbColor.r),
                webglMultipleViewsSrgbToLinear(srgbColor.g),
                webglMultipleViewsSrgbToLinear(srgbColor.b));
        }

        /** Packs one of the three upstream Icosahedron vertex-color formulas into uv.xyz. */
        void colorWebglMultipleViewsIcosahedron(WebglMultipleViewsEntity &entity,
                                                 float radius,
                                                 uint32_t colorMode)
        {
            for (auto &vertex : entity.vertices)
            {
                const float normalizedHeight = std::clamp(vertex.position.y / radius * 0.5f + 0.5f, 0.0f, 1.0f);
                glm::vec3 color(0.0f);
                if (colorMode == 0u)
                    color = webglMultipleViewsHslToRgb(normalizedHeight, 1.0f, 0.5f);
                else if (colorMode == 1u)
                    color = webglMultipleViewsHslToRgb(0.0f, normalizedHeight, 0.5f);
                else
                {
                    const float authoredGreen = std::clamp(0.8f - normalizedHeight, 0.0f, 1.0f);
                    color = glm::vec3(
                        1.0f,
                        webglMultipleViewsSrgbToLinear(authoredGreen),
                        0.0f);
                }
                vertex.uv = glm::vec4(color, 0.0f);
            }
        }

        /** Builds one Three perspective matrix before the generated-backend clip conversion. */
        glm::mat4 webglMultipleViewsProjection(float fieldOfView,
                                                uint32_t width,
                                                uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(fieldOfView), aspect, 1.0f, 10000.0f);
        }


        /** Builds the three camera invocations in the same order as the locked single-sample composite. */
        void buildWebglMultipleViewsInvocations(
            eastl::vector<WebglMultipleViewsHostInvocationData> &invocations,
            uint32_t width,
            uint32_t height,
            float accumulatedMouseX)
        {
            const float cameraDelta = accumulatedMouseX * 0.05f;
            // The generated composite uses top-origin viewport coordinates.  The
            // two half-height views are therefore stored in top-right then
            // bottom-right order, while preserving the upstream camera rules.
            const glm::vec3 eyes[3u] = {
                glm::vec3(cameraDelta, 300.0f, 1800.0f),
                glm::vec3(1400.0f, 800.0f - cameraDelta, 1400.0f),
                glm::vec3(-cameraDelta, 1800.0f, 0.0f)};
            const glm::vec3 targets[3u] = {
                glm::vec3(0.0f),
                glm::vec3(0.0f),
                glm::vec3(-cameraDelta, 0.0f, 0.0f)};
            const glm::vec3 ups[3u] = {
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)};
            const float fovs[3u] = {30.0f, 60.0f, 45.0f};
            const glm::vec4 viewports[3u] = {
                glm::vec4(0.0f, 0.0f, 0.5f, 1.0f),
                glm::vec4(0.5f, 0.0f, 0.5f, 0.5f),
                glm::vec4(0.5f, 0.5f, 0.5f, 0.5f)};
            const glm::vec4 backgrounds[3u] = {
                glm::vec4(0.5f, 0.5f, 0.7f, 1.0f),
                glm::vec4(0.5f, 0.7f, 0.7f, 1.0f),
                glm::vec4(0.7f, 0.5f, 0.5f, 1.0f)};
            invocations.resize(3u);
            for (uint32_t viewIndex = 0u; viewIndex < 3u; ++viewIndex)
            {
                const uint32_t viewWidth = width / 2u;
                const uint32_t viewHeight = viewIndex == 0u ? height : height / 2u;
                const glm::mat4 view = glm::lookAt(
                    eyes[viewIndex], targets[viewIndex], ups[viewIndex]);
                const glm::mat4 projection = webglMultipleViewsProjection(
                    fovs[viewIndex], viewWidth, viewHeight);
                const glm::mat4 viewProjection = projection * view;
                auto &invocation = invocations[viewIndex];
                invocation.viewProjection0 = webglMultipleViewsMatrixRow(viewProjection, 0u);
                invocation.viewProjection1 = webglMultipleViewsMatrixRow(viewProjection, 1u);
                invocation.viewProjection2 = webglMultipleViewsMatrixRow(viewProjection, 2u);
                invocation.viewProjection3 = webglMultipleViewsMatrixRow(viewProjection, 3u);
                invocation.view0 = webglMultipleViewsMatrixRow(view, 0u);
                invocation.view1 = webglMultipleViewsMatrixRow(view, 1u);
                invocation.view2 = webglMultipleViewsMatrixRow(view, 2u);
                invocation.view3 = webglMultipleViewsMatrixRow(view, 3u);
                invocation.viewport = viewports[viewIndex];
                const glm::vec3 lightView = glm::normalize(
                    glm::vec3(view * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
                invocation.lightDirectionAndSelection = glm::vec4(lightView, -1.0f);
                invocation.background = backgrounds[viewIndex];
                invocation.screenSize = glm::vec4(
                    float(viewWidth), float(viewHeight),
                    accumulatedMouseX != 0.0f ? 1.0f : 0.0f, 0.0f);
            }
        }

        /** Validates the four deterministic target/webglMultipleViews scenarios. */
        /** Validates the frozen WebglMultipleViews scenario matrix and output contract. */
        void validateWebglMultipleViewsOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "pointer-camera" && options.targetFrame == 1u;
            if (options.caseId != "webgl_multiple_views"
                || (!scenario0 && !scenario1)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_multiple_views scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebglMultipleViewsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglMultipleViewsOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(9u);
        buildWebglMultipleViewsShadowTexture(shadowTexture);
        buildWebglMultipleViewsShadow(entities[0], 0.0f);
        buildWebglMultipleViewsShadow(entities[1], -400.0f);
        buildWebglMultipleViewsShadow(entities[2], 400.0f);
        buildWebglMultipleViewsIcosahedron(entities[3], 200.0f);
        buildWebglMultipleViewsIcosahedron(entities[4], 200.0f);
        buildWebglMultipleViewsIcosahedron(entities[5], 200.0f);
        colorWebglMultipleViewsIcosahedron(entities[3], 200.0f, 0u);
        colorWebglMultipleViewsIcosahedron(entities[4], 200.0f, 1u);
        colorWebglMultipleViewsIcosahedron(entities[5], 200.0f, 2u);
        buildWebglMultipleViewsWireframe(entities[6], 200.0f);
        buildWebglMultipleViewsWireframe(entities[7], 200.0f);
        buildWebglMultipleViewsWireframe(entities[8], 200.0f);
        const glm::mat4 models[9u] = {
            glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
            glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-400.0f, 0.0f, 0.0f)), -1.87f, glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(400.0f, 0.0f, 0.0f)),
            glm::mat4(1.0f),
            glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-400.0f, 0.0f, 0.0f)), -1.87f, glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(400.0f, 0.0f, 0.0f)),
            glm::mat4(1.0f)};
        const glm::vec4 colors[9u] = {
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(0.65f, 0.18f, 0.25f, 0.0f), glm::vec4(0.80f, 0.35f, 0.10f, 0.0f),
            glm::vec4(0.92f, 0.65f, 0.05f, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 2.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 2.0f), glm::vec4(0.0f, 0.0f, 0.0f, 2.0f)};
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            const glm::mat4 normalModel = glm::transpose(glm::inverse(models[index]));
            entity.objectData.model0 = webglMultipleViewsMatrixRow(models[index], 0u);
            entity.objectData.model1 = webglMultipleViewsMatrixRow(models[index], 1u);
            entity.objectData.model2 = webglMultipleViewsMatrixRow(models[index], 2u);
            entity.objectData.model3 = webglMultipleViewsMatrixRow(models[index], 3u);
            entity.objectData.normalModel0 = webglMultipleViewsMatrixRow(normalModel, 0u);
            entity.objectData.normalModel1 = webglMultipleViewsMatrixRow(normalModel, 1u);
            entity.objectData.normalModel2 = webglMultipleViewsMatrixRow(normalModel, 2u);
            entity.objectData.normalModel3 = webglMultipleViewsMatrixRow(normalModel, 3u);
            entity.objectData.baseColorAndFlags = colors[index];
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = colors[index];
            entity.renderFlags.values[0] = index < 3u ? 1u : 0u;
            entity.renderFlags.values[1] = 0u;
            entity.renderFlags.values[2] = 0u;
            entity.renderFlags.values[3] = 0u;
        }
        invocations.clear();
        invocations.resize(3u);
        const glm::vec3 eyes[3u] = {
            glm::vec3(0.0f, 300.0f, 1800.0f), glm::vec3(1400.0f, 800.0f, 1400.0f),
            glm::vec3(0.0f, 1800.0f, 0.0f)};
        const glm::vec3 ups[3u] = {
            glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)};
        const float fovs[3u] = {30.0f, 60.0f, 45.0f};
        const glm::vec4 viewports[3u] = {
            glm::vec4(0.0f, 0.0f, 0.5f, 1.0f), glm::vec4(0.5f, 0.0f, 0.5f, 0.5f),
            glm::vec4(0.5f, 0.5f, 0.5f, 0.5f)};
        const glm::vec4 backgrounds[3u] = {
            glm::vec4(0.5f, 0.5f, 0.7f, 1.0f), glm::vec4(0.5f, 0.7f, 0.7f, 1.0f),
            glm::vec4(0.7f, 0.5f, 0.5f, 1.0f)};
        for (uint32_t viewIndex = 0u; viewIndex < 3u; ++viewIndex)
        {
            const uint32_t viewWidth = viewIndex == 0u ? options.width / 2u : options.width / 2u;
            const uint32_t viewHeight = viewIndex == 0u ? options.height : options.height / 2u;
            const glm::mat4 view = glm::lookAt(eyes[viewIndex], glm::vec3(0.0f), ups[viewIndex]);
            const glm::mat4 projection = webglMultipleViewsProjection(fovs[viewIndex], viewWidth, viewHeight);
            const glm::mat4 viewProjection = projection * view;
            invocations[viewIndex].viewProjection0 = webglMultipleViewsMatrixRow(viewProjection, 0u);
            invocations[viewIndex].viewProjection1 = webglMultipleViewsMatrixRow(viewProjection, 1u);
            invocations[viewIndex].viewProjection2 = webglMultipleViewsMatrixRow(viewProjection, 2u);
            invocations[viewIndex].viewProjection3 = webglMultipleViewsMatrixRow(viewProjection, 3u);
            invocations[viewIndex].view0 = webglMultipleViewsMatrixRow(view, 0u);
            invocations[viewIndex].view1 = webglMultipleViewsMatrixRow(view, 1u);
            invocations[viewIndex].view2 = webglMultipleViewsMatrixRow(view, 2u);
            invocations[viewIndex].view3 = webglMultipleViewsMatrixRow(view, 3u);
            invocations[viewIndex].viewport = viewports[viewIndex];
            const glm::vec3 lightView = glm::normalize(glm::vec3(view * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
            invocations[viewIndex].lightDirectionAndSelection = glm::vec4(lightView, -1.0f);
            invocations[viewIndex].background = backgrounds[viewIndex];
            invocations[viewIndex].screenSize = glm::vec4(
                float(viewWidth), float(viewHeight), 0.0f, 0.0f);
        }
        invocationValues.clear();
        invocationValues.reserve(invocations.size() * 48u);
        for (const auto &invocation : invocations)
        {
            const glm::vec4 viewProjectionRows[4u] = {
                invocation.viewProjection0, invocation.viewProjection1,
                invocation.viewProjection2, invocation.viewProjection3};
            const glm::vec4 viewRows[4u] = {
                invocation.view0, invocation.view1, invocation.view2, invocation.view3};
            for (const glm::vec4 &row : viewProjectionRows)
            {
                invocationValues.push_back(row.x);
                invocationValues.push_back(row.y);
                invocationValues.push_back(row.z);
                invocationValues.push_back(row.w);
            }
            for (const glm::vec4 &row : viewRows)
            {
                invocationValues.push_back(row.x);
                invocationValues.push_back(row.y);
                invocationValues.push_back(row.z);
                invocationValues.push_back(row.w);
            }
            invocationValues.push_back(invocation.viewport.x);
            invocationValues.push_back(invocation.viewport.y);
            invocationValues.push_back(invocation.viewport.z);
            invocationValues.push_back(invocation.viewport.w);
            invocationValues.push_back(invocation.lightDirectionAndSelection.x);
            invocationValues.push_back(invocation.lightDirectionAndSelection.y);
            invocationValues.push_back(invocation.lightDirectionAndSelection.z);
            invocationValues.push_back(invocation.lightDirectionAndSelection.w);
            invocationValues.push_back(invocation.background.x);
            invocationValues.push_back(invocation.background.y);
            invocationValues.push_back(invocation.background.z);
            invocationValues.push_back(invocation.background.w);
            invocationValues.push_back(invocation.screenSize.x);
            invocationValues.push_back(invocation.screenSize.y);
            invocationValues.push_back(invocation.screenSize.z);
            invocationValues.push_back(invocation.screenSize.w);
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_multiple_views could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebglMultipleViewsVertices" + suffix;
            const std::string indexName = "WebglMultipleViewsIndices" + suffix;
            const std::string objectName = "WebglMultipleViewsObject" + suffix;
            const std::string instanceName = "WebglMultipleViewsInstance" + suffix;
            const std::string materialName = "WebglMultipleViewsMaterial" + suffix;
            const std::string flagsName = "WebglMultipleViewsRenderFlags" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            appendWebglMultipleViewsBuffer(allocation, WebglMultipleViewsSceneRenderSetComponents::renderFlags, flagsName.c_str(), &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglMultipleViewsSceneRenderSetComponents::textures;
            if (index < 3u)
            {
                textureComponent.textures.push_back({
                    .textureName = "WebglMultipleViewsShadowTexture",
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .width = 128u,
                    .height = 128u,
                    .data = shadowTexture.data(),
                    .dataStorageBytes = shadowTexture.size(),
                    .mipmapOffsetBytes = {0u},
                });
            }
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMultipleViewsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        updateCameraState(options, frameIndex);
    }

    void WebglMultipleViewsRuntimeAdapter::rebuildInvocationValues()
    {
        invocationValues.clear();
        invocationValues.reserve(invocations.size() * 48u);
        for (const auto &invocation : invocations)
        {
            const glm::vec4 viewProjectionRows[4u] = {
                invocation.viewProjection0, invocation.viewProjection1,
                invocation.viewProjection2, invocation.viewProjection3};
            const glm::vec4 viewRows[4u] = {
                invocation.view0, invocation.view1, invocation.view2, invocation.view3};
            for (const glm::vec4 &row : viewProjectionRows)
            {
                invocationValues.push_back(row.x);
                invocationValues.push_back(row.y);
                invocationValues.push_back(row.z);
                invocationValues.push_back(row.w);
            }
            for (const glm::vec4 &row : viewRows)
            {
                invocationValues.push_back(row.x);
                invocationValues.push_back(row.y);
                invocationValues.push_back(row.z);
                invocationValues.push_back(row.w);
            }
            invocationValues.push_back(invocation.viewport.x);
            invocationValues.push_back(invocation.viewport.y);
            invocationValues.push_back(invocation.viewport.z);
            invocationValues.push_back(invocation.viewport.w);
            invocationValues.push_back(invocation.lightDirectionAndSelection.x);
            invocationValues.push_back(invocation.lightDirectionAndSelection.y);
            invocationValues.push_back(invocation.lightDirectionAndSelection.z);
            invocationValues.push_back(invocation.lightDirectionAndSelection.w);
            invocationValues.push_back(invocation.background.x);
            invocationValues.push_back(invocation.background.y);
            invocationValues.push_back(invocation.background.z);
            invocationValues.push_back(invocation.background.w);
            invocationValues.push_back(invocation.screenSize.x);
            invocationValues.push_back(invocation.screenSize.y);
            invocationValues.push_back(invocation.screenSize.z);
            invocationValues.push_back(invocation.screenSize.w);
        }
    }

    void WebglMultipleViewsRuntimeAdapter::updateCameraState(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        const float accumulatedMouseX = options.scenarioId == "pointer-camera"
            ? std::clamp(float(frameIndex) * 200.0f, -40000.0f, 40000.0f)
            : 0.0f;
        buildWebglMultipleViewsInvocations(
            invocations, options.width, options.height, accumulatedMouseX);
        rebuildInvocationValues();
    }

    void WebglMultipleViewsRuntimeAdapter::afterFrame(
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
        prepareWebglMultipleViewsPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebglMultipleViewsPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
                   << "\"caseId\":\"webgl_multiple_views\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"pipeline\":\""
                   << options.pipeline.c_str() << "\",\"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\"frame\":"
                   << frameIndex << ",\"randomSeed\":" << options.randomSeed
                   << ",\"width\":" << width << ",\"height\":" << height
                   << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
                   << ",\"byteCount\":" << byteCount
                   << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
                   << "\"msaaEnabled\":false,\"samplePolicy\":{\"mode\":\"single-sample\","
                   << "\"msaaEnabled\":false,\"simulateMsaa\":false},\"invocationScreenSizeZ\":["
                   << invocations[0].screenSize.z << "," << invocations[1].screenSize.z << ","
                   << invocations[2].screenSize.z << "],\"inputReplay\":";
            if (options.scenarioId == "pointer-camera")
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgl_multiple_views\","
                       << "\"scenarioId\":\"pointer-camera\",\"captureFrame\":1,"
                       << "\"sha256\":\"9cefaa9ca40ba27906491a34b946628d1784435c997cdc3e2c53a4a2188d09cc\","
                       << "\"target\":\"#container > canvas\",\"eventCount\":1}";
            }
            else
            {
                output << "null";
            }
            output << "}\n";
        }
        const uint32_t opaqueInvocationCount = 3u;
        const uint32_t transparentInvocationCount = 3u;
        const uint32_t wireInvocationCount = 3u;
        std::ostringstream snapshotBuilder;
        snapshotBuilder << "{\"schemaVersion\":1,\"caseId\":\"webgl_multiple_views\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":9,"
            << "\"scenePassCount\":9,\"screenPassCount\":3,\"drawCommandCount\":9,"
            << "\"directDrawFallback\":false,\"renderSetType\":\"WebglMultipleViewsSceneRenderSet\","
            << "\"scenePassSequence\":[";
        bool firstSequenceEntry = true;
        auto appendSequenceEntry = [&](const char *passName, uint32_t entityOrdinal) {
            if (!firstSequenceEntry) snapshotBuilder << ',';
            firstSequenceEntry = false;
            snapshotBuilder << "{\"sceneRoot\":\"scene\",\"scenePass\":\""
                << passName << "\",\"entityOrdinal\":" << entityOrdinal << '}';
        };
        for (uint32_t view = 0u; view < 3u; ++view)
        {
            appendSequenceEntry("view-opaque", view);
            appendSequenceEntry("view-transparent-entity", view);
            appendSequenceEntry("view-wireframe", view);
        }
        snapshotBuilder << "],\"sceneRoots\":[{\"id\":\"scene\","
            << "\"renderSetId\":\"scene-set-0\",\"renderSetCount\":1,"
            << "\"renderSetType\":\"WebglMultipleViewsSceneRenderSet\","
            << "\"renderableObjectCount\":9,\"entityCount\":9,\"drawCommandCount\":9,"
            << "\"directDrawFallback\":false,\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"one-fixed-radial-shadow-slot\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"opaque-shadow-wireframe-and-selected-entity-phase\"}],"
            << "\"scenePasses\":["
            << "{\"name\":\"view-opaque\",\"renderClass\":\"WebglMultipleViewsOpaquePass\","
            << "\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << opaqueInvocationCount << ",\"drawCommandCount\":" << opaqueInvocationCount
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"view-transparent-entity\",\"renderClass\":\"WebglMultipleViewsTransparentEntityPass\","
            << "\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << transparentInvocationCount << ",\"drawCommandCount\":" << transparentInvocationCount
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"view-wireframe\",\"renderClass\":\"WebglMultipleViewsWirePass\","
            << "\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << wireInvocationCount << ",\"drawCommandCount\":" << wireInvocationCount
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (size_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) snapshotBuilder << ',';
            snapshotBuilder << "{\"entityId\":" << index
                << ",\"logicalRenderableId\":\"multiple-views-"
                << index << "\",\"instanceCount\":1}";
        }
        snapshotBuilder << "]}]}\n";
        const std::string snapshot = snapshotBuilder.str();
        prepareWebglMultipleViewsPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot;
        }
        prepareWebglMultipleViewsPath(options.semanticSnapshotPath);
        if (!options.semanticSnapshotPath.empty())
        {
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot;
        }
        captureWritten = true;
    }

    void WebglMultipleViewsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        shadowTexture.clear();
    }
}
