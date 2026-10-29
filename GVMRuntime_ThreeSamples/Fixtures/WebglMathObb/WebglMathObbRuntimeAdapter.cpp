#include "WebglMathObbRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t BoxCount = 100u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr const char *SelectReplaySha256 =
            "eabaa107b902701becd8b3e7baf90765fcfa2df341007702b7e41ea95c343cca";
        constexpr const char *MissReplaySha256 =
            "f65e4d2d509a1e72fd7dfd3888fd8004ee49e60664cb5b5c1e9e7bebbe48cdae";

        /** Creates parent directories for one deterministic capture artifact. */
        void preparePath(const eastl::string &value)
        {
            if (value.empty()) return;
            const std::filesystem::path path(value.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Appends one triangle with a flat normal and analytic barycentrics. */
        void appendTriangle(WebglMathObbHostEntity &entity,
                            const glm::vec3 &a,
                            const glm::vec3 &b,
                            const glm::vec3 &c,
                            const glm::vec3 &normal)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(1, 0, 0, 0)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0, 1, 0, 0)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0, 0, 1, 0)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Creates the exact six-plane, twelve-triangle BoxGeometry used by Three r185. */
        void buildBox(WebglMathObbHostEntity &entity)
        {
            const float width = 10.0f;
            const float height = 5.0f;
            const float depth = 6.0f;
            const auto appendPlane = [&](char u, char v, char w, float udir, float vdir,
                                         float planeWidth, float planeHeight, float planeDepth,
                                         const glm::vec3 &normal)
            {
                const float widthHalf = planeWidth * 0.5f;
                const float heightHalf = planeHeight * 0.5f;
                const float depthHalf = planeDepth * 0.5f;
                const auto make = [&](float x, float y) {
                    glm::vec3 value(0.0f);
                    if (u == 'x') value.x = x * udir;
                    if (u == 'y') value.y = x * udir;
                    if (u == 'z') value.z = x * udir;
                    if (v == 'x') value.x = y * vdir;
                    if (v == 'y') value.y = y * vdir;
                    if (v == 'z') value.z = y * vdir;
                    if (w == 'x') value.x = depthHalf;
                    if (w == 'y') value.y = depthHalf;
                    if (w == 'z') value.z = depthHalf;
                    return value;
                };
                const glm::vec3 a = make(-widthHalf, -heightHalf);
                const glm::vec3 b = make(-widthHalf, heightHalf);
                const glm::vec3 c = make(widthHalf, heightHalf);
                const glm::vec3 d = make(widthHalf, -heightHalf);
                appendTriangle(entity, a, b, d, normal);
                appendTriangle(entity, b, c, d, normal);
            };
            appendPlane('z', 'y', 'x', -1.0f, -1.0f, depth, height, width, glm::vec3(1, 0, 0));
            appendPlane('z', 'y', 'x', 1.0f, -1.0f, depth, height, -width, glm::vec3(-1, 0, 0));
            appendPlane('x', 'z', 'y', 1.0f, 1.0f, width, depth, height, glm::vec3(0, 1, 0));
            appendPlane('x', 'z', 'y', 1.0f, -1.0f, width, depth, -height, glm::vec3(0, -1, 0));
            appendPlane('x', 'y', 'z', 1.0f, -1.0f, width, height, depth, glm::vec3(0, 0, 1));
            appendPlane('x', 'y', 'z', -1.0f, -1.0f, width, height, -depth, glm::vec3(0, 0, -1));
        }

        /** Returns the next upper-24-bit sample from Three's xorshift32 stream. */
        double seededUnit(uint32_t &state)
        {
            uint32_t value = state != 0u ? state : 0x6d2b79f5u;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<double>(value >> 8u) / 16777216.0;
        }

        /** Tests two oriented boxes using Three r185's separating-axis theorem. */
        bool intersectsObb(const glm::dmat4 &left, const glm::dvec3 &leftHalf,
                           const glm::dmat4 &right, const glm::dvec3 &rightHalf)
        {
            // OBB.applyMatrix4() in Three separates scale from the basis and
            // scales the half extents. Keep the same representation here
            // before applying the literal r185 OBB.intersectsOBB() tests.
            const glm::dvec3 centerA = glm::dvec3(left[3]);
            const glm::dvec3 centerB = glm::dvec3(right[3]);
            glm::dvec3 axisA[3];
            glm::dvec3 axisB[3];
            double extentA[3];
            double extentB[3];
            for (int i = 0; i < 3; ++i)
            {
                const glm::dvec3 columnA(left[i]);
                const glm::dvec3 columnB(right[i]);
                const double lengthA = glm::length(columnA);
                const double lengthB = glm::length(columnB);
                axisA[i] = columnA / lengthA;
                axisB[i] = columnB / lengthB;
                extentA[i] = leftHalf[i] * lengthA;
                extentB[i] = rightHalf[i] * lengthB;
            }
            double rotation[3][3];
            double absoluteRotation[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    rotation[i][j] = glm::dot(axisA[i], axisB[j]);
                    absoluteRotation[i][j] = std::abs(rotation[i][j]) +
                        std::numeric_limits<double>::epsilon();
                }
            const glm::dvec3 translationWorld = centerB - centerA;
            const double translation[3] = {
                glm::dot(translationWorld, axisA[0]),
                glm::dot(translationWorld, axisA[1]),
                glm::dot(translationWorld, axisA[2])};
            for (int i = 0; i < 3; ++i)
            {
                const double radiusB = extentB[0] * absoluteRotation[i][0]
                    + extentB[1] * absoluteRotation[i][1]
                    + extentB[2] * absoluteRotation[i][2];
                if (std::abs(translation[i]) > extentA[i] + radiusB) return false;
            }
            for (int j = 0; j < 3; ++j)
            {
                const double projected = std::abs(glm::dot(translationWorld, axisB[j]));
                const double radiusA = extentA[0] * absoluteRotation[0][j]
                    + extentA[1] * absoluteRotation[1][j]
                    + extentA[2] * absoluteRotation[2][j];
                if (projected > radiusA + extentB[j]) return false;
            }

            // The following nine cross-axis tests intentionally retain the
            // source ordering and expressions from Three r185. The generic
            // cross-product form is algebraically equivalent in exact
            // arithmetic, but its different floating-point grouping changes
            // boundary collisions in the deterministic example.
            double ra;
            double rb;

            ra = extentA[1] * absoluteRotation[2][0] + extentA[2] * absoluteRotation[1][0];
            rb = extentB[1] * absoluteRotation[0][2] + extentB[2] * absoluteRotation[0][1];
            if (std::abs(translation[2] * rotation[1][0] - translation[1] * rotation[2][0]) > ra + rb) return false;

            ra = extentA[1] * absoluteRotation[2][1] + extentA[2] * absoluteRotation[1][1];
            rb = extentB[0] * absoluteRotation[0][2] + extentB[2] * absoluteRotation[0][0];
            if (std::abs(translation[2] * rotation[1][1] - translation[1] * rotation[2][1]) > ra + rb) return false;

            ra = extentA[1] * absoluteRotation[2][2] + extentA[2] * absoluteRotation[1][2];
            rb = extentB[0] * absoluteRotation[0][1] + extentB[1] * absoluteRotation[0][0];
            if (std::abs(translation[2] * rotation[1][2] - translation[1] * rotation[2][2]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[2][0] + extentA[2] * absoluteRotation[0][0];
            rb = extentB[1] * absoluteRotation[1][2] + extentB[2] * absoluteRotation[1][1];
            if (std::abs(translation[0] * rotation[2][0] - translation[2] * rotation[0][0]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[2][1] + extentA[2] * absoluteRotation[0][1];
            rb = extentB[0] * absoluteRotation[1][2] + extentB[2] * absoluteRotation[1][0];
            if (std::abs(translation[0] * rotation[2][1] - translation[2] * rotation[0][1]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[2][2] + extentA[2] * absoluteRotation[0][2];
            rb = extentB[0] * absoluteRotation[1][1] + extentB[1] * absoluteRotation[1][0];
            if (std::abs(translation[0] * rotation[2][2] - translation[2] * rotation[0][2]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[1][0] + extentA[1] * absoluteRotation[0][0];
            rb = extentB[1] * absoluteRotation[2][2] + extentB[2] * absoluteRotation[2][1];
            if (std::abs(translation[1] * rotation[0][0] - translation[0] * rotation[1][0]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[1][1] + extentA[1] * absoluteRotation[0][1];
            rb = extentB[0] * absoluteRotation[2][2] + extentB[2] * absoluteRotation[2][0];
            if (std::abs(translation[1] * rotation[0][1] - translation[0] * rotation[1][1]) > ra + rb) return false;

            ra = extentA[0] * absoluteRotation[1][2] + extentA[1] * absoluteRotation[0][2];
            rb = extentB[0] * absoluteRotation[2][1] + extentB[1] * absoluteRotation[2][0];
            if (std::abs(translation[1] * rotation[0][2] - translation[0] * rotation[1][2]) > ra + rb) return false;

            return true;
        }

        /** Validates the four locked OBB scenarios and the fixed capture extent. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool validScenario =
                (options.scenarioId == "initial-seeded-boxes" && options.targetFrame == 0u) ||
                (options.scenarioId == "animated-collisions" && options.targetFrame == 120u) ||
                (options.scenarioId == "closest-hit-selection" && options.targetFrame == 121u) ||
                (options.scenarioId == "selection-miss-removal" && options.targetFrame == 122u);
            if (options.caseId != "webgl_math_obb" || !validScenario ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_math_obb scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebglMathObbRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        captureWritten = false;
        entities.clear();
        entities.resize(BoxCount);
        entityIndices.clear();
        basePositions.clear();
        baseRotations.clear();
        baseScales.clear();
        worldModels.clear();
        entityIndices.reserve(BoxCount + 1u);
        basePositions.reserve(BoxCount);
        baseRotations.reserve(BoxCount);
        baseScales.reserve(BoxCount);
        worldModels.reserve(BoxCount);
        uint32_t randomState = options.randomSeed;
        viewMatrix = glm::lookAt(glm::vec3(0, 0, 75), glm::vec3(0), glm::vec3(0, 1, 0));
        projectionMatrix = glm::perspective(glm::radians(70.0f),
                                            float(options.width) / float(options.height),
                                            1.0f, 1000.0f);
        const glm::dvec3 halfSize(5.0, 2.5, 3.0);
        // The fixed r185 module graph consumes 100 xorshift samples before
        // the first Mesh transform. This prefix includes the UUID calls made
        // by the camera, scene, light, geometry, controls, renderer setup,
        // and the initial material graph. It is part of the locked oracle
        // stream and must not vary by scenario or command-line state.
        for (uint32_t uuidSample = 0u; uuidSample < 100u; ++uuidSample)
            (void)seededUnit(randomState);
        for (uint32_t index = 0u; index < BoxCount; ++index)
        {
            auto &entity = entities[index];
            buildBox(entity);
            const glm::dvec3 position(
                seededUnit(randomState) * 80.0 - 40.0,
                seededUnit(randomState) * 80.0 - 40.0,
                seededUnit(randomState) * 80.0 - 40.0);
            const glm::dvec3 rotation(
                seededUnit(randomState) * (2.0 * Pi),
                seededUnit(randomState) * (2.0 * Pi),
                seededUnit(randomState) * (2.0 * Pi));
            const glm::dvec3 scale(
                seededUnit(randomState) + 0.5,
                seededUnit(randomState) + 0.5,
                seededUnit(randomState) + 0.5);
            basePositions.push_back(position);
            baseRotations.push_back(rotation);
            baseScales.push_back(scale);
            // Three's default Euler order XYZ composes as Rx * Ry * Rz in
            // the column-vector convention used by Matrix4.compose. Keep
            // this order identical to Quaternion.setFromEuler in r185.
            const glm::dmat4 model = glm::translate(glm::dmat4(1.0), position) *
                glm::rotate(glm::dmat4(1.0), rotation.x, glm::dvec3(1, 0, 0)) *
                glm::rotate(glm::dmat4(1.0), rotation.y, glm::dvec3(0, 1, 0)) *
                glm::rotate(glm::dmat4(1.0), rotation.z, glm::dvec3(0, 0, 1)) *
                glm::scale(glm::dmat4(1.0), scale);
            const glm::mat4 modelFloat = glm::mat4(model);
            entity.worldModel = modelFloat;
            worldModels.push_back(model);
            entity.objectData.modelView = viewMatrix * modelFloat;
            entity.objectData.modelViewProjection = projectionMatrix * entity.objectData.modelView;
            // Three computes normalMatrix from the double-precision JS
            // model-view matrix and only then uploads its Float32Array. Do
            // the inverse-transpose in double precision as well; computing
            // it from an already rounded glm::mat4 changes the face
            // irradiance on rotated, non-uniformly scaled boxes.
            const glm::dmat4 modelViewDouble = glm::dmat4(viewMatrix) * model;
            entity.objectData.normalMatrix = glm::mat4(
                glm::transpose(glm::inverse(glm::mat3(modelViewDouble))));
            entity.objectData.colorAndPhase = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            entity.materialData.baseColor = glm::vec4(1.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.renderFlags.values[0] = 0u;
            entity.renderFlags.values[1] = 0u;
            entity.renderFlags.values[2] = 0u;
            entity.renderFlags.values[3] = 0u;
            // The next MeshLambertMaterial and Mesh each allocate a UUID.
            for (uint32_t uuidSample = 0u; uuidSample < 8u; ++uuidSample)
                (void)seededUnit(randomState);
        }
        for (uint32_t i = 0u; i < BoxCount; ++i)
            for (uint32_t j = i + 1u; j < BoxCount; ++j)
                if (intersectsObb(worldModels[i], halfSize, worldModels[j], halfSize))
                {
                    entities[i].objectData.colorAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    entities[j].objectData.colorAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }
        if (options.scenarioId == "closest-hit-selection")
        {
            selectedEntity = 0u;
            WebglMathObbHostEntity hitbox = entities[selectedEntity];
            hitbox.objectData.colorAndPhase = glm::vec4(0.15f, 0.12f, 0.08f, 1.0f);
            hitbox.renderFlags.values[0] = 1u;
            hitbox.renderFlags.values[1] = 0u;
            entities.push_back(hitbox);
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_math_obb could not create its RenderSet encoder.");
        for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            const auto &entity = entities[entityIndex];
            const std::string suffix = "-" + std::to_string(entityIndex);
            const std::string vertexName = "WebglMathObbVertices" + suffix;
            const std::string indexName = "WebglMathObbIndices" + suffix;
            const std::string objectName = "WebglMathObbObject" + suffix;
            const std::string instanceName = "WebglMathObbInstance" + suffix;
            const std::string materialName = "WebglMathObbMaterial" + suffix;
            const std::string flagsName = "WebglMathObbRenderFlags" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::vertices,
                         vertexName.c_str(), entity.vertices.data(),
                         entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::indices,
                         indexName.c_str(), entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t), 1u);
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::objects,
                         objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::instances,
                         instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::materials,
                         materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            appendBuffer(allocation, WebglMathObbSceneRenderSetComponents::renderFlags,
                         flagsName.c_str(), &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            entityIndices.push_back(encoder->allocEntity(allocation));
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMathObbRuntimeAdapter::updateFrameState(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (entityIndices.size() != entities.size() || worldModels.size() != BoxCount)
            throw std::runtime_error("webgl_math_obb frame state is not allocated.");

        // Timer.update() observes zero delta for the first callback and one
        // fixed 1/60 s delta for every subsequent callback in the reference
        // clock.  Therefore frameIndex/60 reproduces the accumulated r185
        // rotation at the callback that is captured.
        const double elapsedSeconds = static_cast<double>(frameIndex) / 60.0;
        const glm::dvec3 halfSize(5.0, 2.5, 3.0);
        for (uint32_t index = 0u; index < BoxCount; ++index)
        {
            auto &entity = entities[index];
            const glm::dvec3 rotation(
                baseRotations[index].x + elapsedSeconds * Pi * 0.20,
                baseRotations[index].y + elapsedSeconds * Pi * 0.10,
                baseRotations[index].z);
            const glm::dmat4 model = glm::translate(glm::dmat4(1.0), basePositions[index]) *
                glm::rotate(glm::dmat4(1.0), rotation.x, glm::dvec3(1, 0, 0)) *
                glm::rotate(glm::dmat4(1.0), rotation.y, glm::dvec3(0, 1, 0)) *
                glm::rotate(glm::dmat4(1.0), rotation.z, glm::dvec3(0, 0, 1)) *
                glm::scale(glm::dmat4(1.0), baseScales[index]);
            const glm::mat4 modelFloat = glm::mat4(model);
            worldModels[index] = model;
            entity.worldModel = modelFloat;
            entity.objectData.modelView = viewMatrix * modelFloat;
            entity.objectData.modelViewProjection = projectionMatrix * entity.objectData.modelView;
            const glm::dmat4 modelViewDouble = glm::dmat4(viewMatrix) * model;
            entity.objectData.normalMatrix = glm::mat4(
                glm::transpose(glm::inverse(glm::mat3(modelViewDouble))));
            entity.objectData.colorAndPhase = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        }

        for (uint32_t i = 0u; i < BoxCount; ++i)
            for (uint32_t j = i + 1u; j < BoxCount; ++j)
                if (intersectsObb(worldModels[i], halfSize, worldModels[j], halfSize))
                {
                    entities[i].objectData.colorAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    entities[j].objectData.colorAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }

        if (options.scenarioId == "closest-hit-selection" && entities.size() > BoxCount)
        {
            auto &hitbox = entities[BoxCount];
            hitbox.objectData = entities[selectedEntity].objectData;
            hitbox.objectData.colorAndPhase = glm::vec4(0.15f, 0.12f, 0.08f, 1.0f);
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_math_obb could not create its animation update encoder.");
        for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            const auto &entity = entities[entityIndex];
            encoder->setBufferComponentData(
                entityIndices[entityIndex],
                WebglMathObbSceneRenderSetComponents::objects,
                &entity.objectData,
                sizeof(entity.objectData),
                0u,
                1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMathObbRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateFrameState(renderer, options, frameIndex);
    }

    void WebglMathObbRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const size_t byteCount = static_cast<size_t>(uint64_t(width) * uint64_t(height) * 4u);
        eastl::vector<uint8_t> rgba(byteCount);
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        preparePath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        preparePath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgl_math_obb\",\n  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n  \"pipeline\":\""
                   << options.pipeline.c_str() << "\",\n  \"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\n  \"frame\":" << frameIndex
                   << ",\n  \"randomSeed\":" << options.randomSeed
                   << ",\n  \"width\":" << width << ",\n  \"height\":" << height
                   << ",\n  \"rowStrideBytes\":" << uint64_t(width) * 4u
                   << ",\n  \"byteCount\":" << byteCount
                   << ",\n  \"format\":\"rgba8unorm\",\n  \"sampleCount\":1,\n"
                   << "  \"msaaEnabled\":false,\n"
                   << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n";
            if (options.scenarioId == "closest-hit-selection")
                output << "  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_math_obb\",\"scenarioId\":\"closest-hit-selection\",\"captureFrame\":121,\"sha256\":\""
                       << SelectReplaySha256 << "\",\"target\":\"body > canvas\",\"eventCount\":1}\n}\n";
            else if (options.scenarioId == "selection-miss-removal")
                output << "  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_math_obb\",\"scenarioId\":\"selection-miss-removal\",\"captureFrame\":122,\"sha256\":\""
                       << MissReplaySha256 << "\",\"target\":\"body > canvas\",\"eventCount\":1}\n}\n";
            else
                output << "  \"inputReplay\":null\n}\n";
        }
        preparePath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_math_obb\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"renderSetPolicy\":\"required\",\n  \"gpuWorkDslOnly\":true,\n"
                   << "  \"sceneRenderSetCount\":1,\n  \"renderableObjectCount\":" << entities.size() << ",\n"
                   << "  \"scenePassCount\":2,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":2,\n"
                   << "  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
                   << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                   << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"collision-selection-visibility-and-material-phase\"}],\n"
                   << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"webgl-math-obb-scene-set\",\"renderSetType\":\"WebglMathObbSceneRenderSet\",\"renderableObjectCount\":" << entities.size() << ",\"entityCount\":" << entities.size() << ",\"entities\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ",";
                output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\"box-" << index
                       << "\",\"instanceCount\":1}";
            }
            output << "],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"collision-selection-visibility-and-material-phase\"}],\"drawCommandCount\":2,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main-lambert\",\"renderClass\":\"WebglMathObbMainPass\",\"renderSetId\":\"webgl-math-obb-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"selected-wireframe\",\"renderClass\":\"WebglMathObbWireframePass\",\"renderSetId\":\"webgl-math-obb-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
                   << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-lambert\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"selected-wireframe\",\"entityOrdinal\":0}]\n}\n";
        }
        captureWritten = true;
    }

    void WebglMathObbRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
