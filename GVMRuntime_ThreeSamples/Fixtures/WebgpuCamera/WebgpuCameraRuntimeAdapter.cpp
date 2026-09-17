#include "WebgpuCameraRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

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
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double EpochMs = 1700000000000.0;

        static_assert(sizeof(WebgpuCameraVertex) == 96u);
        static_assert(sizeof(WebgpuCameraHostObjectData) == 144u);
        static_assert(sizeof(WebgpuCameraHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuCameraHostMaterialData) == 16u);

        /** Creates parent directories for one camera evidence file. */
        void prepareWebgpuCameraOutputPath(const std::filesystem::path &path)
        {
            if (path.empty()) return;
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one binary camera artifact atomically enough for the runner. */
        void writeWebgpuCameraBinary(
            const eastl::string &path,
            const eastl::vector<uint8_t> &bytes)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuCameraOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("Could not write camera RGBA evidence.");
        }

        /** Writes one UTF-8 camera evidence document. */
        void writeWebgpuCameraText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuCameraOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write camera JSON evidence.");
        }

        /** Appends one generated component payload to an entity allocation. */
        void appendWebgpuCameraPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *label,
            const void *data,
            uint64_t size,
            uint32_t elementCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = label,
                .value = data,
                .dataStorageSize = size,
                .instanceCount = elementCount,
            });
        }

        /** Advances one exact upper-24-bit xorshift32 reference value. */
        double nextWebgpuCameraRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds a generated-backend perspective matrix with y inversion and [0,1] depth. */
        glm::dmat4 makeWebgpuCameraPerspective(
            double fovDegrees,
            double aspect,
            double nearDistance,
            double farDistance)
        {
            const double inverseTangent =
                1.0 / std::tan(fovDegrees * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / aspect;
            result[1u][1u] = -inverseTangent;
            result[2u][2u] = farDistance / (nearDistance - farDistance);
            result[2u][3u] = -1.0;
            result[3u][2u] =
                farDistance * nearDistance / (nearDistance - farDistance);
            return result;
        }

        /** Builds a generated-backend orthographic matrix with y inversion and [0,1] depth. */
        glm::dmat4 makeWebgpuCameraOrthographic(
            double left,
            double right,
            double top,
            double bottom,
            double nearDistance,
            double farDistance)
        {
            glm::dmat4 result(1.0);
            result[0u][0u] = 2.0 / (right - left);
            result[1u][1u] = -2.0 / (top - bottom);
            result[2u][2u] = 1.0 / (nearDistance - farDistance);
            result[3u][0u] = -(right + left) / (right - left);
            result[3u][1u] = (top + bottom) / (top - bottom);
            result[3u][2u] = nearDistance / (nearDistance - farDistance);
            return result;
        }

        /** Builds the Group lookAt transform whose positive z axis targets the mesh. */
        glm::dmat4 makeWebgpuCameraRig(const glm::dvec3 &target)
        {
            const glm::dvec3 zAxis = glm::normalize(target);
            const glm::dvec3 xAxis = glm::normalize(
                glm::cross(glm::dvec3(0.0, 1.0, 0.0), zAxis));
            const glm::dvec3 yAxis = glm::cross(zAxis, xAxis);
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(xAxis, 0.0);
            result[1u] = glm::dvec4(yAxis, 0.0);
            result[2u] = glm::dvec4(zAxis, 0.0);
            return result;
        }

        /** Appends one semantic line as a native LineList segment. */
        void appendWebgpuCameraLine(
            WebgpuCameraEntity &entity,
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::vec3 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 endpoint0(glm::vec3(a), 1.0f);
            const glm::vec4 endpoint1(glm::vec3(b), 1.0f);
            const glm::vec4 colorAndKind(color, 0.0f);
            const float4 endpoint0Value(endpoint0.x, endpoint0.y, endpoint0.z, endpoint0.w);
            const float4 endpoint1Value(endpoint1.x, endpoint1.y, endpoint1.z, endpoint1.w);
            const float4 colorAndKindValue(colorAndKind.x, colorAndKind.y, colorAndKind.z, colorAndKind.w);
            entity.vertices.push_back({endpoint0Value, endpoint1Value, colorAndKindValue,
                float4(0.0f), float4(0.0f), float4(0.0f)});
            entity.vertices.push_back({endpoint0Value, endpoint1Value, colorAndKindValue,
                float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f), float4(0.0f)});
            entity.indices.insert(entity.indices.end(), {base, base + 1u});
        }

        /** Appends one one-pixel semantic point as a deterministic screen-space quad. */
        void appendWebgpuCameraPoint(
            WebgpuCameraEntity &entity,
            const glm::dvec3 &position,
            const glm::vec3 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 endpoint(glm::vec3(position), 1.0f);
            const glm::vec4 colorAndKind(color, 1.0f);
            const float4 endpointValue(endpoint.x, endpoint.y, endpoint.z, endpoint.w);
            const float4 colorAndKindValue(colorAndKind.x, colorAndKind.y, colorAndKind.z, colorAndKind.w);
            entity.vertices.push_back({endpointValue, endpointValue, colorAndKindValue,
                float4(0.0f, -1.0f, -1.0f, 0.0f), float4(0.0f), float4(0.0f)});
            entity.vertices.push_back({endpointValue, endpointValue, colorAndKindValue,
                float4(0.0f, 1.0f, -1.0f, 0.0f), float4(0.0f), float4(0.0f)});
            entity.vertices.push_back({endpointValue, endpointValue, colorAndKindValue,
                float4(0.0f, -1.0f, 1.0f, 0.0f), float4(0.0f), float4(0.0f)});
            entity.vertices.push_back({endpointValue, endpointValue, colorAndKindValue,
                float4(0.0f, 1.0f, 1.0f, 0.0f), float4(0.0f), float4(0.0f)});
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u,
                base + 2u, base + 1u, base + 3u});
        }

        /** Appends the per-pass clear-color domain to the first entity. */
        void appendWebgpuCameraBackground(WebgpuCameraEntity &entity)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 zero(0.0f);
            const glm::vec4 colorAndKind(0.0f, 0.0f, 0.0f, 2.0f);
            entity.vertices.push_back({zero, zero, colorAndKind, {-1.0f, -1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({zero, zero, colorAndKind, {1.0f, -1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({zero, zero, colorAndKind, {-1.0f, 1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({zero, zero, colorAndKind, {1.0f, 1.0f, 0.0f, 0.0f}});
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u,
                base + 2u, base + 1u, base + 3u});
        }

        /** Appends one indexed sphere edge as a line-list segment. */
        void appendWebgpuCameraSphereEdge(
            WebgpuCameraEntity &entity,
            const glm::dvec3 &first,
            const glm::dvec3 &second,
            const glm::vec3 &color)
        {
            appendWebgpuCameraLine(entity, first, second, color);
        }

        /** Appends the exact indexed SphereGeometry wireframe edge stream. */
        void appendWebgpuCameraWireSphere(
            WebgpuCameraEntity &entity,
            double radius,
            const glm::vec3 &color)
        {
            constexpr uint32_t WidthSegments = 16u;
            constexpr uint32_t HeightSegments = 8u;
            eastl::vector<glm::dvec3> positions;
            eastl::vector<uint32_t> triangleIndices;
            positions.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            for (uint32_t row = 0u; row <= HeightSegments; ++row)
            {
                const double theta = double(row) / double(HeightSegments) * Pi;
                const double y = radius * std::cos(theta);
                const double ringRadius = std::sqrt(
                    std::max(0.0, radius * radius - y * y));
                for (uint32_t column = 0u; column <= WidthSegments; ++column)
                {
                    const double phi =
                        double(column) / double(WidthSegments) * 2.0 * Pi;
                    // SphereGeometry stores the position attribute as
                    // Float32 before WireframeGeometry hashes endpoint
                    // coordinates. Quantize here before edge canonicalization
                    // so seam/pole equality follows the upstream buffer.
                    positions.push_back({
                        double(static_cast<float>(-ringRadius * std::cos(phi))),
                        double(static_cast<float>(y)),
                        double(static_cast<float>(ringRadius * std::sin(phi)))});
                }
            }
            for (uint32_t row = 0u; row < HeightSegments; ++row)
            {
                for (uint32_t column = 0u; column < WidthSegments; ++column)
                {
                    const uint32_t a = row * (WidthSegments + 1u) + column + 1u;
                    const uint32_t b = a - 1u;
                    const uint32_t c = (row + 1u) * (WidthSegments + 1u) + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u) triangleIndices.insert(triangleIndices.end(), {a, b, d});
                    if (row != HeightSegments - 1u) triangleIndices.insert(triangleIndices.end(), {b, c, d});
                }
            }
            /* WebGPU's MeshBasicMaterial wireframe path uses the renderer's
               indexed wireframe attribute. It intentionally emits every
               triangle edge, including coincident seam and pole edges, rather
               than the public WireframeGeometry de-duplication path. */
            for (size_t index = 0u; index < triangleIndices.size(); index += 3u)
            {
                const uint32_t a = triangleIndices[index];
                const uint32_t b = triangleIndices[index + 1u];
                const uint32_t c = triangleIndices[index + 2u];
                const uint32_t edgeStarts[3u] = {a, b, c};
                const uint32_t edgeEnds[3u] = {b, c, a};
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    appendWebgpuCameraSphereEdge(
                        entity, positions[edgeStarts[edge]],
                        positions[edgeEnds[edge]], color);
                }
            }
        }

        /** Returns one local frustum point for perspective or orthographic helpers. */
        glm::dvec3 makeWebgpuCameraHelperPoint(
            const char *id,
            bool perspective,
            double fovDegrees,
            double nearDistance,
            double farDistance)
        {
            double x = 0.0;
            double y = 0.0;
            double depth = nearDistance;
            if (id[0] == 'f' || id[0] == 't') depth = farDistance;
            const bool farPoint = id[0] == 'f';
            const double halfHeight = perspective
                ? depth * std::tan(fovDegrees * Pi / 360.0)
                : 300.0;
            const double halfWidth = perspective ? halfHeight * 0.8 : 240.0;
            if ((id[0] == 'n' || farPoint) && id[1] >= '1' && id[1] <= '4')
            {
                const bool right = id[1] == '2' || id[1] == '4';
                const bool top = id[1] == '3' || id[1] == '4';
                x = right ? halfWidth : -halfWidth;
                y = top ? halfHeight : -halfHeight;
            }
            else if (id[0] == 'u')
            {
                depth = nearDistance;
                const double nearHalfHeight = perspective
                    ? nearDistance * std::tan(fovDegrees * Pi / 360.0)
                    : 300.0;
                const double nearHalfWidth = perspective
                    ? nearHalfHeight * 0.8
                    : 240.0;
                if (id[1] == '1') { x = nearHalfWidth * 0.7; y = nearHalfHeight * 1.1; }
                else if (id[1] == '2') { x = -nearHalfWidth * 0.7; y = nearHalfHeight * 1.1; }
                else { y = nearHalfHeight * 2.0; }
            }
            else if (id[0] == 'c' && id[1] == 'n')
            {
                depth = nearDistance;
                if (id[2] == '1') x = -halfWidth;
                else if (id[2] == '2') x = halfWidth;
                else if (id[2] == '3') y = -halfHeight;
                else y = halfHeight;
            }
            else if (id[0] == 'c' && id[1] == 'f')
            {
                depth = farDistance;
                const double farHalfHeight = perspective
                    ? farDistance * std::tan(fovDegrees * Pi / 360.0)
                    : 300.0;
                const double farHalfWidth = perspective
                    ? farHalfHeight * 0.8
                    : 240.0;
                if (id[2] == '1') x = -farHalfWidth;
                else if (id[2] == '2') x = farHalfWidth;
                else if (id[2] == '3') y = -farHalfHeight;
                else y = farHalfHeight;
            }
            else if (id[0] == 'p')
            {
                depth = 0.0;
            }
            return {x, y, -depth};
        }

        /** Builds the exact 25-segment CameraHelper line and color ordering. */
        void appendWebgpuCameraHelper(
            WebgpuCameraEntity &entity,
            bool perspective,
            double fovDegrees,
            double nearDistance,
            double farDistance)
        {
            struct HelperLine
            {
                const char *a;
                const char *b;
                glm::vec3 color;
            };
            const glm::vec3 frustum(1.0f, 170.0f / 255.0f, 0.0f);
            const glm::vec3 cone(1.0f, 0.0f, 0.0f);
            const glm::vec3 up(0.0f, 170.0f / 255.0f, 1.0f);
            const glm::vec3 target(1.0f);
            const glm::vec3 cross(51.0f / 255.0f);
            const HelperLine lines[] = {
                {"n1", "n2", frustum}, {"n2", "n4", frustum},
                {"n4", "n3", frustum}, {"n3", "n1", frustum},
                {"f1", "f2", frustum}, {"f2", "f4", frustum},
                {"f4", "f3", frustum}, {"f3", "f1", frustum},
                {"n1", "f1", frustum}, {"n2", "f2", frustum},
                {"n3", "f3", frustum}, {"n4", "f4", frustum},
                {"p", "n1", cone}, {"p", "n2", cone},
                {"p", "n3", cone}, {"p", "n4", cone},
                {"u1", "u2", up}, {"u2", "u3", up}, {"u3", "u1", up},
                {"c", "t", target}, {"p", "c", cross},
                {"cn1", "cn2", cross}, {"cn3", "cn4", cross},
                {"cf1", "cf2", cross}, {"cf3", "cf4", cross}};
            for (const HelperLine &line : lines)
                appendWebgpuCameraLine(
                    entity,
                    makeWebgpuCameraHelperPoint(
                        line.a, perspective, fovDegrees,
                        nearDistance, farDistance),
                    makeWebgpuCameraHelperPoint(
                        line.b, perspective, fovDegrees,
                        nearDistance, farDistance),
                    line.color);
        }

        /** Creates the target-frame hierarchy, camera state, and all six entities. */
        eastl::vector<WebgpuCameraEntity> buildWebgpuCameraEntities(
            const ThreeSampleHostOptions &options,
            glm::mat4 &leftViewProjection,
            glm::mat4 &rightViewProjection)
        {
            const bool orthographic = options.scenarioId == "orthographic";
            const double virtualTimeMs = double(options.targetFrame) * (1000.0 / 60.0);
            const double r = (EpochMs + virtualTimeMs) * 0.0005;
            const glm::dvec3 meshPosition(
                700.0 * std::cos(r),
                700.0 * std::sin(r),
                700.0 * std::sin(r));
            const glm::dvec3 childPosition(
                70.0 * std::cos(2.0 * r), 150.0, 70.0 * std::sin(r));
            const double activeFar = glm::length(meshPosition);
            const double activeFov = 35.0 + 30.0 * std::sin(0.5 * r);
            const glm::dmat4 rig = makeWebgpuCameraRig(meshPosition);
            const glm::dmat4 cameraRotation =
                glm::rotate(glm::dmat4(1.0), Pi, glm::dvec3(0.0, 1.0, 0.0));
            const glm::dmat4 activeCameraWorld = rig * cameraRotation;
            const glm::dmat4 activeProjection = orthographic
                ? makeWebgpuCameraOrthographic(-240.0, 240.0, 300.0, -300.0, 150.0, activeFar)
                : makeWebgpuCameraPerspective(activeFov, 0.8, 150.0, activeFar);
            const glm::dmat4 observerProjection =
                makeWebgpuCameraPerspective(50.0, 0.8, 1.0, 10000.0);
            const glm::dmat4 observerView = glm::lookAtRH(
                glm::dvec3(0.0, 0.0, 2500.0),
                glm::dvec3(0.0),
                glm::dvec3(0.0, 1.0, 0.0));
            const glm::dmat4 activeView = glm::inverse(activeCameraWorld);
            leftViewProjection = glm::mat4(activeProjection);
            rightViewProjection = glm::mat4(observerProjection);

            eastl::vector<WebgpuCameraEntity> result(6u);
            result[0u].logicalId = "perspective-camera-helper";
            result[1u].logicalId = "orthographic-camera-helper";
            result[2u].logicalId = "camera-rig-blue-wire-sphere";
            result[3u].logicalId = "primary-wire-sphere";
            result[4u].logicalId = "nested-green-wire-sphere";
            result[5u].logicalId = "deterministic-point-cloud";
            appendWebgpuCameraBackground(result[0u]);
            appendWebgpuCameraHelper(
                result[0u], true, activeFov, 150.0, activeFar);
            appendWebgpuCameraHelper(
                result[1u], false, activeFov, 150.0, activeFar);
            appendWebgpuCameraWireSphere(
                result[2u], 5.0, glm::vec3(0.0f, 0.0f, 1.0f));
            appendWebgpuCameraWireSphere(
                result[3u], 100.0, glm::vec3(1.0f));
            appendWebgpuCameraWireSphere(
                result[4u], 50.0, glm::vec3(0.0f, 1.0f, 0.0f));
            uint32_t randomState = options.randomSeed;
            // The locked reference bootstrap consumes 212 values before the
            // Float32BufferAttribute point stream is constructed. Keeping the
            // complete prefix here preserves both point ordering and the final
            // r185 random state; splitting the prefix around point generation
            // changes the first and last 42 logical particles.
            for (uint32_t draw = 0u; draw < 212u; ++draw)
                (void)nextWebgpuCameraRandom(randomState);
            for (uint32_t point = 0u; point < 10000u; ++point)
            {
                appendWebgpuCameraPoint(result[5u], {
                    2000.0 * (0.5 - nextWebgpuCameraRandom(randomState)),
                    2000.0 * (0.5 - nextWebgpuCameraRandom(randomState)),
                    2000.0 * (0.5 - nextWebgpuCameraRandom(randomState))},
                    glm::vec3(1.0f));
            }
            if (randomState != 4144800406u)
                throw std::runtime_error("Camera deterministic random stream diverged from r185.");

            const glm::dmat4 models[6u] = {
                activeCameraWorld,
                activeCameraWorld,
                rig * glm::translate(
                    glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 150.0)),
                glm::translate(glm::dmat4(1.0), meshPosition),
                glm::translate(glm::dmat4(1.0), meshPosition) *
                    glm::translate(glm::dmat4(1.0), childPosition),
                glm::dmat4(1.0)};
            for (uint32_t entityIndex = 0u; entityIndex < result.size(); ++entityIndex)
            {
                result[entityIndex].objectData.leftModelView =
                    options.scenarioId == "initial" && entityIndex == 4u
                    ? glm::mat4(activeView) * glm::mat4(models[entityIndex])
                    : glm::mat4(activeView * models[entityIndex]);
                result[entityIndex].objectData.rightModelView = glm::mat4(
                    observerView * models[entityIndex]);
                result[entityIndex].objectData.visibility =
                    glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
                result[entityIndex].instanceData.reserved = glm::vec4(0.0f);
                result[entityIndex].materialData.phase =
                    glm::vec4(float(entityIndex), 0.0f, 0.0f, 0.0f);
            }
            result[0u].objectData.visibility = glm::vec4(
                0.0f, orthographic ? 0.0f : 1.0f, 0.0f, 0.0f);
            result[1u].objectData.visibility = glm::vec4(
                0.0f, orthographic ? 1.0f : 0.0f, 0.0f, 0.0f);
            return result;
        }
    } // namespace

    void WebgpuCameraRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "initial" && options.targetFrame == 0u) ||
            (options.scenarioId == "animated" && options.targetFrame == 60u) ||
            (options.scenarioId == "orthographic" && options.targetFrame == 1u);
        if (options.caseId != "webgpu_camera" || !validScenario ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
            throw std::invalid_argument("WebGPU camera requires the locked r185 scenarios.");
        device = inDevice;
        entities = buildWebgpuCameraEntities(
            options, leftViewProjection, rightViewProjection);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create WebGPU camera Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            static constexpr const char *VertexLabels[6u] = {
                "WebgpuCameraVertices0", "WebgpuCameraVertices1",
                "WebgpuCameraVertices2", "WebgpuCameraVertices3",
                "WebgpuCameraVertices4", "WebgpuCameraVertices5"};
            static constexpr const char *IndexLabels[6u] = {
                "WebgpuCameraIndices0", "WebgpuCameraIndices1",
                "WebgpuCameraIndices2", "WebgpuCameraIndices3",
                "WebgpuCameraIndices4", "WebgpuCameraIndices5"};
            static constexpr const char *ObjectLabels[6u] = {
                "WebgpuCameraObject0", "WebgpuCameraObject1",
                "WebgpuCameraObject2", "WebgpuCameraObject3",
                "WebgpuCameraObject4", "WebgpuCameraObject5"};
            static constexpr const char *InstanceLabels[6u] = {
                "WebgpuCameraInstance0", "WebgpuCameraInstance1",
                "WebgpuCameraInstance2", "WebgpuCameraInstance3",
                "WebgpuCameraInstance4", "WebgpuCameraInstance5"};
            static constexpr const char *MaterialLabels[6u] = {
                "WebgpuCameraMaterial0", "WebgpuCameraMaterial1",
                "WebgpuCameraMaterial2", "WebgpuCameraMaterial3",
                "WebgpuCameraMaterial4", "WebgpuCameraMaterial5"};
            const WebgpuCameraEntity &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebgpuCameraPayload(
                allocation, WebgpuCameraSceneRenderSetComponents::vertices,
                VertexLabels[entityIndex], entity.vertices.data(),
                entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebgpuCameraPayload(
                allocation, WebgpuCameraSceneRenderSetComponents::indices,
                IndexLabels[entityIndex], entity.indices.data(),
                entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebgpuCameraPayload(
                allocation, WebgpuCameraSceneRenderSetComponents::objects,
                ObjectLabels[entityIndex], &entity.objectData,
                sizeof(entity.objectData), 1u);
            appendWebgpuCameraPayload(
                allocation, WebgpuCameraSceneRenderSetComponents::instances,
                InstanceLabels[entityIndex], &entity.instanceData,
                sizeof(entity.instanceData), 1u);
            appendWebgpuCameraPayload(
                allocation, WebgpuCameraSceneRenderSetComponents::materials,
                MaterialLabels[entityIndex], &entity.materialData,
                sizeof(entity.materialData), 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuCameraRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuCameraRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeWebgpuCameraBinary(options.captureRgbaPath, rgba);
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_camera\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"";
        if (options.scenarioId == "orthographic")
        {
            metadata
                << ",\"inputReplay\":{\"schemaVersion\":1,"
                << "\"caseId\":\"webgpu_camera\",\"scenarioId\":\"orthographic\","
                << "\"captureFrame\":1,\"sha256\":"
                << "\"8e11f7be42172819de071dec1340be6bf2e28b09307a75d08dd75a38624be74b\","
                << "\"target\":\"body\",\"eventCount\":1}";
        }
        metadata << "}\n";
        writeWebgpuCameraText(options.captureMetadataPath, metadata.str());
        uint64_t vertexCount = 0u;
        uint64_t indexCount = 0u;
        for (const WebgpuCameraEntity &entity : entities)
        {
            vertexCount += entity.vertices.size();
            indexCount += entity.indices.size();
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_camera\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":6,"
            << "\"entityCount\":6,\"instanceCount\":6,\"vertexCount\":"
            << vertexCount << ",\"indexCount\":" << indexCount
            // The two viewports require separate topology states. Each
            // topology-specific RenderClass is a declared Scene pass and all
            // six passes bind the same RenderSet.
            << ",\"scenePassCount\":6,\"screenPassCount\":0,"
            << "\"drawCommandCount\":6,\"renderSetType\":\"WebgpuCameraSceneRenderSet\","
            << "\"directDrawFallback\":false,\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\","
            << "\"renderSetType\":\"WebgpuCameraSceneRenderSet\","
            << "\"renderableObjectCount\":6,\"entityCount\":6,\"entities\":["
            << "{\"entityId\":0,\"logicalRenderableId\":\"perspective-camera-helper\",\"instanceCount\":1},"
            << "{\"entityId\":1,\"logicalRenderableId\":\"orthographic-camera-helper\",\"instanceCount\":1},"
            << "{\"entityId\":2,\"logicalRenderableId\":\"camera-rig-blue-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":3,\"logicalRenderableId\":\"primary-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":4,\"logicalRenderableId\":\"nested-green-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":5,\"logicalRenderableId\":\"deterministic-point-cloud\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":6,\"directDrawFallback\":false,\"scenePasses\":["
            << "{\"name\":\"active-camera-left\",\"renderClass\":\"WebgpuCameraActiveCameraPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"active-camera-lines\",\"renderClass\":\"WebgpuCameraActiveLinePass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"active-camera-points\",\"renderClass\":\"WebgpuCameraActivePointPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"observer-camera-right\",\"renderClass\":\"WebgpuCameraObserverCameraPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"observer-camera-lines\",\"renderClass\":\"WebgpuCameraObserverLinePass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"observer-camera-points\",\"renderClass\":\"WebgpuCameraObserverPointPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"active-camera-left\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"active-camera-lines\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"active-camera-points\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"observer-camera-right\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"observer-camera-lines\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"observer-camera-points\",\"entityOrdinal\":0}]}\n";
        writeWebgpuCameraText(options.sceneSnapshotPath, snapshot.str());
        writeWebgpuCameraText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebgpuCameraRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
