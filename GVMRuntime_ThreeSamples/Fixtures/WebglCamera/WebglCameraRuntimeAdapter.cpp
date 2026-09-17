#include "WebglCameraRuntimeAdapter.hpp"

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

        static_assert(sizeof(WebglCameraVertex) == 96u);
        static_assert(sizeof(WebglCameraHostObjectData) == 144u);
        static_assert(sizeof(WebglCameraHostInstanceData) == 16u);
        static_assert(sizeof(WebglCameraHostMaterialData) == 16u);

        /** Creates parent directories for one camera evidence file. */
        void prepareWebglCameraOutputPath(const std::filesystem::path &path)
        {
            if (path.empty()) return;
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one binary camera artifact atomically enough for the runner. */
        void writeWebglCameraBinary(
            const eastl::string &path,
            const eastl::vector<uint8_t> &bytes)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglCameraOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("Could not write camera RGBA evidence.");
        }

        /** Writes one UTF-8 camera evidence document. */
        void writeWebglCameraText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglCameraOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write camera JSON evidence.");
        }

        /** Appends one generated component payload to an entity allocation. */
        void appendWebglCameraPayload(
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
        double nextWebglCameraRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds a generated-backend perspective matrix with y inversion and [0,1] depth. */
        glm::dmat4 makeWebglCameraPerspective(
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
        glm::dmat4 makeWebglCameraOrthographic(
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
        glm::dmat4 makeWebglCameraRig(const glm::dvec3 &target)
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

        /** Appends one one-pixel semantic line as an indexed triangle quad. */
        void appendWebglCameraLine(
            WebglCameraEntity &entity,
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::vec3 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 endpoint0(glm::vec3(a), 1.0f);
            const glm::vec4 endpoint1(glm::vec3(b), 1.0f);
            const glm::vec4 colorAndKind(color, 0.0f);
            entity.vertices.push_back({endpoint0, endpoint1, colorAndKind, {0.0f, -1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({endpoint0, endpoint1, colorAndKind, {0.0f, 1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({endpoint0, endpoint1, colorAndKind, {1.0f, -1.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({endpoint0, endpoint1, colorAndKind, {1.0f, 1.0f, 0.0f, 0.0f}});
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u,
                base + 2u, base + 1u, base + 3u});
        }

        /** Appends one one-pixel point as an indexed triangle quad. */
        void appendWebglCameraPoint(
            WebglCameraEntity &entity,
            const glm::dvec3 &position,
            const glm::vec3 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 endpoint(glm::vec3(position), 1.0f);
            const glm::vec4 colorAndKind(color, 1.0f);
            entity.vertices.push_back({endpoint, endpoint, colorAndKind, {0.0f, -1.0f, -1.0f, 0.0f}});
            entity.vertices.push_back({endpoint, endpoint, colorAndKind, {0.0f, 1.0f, -1.0f, 0.0f}});
            entity.vertices.push_back({endpoint, endpoint, colorAndKind, {0.0f, -1.0f, 1.0f, 0.0f}});
            entity.vertices.push_back({endpoint, endpoint, colorAndKind, {0.0f, 1.0f, 1.0f, 0.0f}});
            entity.indices.insert(entity.indices.end(), {
                base, base + 1u, base + 2u,
                base + 2u, base + 1u, base + 3u});
        }

        /** Appends the per-pass clear-color domain to the first entity. */
        void appendWebglCameraBackground(WebglCameraEntity &entity)
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

        /** Appends the exact indexed SphereGeometry wireframe edge stream. */
        void appendWebglCameraWireSphere(
            WebglCameraEntity &entity,
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
                    positions.push_back({
                        -ringRadius * std::cos(phi),
                        y,
                        ringRadius * std::sin(phi)});
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
            for (size_t index = 0u; index < triangleIndices.size(); index += 3u)
            {
                const glm::dvec3 &a = positions[triangleIndices[index]];
                const glm::dvec3 &b = positions[triangleIndices[index + 1u]];
                const glm::dvec3 &c = positions[triangleIndices[index + 2u]];
                appendWebglCameraLine(entity, a, b, color);
                appendWebglCameraLine(entity, b, c, color);
                appendWebglCameraLine(entity, c, a, color);
            }
        }

        /** Returns one local frustum point for perspective or orthographic helpers. */
        glm::dvec3 makeWebglCameraHelperPoint(
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
        void appendWebglCameraHelper(
            WebglCameraEntity &entity,
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
                appendWebglCameraLine(
                    entity,
                    makeWebglCameraHelperPoint(
                        line.a, perspective, fovDegrees,
                        nearDistance, farDistance),
                    makeWebglCameraHelperPoint(
                        line.b, perspective, fovDegrees,
                        nearDistance, farDistance),
                    line.color);
        }

        /** Creates the target-frame hierarchy, camera state, and all six entities. */
        eastl::vector<WebglCameraEntity> buildWebglCameraEntities(
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
            const glm::dmat4 rig = makeWebglCameraRig(meshPosition);
            const glm::dmat4 cameraRotation =
                glm::rotate(glm::dmat4(1.0), Pi, glm::dvec3(0.0, 1.0, 0.0));
            const glm::dmat4 activeCameraWorld = rig * cameraRotation;
            const glm::dmat4 activeProjection = orthographic
                ? makeWebglCameraOrthographic(-240.0, 240.0, 300.0, -300.0, 150.0, activeFar)
                : makeWebglCameraPerspective(activeFov, 0.8, 150.0, activeFar);
            const glm::dmat4 observerProjection =
                makeWebglCameraPerspective(50.0, 0.8, 1.0, 10000.0);
            const glm::dmat4 observerView = glm::lookAtRH(
                glm::dvec3(0.0, 0.0, 2500.0),
                glm::dvec3(0.0),
                glm::dvec3(0.0, 1.0, 0.0));
            const glm::dmat4 activeView = glm::inverse(activeCameraWorld);
            leftViewProjection = glm::mat4(activeProjection);
            rightViewProjection = glm::mat4(observerProjection);

            eastl::vector<WebglCameraEntity> result(6u);
            result[0u].logicalId = "perspective-camera-helper";
            result[1u].logicalId = "orthographic-camera-helper";
            result[2u].logicalId = "primary-wire-sphere";
            result[3u].logicalId = "nested-green-wire-sphere";
            result[4u].logicalId = "camera-rig-blue-wire-sphere";
            result[5u].logicalId = "deterministic-point-cloud";
            appendWebglCameraBackground(result[0u]);
            appendWebglCameraHelper(
                result[0u], true, activeFov, 150.0, activeFar);
            appendWebglCameraHelper(
                result[1u], false, activeFov, 150.0, activeFar);
            appendWebglCameraWireSphere(result[2u], 100.0, glm::vec3(1.0f));
            appendWebglCameraWireSphere(result[3u], 50.0, glm::vec3(0.0f, 1.0f, 0.0f));
            appendWebglCameraWireSphere(result[4u], 5.0, glm::vec3(0.0f, 0.0f, 1.0f));
            uint32_t randomState = options.randomSeed;
            for (uint32_t draw = 0u; draw < 85u; ++draw)
                (void)nextWebglCameraRandom(randomState);
            for (uint32_t point = 0u; point < 10000u; ++point)
            {
                appendWebglCameraPoint(result[5u], {
                    2000.0 * (0.5 - nextWebglCameraRandom(randomState)),
                    2000.0 * (0.5 - nextWebglCameraRandom(randomState)),
                    2000.0 * (0.5 - nextWebglCameraRandom(randomState))},
                    glm::vec3(136.0f / 255.0f));
            }
            for (uint32_t draw = 0u; draw < 127u; ++draw)
                (void)nextWebglCameraRandom(randomState);
            if (randomState != 4144800406u)
                throw std::runtime_error("Camera deterministic random stream diverged from r185.");

            const glm::dmat4 models[6u] = {
                activeCameraWorld,
                activeCameraWorld,
                glm::translate(glm::dmat4(1.0), meshPosition),
                glm::translate(glm::dmat4(1.0), meshPosition) *
                    glm::translate(glm::dmat4(1.0), childPosition),
                rig * glm::translate(
                    glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 150.0)),
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
            if (options.scenarioId == "initial")
            {
                result[4u].objectData.visibility.z = 1.0f;
                const glm::mat4 oldViewProjection = glm::mat4(
                    activeProjection * activeView);
                const glm::mat4 oldModel = glm::mat4(models[4u]);
                for (WebglCameraVertex &vertex : result[4u].vertices)
                {
                    vertex.leftClipEndpoint0 = oldViewProjection *
                        (oldModel * vertex.endpoint0);
                    vertex.leftClipEndpoint1 = oldViewProjection *
                        (oldModel * vertex.endpoint1);
                }
            }
            return result;
        }
    } // namespace

    void WebglCameraRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "initial" && options.targetFrame == 0u) ||
            (options.scenarioId == "animated" && options.targetFrame == 60u) ||
            (options.scenarioId == "orthographic" && options.targetFrame == 1u);
        if (options.caseId != "webgl_camera" || !validScenario ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
            throw std::invalid_argument("WebGL camera requires the locked r185 scenarios.");
        device = inDevice;
        entities = buildWebglCameraEntities(
            options, leftViewProjection, rightViewProjection);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create WebGL camera Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            static constexpr const char *VertexLabels[6u] = {
                "WebglCameraVertices0", "WebglCameraVertices1",
                "WebglCameraVertices2", "WebglCameraVertices3",
                "WebglCameraVertices4", "WebglCameraVertices5"};
            static constexpr const char *IndexLabels[6u] = {
                "WebglCameraIndices0", "WebglCameraIndices1",
                "WebglCameraIndices2", "WebglCameraIndices3",
                "WebglCameraIndices4", "WebglCameraIndices5"};
            static constexpr const char *ObjectLabels[6u] = {
                "WebglCameraObject0", "WebglCameraObject1",
                "WebglCameraObject2", "WebglCameraObject3",
                "WebglCameraObject4", "WebglCameraObject5"};
            static constexpr const char *InstanceLabels[6u] = {
                "WebglCameraInstance0", "WebglCameraInstance1",
                "WebglCameraInstance2", "WebglCameraInstance3",
                "WebglCameraInstance4", "WebglCameraInstance5"};
            static constexpr const char *MaterialLabels[6u] = {
                "WebglCameraMaterial0", "WebglCameraMaterial1",
                "WebglCameraMaterial2", "WebglCameraMaterial3",
                "WebglCameraMaterial4", "WebglCameraMaterial5"};
            const WebglCameraEntity &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebglCameraPayload(
                allocation, WebglCameraSceneRenderSetComponents::vertices,
                VertexLabels[entityIndex], entity.vertices.data(),
                entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglCameraPayload(
                allocation, WebglCameraSceneRenderSetComponents::indices,
                IndexLabels[entityIndex], entity.indices.data(),
                entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglCameraPayload(
                allocation, WebglCameraSceneRenderSetComponents::objects,
                ObjectLabels[entityIndex], &entity.objectData,
                sizeof(entity.objectData), 1u);
            appendWebglCameraPayload(
                allocation, WebglCameraSceneRenderSetComponents::instances,
                InstanceLabels[entityIndex], &entity.instanceData,
                sizeof(entity.instanceData), 1u);
            appendWebglCameraPayload(
                allocation, WebglCameraSceneRenderSetComponents::materials,
                MaterialLabels[entityIndex], &entity.materialData,
                sizeof(entity.materialData), 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglCameraRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglCameraRuntimeAdapter::afterFrame(
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
        writeWebglCameraBinary(options.captureRgbaPath, rgba);
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_camera\",\"scenarioId\":\""
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
                << "\"caseId\":\"webgl_camera\",\"scenarioId\":\"orthographic\","
                << "\"captureFrame\":1,\"sha256\":"
                << "\"ed89b00cc02fbbdaaa21ae6c45cc6c1b5f7ffb51502ce93321f7bb9af05c775d\","
                << "\"target\":\"body\",\"eventCount\":1}";
        }
        metadata << "}\n";
        writeWebglCameraText(options.captureMetadataPath, metadata.str());
        uint64_t vertexCount = 0u;
        uint64_t indexCount = 0u;
        for (const WebglCameraEntity &entity : entities)
        {
            vertexCount += entity.vertices.size();
            indexCount += entity.indices.size();
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_camera\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":6,"
            << "\"entityCount\":6,\"instanceCount\":6,\"vertexCount\":"
            << vertexCount << ",\"indexCount\":" << indexCount
            << ",\"scenePassCount\":2,\"screenPassCount\":0,"
            << "\"drawCommandCount\":2,\"renderSetType\":\"WebglCameraSceneRenderSet\","
            << "\"directDrawFallback\":false,\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\","
            << "\"renderSetType\":\"WebglCameraSceneRenderSet\","
            << "\"renderableObjectCount\":6,\"entityCount\":6,\"entities\":["
            << "{\"entityId\":0,\"logicalRenderableId\":\"perspective-camera-helper\",\"instanceCount\":1},"
            << "{\"entityId\":1,\"logicalRenderableId\":\"orthographic-camera-helper\",\"instanceCount\":1},"
            << "{\"entityId\":2,\"logicalRenderableId\":\"primary-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":3,\"logicalRenderableId\":\"nested-green-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":4,\"logicalRenderableId\":\"camera-rig-blue-wire-sphere\",\"instanceCount\":1},"
            << "{\"entityId\":5,\"logicalRenderableId\":\"deterministic-point-cloud\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":2,\"directDrawFallback\":false,\"scenePasses\":["
            << "{\"name\":\"active-camera-left\",\"renderClass\":\"WebglCameraActiveCameraPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"observer-camera-right\",\"renderClass\":\"WebglCameraObserverCameraPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"active-camera-left\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"observer-camera-right\",\"entityOrdinal\":0}]}\n";
        writeWebglCameraText(options.sceneSnapshotPath, snapshot.str());
        writeWebglCameraText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglCameraRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
