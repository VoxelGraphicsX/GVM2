#include "WebglMultipleElementsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double FrameStepSeconds = 1.0 / 60.0;
        // The deterministic browser bootstrap fixes Date.now() at this epoch;
        // the sample's compatibility transform uses the same phase.
        constexpr double ReferenceEpochSeconds = 1700000000.0;
        constexpr uint32_t RandomPreamble = 100u;
        constexpr uint32_t RandomStridePerScene = 34u;
        constexpr char WebglMultipleElementsScrollReplaySha256[] =
            "c3265ecc2364d81a257a74e8fa3ac4923634ecd300688bef4cabdf17acc9c331";
        constexpr char WebglMultipleElementsOrbitReplaySha256[] =
            "6ba4d0fe58fcf43dfc95726be7677a6be7e7728c175cb623c47f82baaf3a7bc4";
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
        void prepareWebglMultipleElementsPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Computes the SHA-256 identity of one locked input replay document. */
        eastl::string calculateWebglMultipleElementsReplaySha256(const eastl::string &pathValue)
        {
            std::ifstream input(pathValue.c_str(), std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("webgl_multiple_elements could not open its input replay.");
            const std::streamsize size = input.tellg();
            if (size < 0 || size > static_cast<std::streamsize>(UINT32_MAX))
                throw std::runtime_error("webgl_multiple_elements input replay is too large.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0, std::ios::beg);
            if (size > 0 && !input.read(reinterpret_cast<char *>(bytes.data()), size))
                throw std::runtime_error("webgl_multiple_elements could not read its input replay.");
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            static constexpr char Hex[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t byte : digest)
            {
                result.push_back(Hex[(byte >> 4u) & 0x0fu]);
                result.push_back(Hex[byte & 0x0fu]);
            }
            return result;
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglMultipleElementsBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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
        void appendWebglMultipleElementsTriangle(WebglMultipleElementsEntity &entity,
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

        /** Advances the fixed xorshift32 stream used by the r185 reference harness. */
        float nextWebglMultipleElementsRandom(uint32_t &state)
        {
            uint32_t value = state == 0u ? 0x6d2b79f5u : state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Converts one HSL hue to the working-linear RGB color used by MeshStandardMaterial. */
        glm::vec3 webglMultipleElementsHslToLinear(float hue)
        {
            const float q = 1.0f;
            const float p = 0.5f;
            const auto hueToRgb = [p, q](float value)
            {
                if (value < 0.0f) value += 1.0f;
                if (value > 1.0f) value -= 1.0f;
                if (value < 1.0f / 6.0f) return p + (q - p) * 6.0f * value;
                if (value < 0.5f) return q;
                if (value < 2.0f / 3.0f)
                    return p + (q - p) * 6.0f * (2.0f / 3.0f - value);
                return p;
            };
            const glm::vec3 srgb(
                hueToRgb(hue + 1.0f / 3.0f), hueToRgb(hue), hueToRgb(hue - 1.0f / 3.0f));
            const auto srgbToLinear = [](float value)
            {
                return value <= 0.04045f
                    ? value / 12.92f
                    : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            return glm::vec3(srgbToLinear(srgb.x), srgbToLinear(srgb.y), srgbToLinear(srgb.z));
        }

        /** Appends a full-card triangle list that is expanded by the DSL background path. */
        void appendWebglMultipleElementsCardBackground(WebglMultipleElementsEntity &entity)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec3 corners[4u] = {
                {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f},
                {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
            for (const glm::vec3 &corner : corners)
            {
                entity.vertices.push_back({
                    glm::vec4(corner, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                    glm::vec4(0.0f)});
            }
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u, base, base + 2u, base + 3u});
        }

        /** Generates the exact r185 BoxGeometry(1,1,1) face-separated stream. */
        void buildWebglMultipleElementsBox(WebglMultipleElementsEntity &entity)
        {
            const glm::vec3 faceCorners[6u][4u] = {
                {{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f}},
                {{-0.5f,-0.5f,0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{-0.5f,0.5f,0.5f}},
                {{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}},
                {{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}},
                {{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f}},
                {{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f}}};
            const glm::vec3 faceNormals[6u] = {
                {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                {
                    entity.vertices.push_back({
                        glm::vec4(faceCorners[face][corner], 1.0f),
                        glm::vec4(faceNormals[face], 0.0f), glm::vec4(0.0f)});
                }
                entity.indices.insert(entity.indices.end(), {
                    base, base + 1u, base + 2u, base, base + 2u, base + 3u});
            }
        }

        /** Generates the flat-shaded SphereGeometry(0.5,12,8) triangle stream. */
        void buildWebglMultipleElementsSphere(WebglMultipleElementsEntity &entity)
        {
            constexpr uint32_t Rings = 8u;
            constexpr uint32_t Segments = 12u;
            constexpr float Radius = 0.5f;
            for (uint32_t ring = 0u; ring < Rings; ++ring)
            {
                const double v0 = double(ring) / double(Rings);
                const double v1 = double(ring + 1u) / double(Rings);
                const double p0 = Pi * v0;
                const double p1 = Pi * v1;
                for (uint32_t segment = 0u; segment < Segments; ++segment)
                {
                    const double u0 = 2.0 * Pi * double(segment) / double(Segments);
                    const double u1 = 2.0 * Pi * double(segment + 1u) / double(Segments);
                    const glm::vec3 a(Radius * float(std::sin(p0) * std::cos(u0)), Radius * float(std::cos(p0)), Radius * float(std::sin(p0) * std::sin(u0)));
                    const glm::vec3 b(Radius * float(std::sin(p1) * std::cos(u0)), Radius * float(std::cos(p1)), Radius * float(std::sin(p1) * std::sin(u0)));
                    const glm::vec3 c(Radius * float(std::sin(p1) * std::cos(u1)), Radius * float(std::cos(p1)), Radius * float(std::sin(p1) * std::sin(u1)));
                    const glm::vec3 d(Radius * float(std::sin(p0) * std::cos(u1)), Radius * float(std::cos(p0)), Radius * float(std::sin(p0) * std::sin(u1)));
                    if (ring != 0u)
                        appendWebglMultipleElementsTriangle(entity, a, b, c, glm::normalize(a + b + c));
                    if (ring + 1u != Rings)
                        appendWebglMultipleElementsTriangle(entity, a, c, d, glm::normalize(a + c + d));
                }
            }
        }

        /** Generates the exact radius-.5 DodecahedronGeometry(0.5) triangles. */
        void buildWebglMultipleElementsDodecahedron(WebglMultipleElementsEntity &entity)
        {
            // Keep the polyhedron construction in double precision until the
            // final Float32 vertex upload, matching Three's JavaScript
            // PolyhedronGeometry normalization before Float32BufferAttribute.
            const double t = (1.0 + std::sqrt(5.0)) * 0.5;
            const double r = 1.0 / t;
            const glm::dvec3 source[20u] = {
                {-1,-1,-1},{-1,-1,1},{-1,1,-1},{-1,1,1},{1,-1,-1},{1,-1,1},{1,1,-1},{1,1,1},
                {0,-r,-t},{0,-r,t},{0,r,-t},{0,r,t},{-r,-t,0},{-r,t,0},{r,-t,0},{r,t,0},
                {-t,0,-r},{t,0,-r},{-t,0,r},{t,0,r}};
            const uint32_t indexList[] = {
                3,11,7,3,7,15,3,15,13, 7,19,17,7,17,6,7,6,15,
                17,4,8,17,8,10,17,10,6, 8,0,16,8,16,2,8,2,10,
                0,12,1,0,1,18,0,18,16, 6,10,2,6,2,13,6,13,15,
                2,16,18,2,18,3,2,3,13, 18,1,9,18,9,11,18,11,3,
                4,14,12,4,12,0,4,0,8, 11,9,5,11,5,19,11,19,7,
                19,5,14,19,14,4,19,4,17, 1,12,14,1,14,5,1,5,9};
            for (uint32_t triangle = 0u; triangle < sizeof(indexList) / sizeof(indexList[0]); triangle += 3u)
            {
                const glm::dvec3 normalizedA = glm::normalize(source[indexList[triangle]]) * 0.5;
                const glm::dvec3 normalizedB = glm::normalize(source[indexList[triangle + 1u]]) * 0.5;
                const glm::dvec3 normalizedC = glm::normalize(source[indexList[triangle + 2u]]) * 0.5;
                const glm::vec3 a(normalizedA);
                const glm::vec3 b(normalizedB);
                const glm::vec3 c(normalizedC);
                appendWebglMultipleElementsTriangle(entity, a, c, b,
                    glm::normalize(glm::cross(b - a, c - a)));
            }
        }

        /** Generates the flat-shaded CylinderGeometry(0.5,0.5,1,12) stream. */
        void buildWebglMultipleElementsCylinder(WebglMultipleElementsEntity &entity)
        {
            constexpr uint32_t Segments = 12u;
            constexpr float Radius = 0.5f;
            constexpr float HalfHeight = 0.5f;
            for (uint32_t segment = 0u; segment < Segments; ++segment)
            {
                const double a0 = 2.0 * Pi * double(segment) / double(Segments);
                const double a1 = 2.0 * Pi * double(segment + 1u) / double(Segments);
                const glm::vec3 top0(Radius * float(std::cos(a0)), HalfHeight, Radius * float(std::sin(a0)));
                const glm::vec3 top1(Radius * float(std::cos(a1)), HalfHeight, Radius * float(std::sin(a1)));
                const glm::vec3 bottom0(top0.x, -HalfHeight, top0.z);
                const glm::vec3 bottom1(top1.x, -HalfHeight, top1.z);
                const glm::vec3 sideNormal = glm::normalize(glm::vec3(top0.x + top1.x, 0.0f, top0.z + top1.z));
                appendWebglMultipleElementsTriangle(entity, bottom0, bottom1, top1, sideNormal);
                appendWebglMultipleElementsTriangle(entity, bottom0, top1, top0, sideNormal);
                appendWebglMultipleElementsTriangle(entity, top0, top1, glm::vec3(0.0f, HalfHeight, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                appendWebglMultipleElementsTriangle(entity, bottom0, glm::vec3(0.0f, -HalfHeight, 0.0f), bottom1, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates the eight-sided cone used by the upstream webglMultipleElements example. */
        void buildWebglMultipleElementsCone(WebglMultipleElementsEntity &entity)
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
                appendWebglMultipleElementsTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebglMultipleElementsTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildWebglMultipleElementsSphere(WebglMultipleElementsEntity &entity,
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
                    appendWebglMultipleElementsTriangle(entity, a, b, c, glm::normalize(a));
                    appendWebglMultipleElementsTriangle(entity, a, c, d, glm::normalize(a));
                }
            }
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webglMultipleElementsProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
        }

        /** Validates the four deterministic target/webglMultipleElements scenarios. */
        /** Validates the frozen WebglMultipleElements scenario matrix and output contract. */
        void validateWebglMultipleElementsOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-cards" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "rotated-cards" && options.targetFrame == 120u;
            const bool scenario2 = options.scenarioId == "scrolled-cards" && options.targetFrame == 121u;
            const bool scenario3 = options.scenarioId == "card-orbit" && options.targetFrame == 121u;
            if (options.caseId != "webgl_multiple_elements"
                || (!scenario0 && !scenario1 && !scenario2 && !scenario3)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_multiple_elements scenario does not match the locked r185 contract.");
            }
            const bool replayScenario = scenario2 || scenario3;
            if (replayScenario != !options.inputReplayPath.empty())
                throw std::invalid_argument("webgl_multiple_elements input replay is required only for scroll/orbit scenarios.");
            if (replayScenario)
            {
                const eastl::string actualHash = calculateWebglMultipleElementsReplaySha256(options.inputReplayPath);
                const eastl::string expectedHash = scenario2
                    ? WebglMultipleElementsScrollReplaySha256
                    : WebglMultipleElementsOrbitReplaySha256;
                if (actualHash != expectedHash)
                    throw std::invalid_argument("webgl_multiple_elements input replay SHA-256 does not match the locked document.");
            }
        }
    }

    void WebglMultipleElementsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglMultipleElementsOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(40u);
        uint32_t randomState = options.randomSeed;
        for (uint32_t call = 0u; call < RandomPreamble; ++call)
            (void)nextWebglMultipleElementsRandom(randomState);
        const glm::vec3 camera(0.0f, 0.0f, 2.0f);
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 1.0f, 10.0f);
        const double referenceTime = ReferenceEpochSeconds +
            double(options.targetFrame) * FrameStepSeconds;
        const float rotation = static_cast<float>(std::fmod(referenceTime, 2.0 * Pi));
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            appendWebglMultipleElementsCardBackground(entity);
            const uint32_t geometryIndex = static_cast<uint32_t>(nextWebglMultipleElementsRandom(randomState) * 4.0f);
            const float hue = nextWebglMultipleElementsRandom(randomState);
            if (geometryIndex == 0u) buildWebglMultipleElementsBox(entity);
            else if (geometryIndex == 1u) buildWebglMultipleElementsSphere(entity);
            else if (geometryIndex == 2u) buildWebglMultipleElementsDodecahedron(entity);
            else buildWebglMultipleElementsCylinder(entity);
            for (uint32_t call = 2u; call < RandomStridePerScene; ++call)
                (void)nextWebglMultipleElementsRandom(randomState);
            const uint32_t cardColumn = index % 3u;
            const uint32_t cardRow = index / 3u;
            const float cardX = 26.0f + 252.0f * float(cardColumn);
            const float cardTop = 65.0f + 285.0f * float(cardRow);
            const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            entity.objectData.viewport = glm::vec4(
                cardX / float(options.width),
                cardTop / float(options.height),
                200.0f / float(options.width),
                200.0f / float(options.height));
            entity.objectData.baseColorAndFlags = glm::vec4(1.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = glm::vec4(webglMultipleElementsHslToLinear(hue), 1.0f);
        }
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandles[index]);
            if (!encoder) throw std::runtime_error("webgl_multiple_elements could not create its RenderSet encoder.");
            const auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebglMultipleElementsBuffer(allocation, WebglMultipleElementsSceneRenderSetComponents::vertices, "WebglMultipleElementsVertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglMultipleElementsBuffer(allocation, WebglMultipleElementsSceneRenderSetComponents::indices, "WebglMultipleElementsIndices", entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglMultipleElementsBuffer(allocation, WebglMultipleElementsSceneRenderSetComponents::objects, "WebglMultipleElementsObject", &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebglMultipleElementsBuffer(allocation, WebglMultipleElementsSceneRenderSetComponents::instances, "WebglMultipleElementsInstance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebglMultipleElementsBuffer(allocation, WebglMultipleElementsSceneRenderSetComponents::materials, "WebglMultipleElementsMaterial", &entity.materialData, sizeof(entity.materialData), 1u);
            encoder->allocEntity(allocation);
            renderer.executeRenderSetCommand(SceneRenderSetHandles[index], encoder);
        }
    }

    void WebglMultipleElementsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMultipleElementsRuntimeAdapter::afterFrame(
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
        prepareWebglMultipleElementsPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebglMultipleElementsPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            const bool replayScenario = options.scenarioId == "scrolled-cards" || options.scenarioId == "card-orbit";
            const char *replayHash = options.scenarioId == "scrolled-cards"
                ? WebglMultipleElementsScrollReplaySha256
                : WebglMultipleElementsOrbitReplaySha256;
            const uint32_t replayEventCount = options.scenarioId == "scrolled-cards" ? 1u : 4u;
            output << "{\n  \"schemaVersion\": 1,\n"
                   << "  \"caseId\": \"webgl_multiple_elements\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n  \"byteCount\": " << byteCount << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"samplePolicy\": {\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                   << "  \"gpuWorkDslOnly\": true,\n  \"inputReplay\": ";
            if (replayScenario)
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgl_multiple_elements\",\"scenarioId\":\""
                       << options.scenarioId.c_str() << "\",\"captureFrame\":121,\"sha256\":\""
                       << replayHash << "\",\"target\":\"#c\",\"eventCount\":" << replayEventCount << "}";
            }
            else output << "null";
            const uint32_t visibleStart = replayScenario ? 6u : 0u;
            output << ",\n  \"sceneRenderSetCount\": 40,\n  \"renderSetType\": \"WebglMultipleElementsSceneRenderSet\",\n"
                   << "  \"entityCount\": 40,\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index) output << (index == 0u ? "1" : ",1");
            output << "],\n  \"sceneRoots\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << "{\"id\":\"scene-" << std::setfill('0') << std::setw(2) << index
                       << "\",\"renderSetCount\":1,\"renderSetType\":\"WebglMultipleElementsSceneRenderSet\"}";
            }
            output << "],\n  \"scenePassCount\": 6,\n  \"screenPassCount\": 2,\n"
                   << "  \"drawCommandCount\": 6,\n  \"directDrawFallback\": false,\n"
                   << "  \"sampleCount\": 1,\n  \"msaaEnabled\": false\n}\n";
        }
        prepareWebglMultipleElementsPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            const bool replayScenario = options.scenarioId == "scrolled-cards" || options.scenarioId == "card-orbit";
            const uint32_t visibleStart = replayScenario ? 6u : 0u;
            std::ofstream snapshot(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            snapshot << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_multiple_elements\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex << ",\n"
                     << "  \"renderSetPolicy\":\"required\",\n  \"gpuWorkDslOnly\":true,\n"
                     << "  \"sceneRenderSetCount\":40,\n  \"renderableObjectCount\":40,\n  \"scenePassCount\":6,\n"
                     << "  \"screenPassCount\":2,\n  \"drawCommandCount\":6,\n  \"directDrawFallback\":false,\n"
                     << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"sceneRoots\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) snapshot << ',';
                const bool visible = index >= visibleStart && index < visibleStart + 6u;
                std::ostringstream passName;
                passName << "WebglMultipleElementsScene" << std::setfill('0') << std::setw(2) << index << "Pass";
                snapshot << "{\"id\":\"scene-" << std::setfill('0') << std::setw(2) << index
                         << "\",\"renderSetCount\":1,\"renderSetId\":\"scene-" << std::setfill('0') << std::setw(2) << index
                         << "\",\"renderSetType\":\"WebglMultipleElementsSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,"
                         << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"scene-" << std::setfill('0') << std::setw(2) << index
                         << "-object\",\"instanceCount\":1}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                         << "\"drawCommandCount\":" << (visible ? 1 : 0) << ",\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\""
                         << passName.str() << "\",\"renderSetId\":\"scene-" << std::setfill('0') << std::setw(2) << index
                         << "\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
                         << (visible ? 1 : 0) << ",\"drawCommandCount\":" << (visible ? 1 : 0)
                         << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}";
            }
            snapshot << "],\n  \"screenPasses\":[{\"name\":\"card-background-and-shadow-compose\",\"drawMode\":\"fullscreen-triangle\"},{\"name\":\"card-label-and-info-glyph-compose\",\"drawMode\":\"fullscreen-triangle\"}],\n  \"scenePassSequence\":[";
            for (uint32_t ordinal = 0u; ordinal < 6u; ++ordinal)
            {
                if (ordinal != 0u) snapshot << ',';
                snapshot << "{\"sceneRoot\":\"scene-" << std::setfill('0') << std::setw(2) << (visibleStart + ordinal)
                         << "\",\"scenePass\":\"main\",\"entityOrdinal\":0}";
            }
            snapshot << "]\n}\n";
        }
        captureWritten = true;
    }

    void WebglMultipleElementsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
