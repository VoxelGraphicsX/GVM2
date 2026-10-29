#include "WebglInteractiveCubesOrthoRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t CubeCount = 2000u;
        constexpr uint32_t InitialThreeUuidRandomSamples = 100u;
        constexpr uint32_t PerCubeThreeUuidRandomSamples = 8u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        /** Creates parent directories for an orthographic capture artifact. */
        void prepareOrthoCubesPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Converts one sRGB byte channel to linear light. */
        float orthoCubesSrgbByteToLinear(uint32_t value)
        {
            const float channel = float(value) / 255.0f;
            return channel <= 0.04045f
                ? channel / 12.92f
                : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }

        /** Advances the deterministic stream used to replace Math.random. */
        uint32_t nextOrthoCubeRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns a deterministic unit interval sample. */
        float orthoCubeRandom(uint32_t &state)
        {
            return float(nextOrthoCubeRandom(state) >> 8u) / 16777216.0f;
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendOrthoCubeBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Returns the nearest positive hit distance for a ray against one transformed unit cube. */
        bool intersectOrthoCubeRay(const glm::vec3 &origin,
                                   const glm::vec3 &direction,
                                   const glm::mat4 &model,
                                   float &worldDistance)
        {
            const glm::mat4 inverseModel = glm::inverse(model);
            const glm::vec3 localOrigin = glm::vec3(inverseModel * glm::vec4(origin, 1.0f));
            const glm::vec3 localDirection = glm::vec3(inverseModel * glm::vec4(direction, 0.0f));
            float nearDistance = -std::numeric_limits<float>::infinity();
            float farDistance = std::numeric_limits<float>::infinity();
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const float coordinate = localOrigin[axis];
                const float delta = localDirection[axis];
                if (std::abs(delta) < 1.0e-7f)
                {
                    if (coordinate < -0.5f || coordinate > 0.5f) return false;
                    continue;
                }
                const float reciprocal = 1.0f / delta;
                float slabNear = (-0.5f - coordinate) * reciprocal;
                float slabFar = (0.5f - coordinate) * reciprocal;
                if (slabNear > slabFar) std::swap(slabNear, slabFar);
                nearDistance = std::max(nearDistance, slabNear);
                farDistance = std::min(farDistance, slabFar);
                if (nearDistance > farDistance) return false;
            }
            const float localDistance = nearDistance >= 0.0f ? nearDistance : farDistance;
            if (localDistance < 0.0f) return false;
            const glm::vec3 localHit = localOrigin + localDirection * localDistance;
            const glm::vec3 worldHit = glm::vec3(model * glm::vec4(localHit, 1.0f));
            worldDistance = glm::length(worldHit - origin);
            return true;
        }

        /** Finds the nearest cube intersected by the canonical center-pointer replay. */
        uint32_t findOrthoCenterPointerCube(
            const eastl::vector<WebglInteractiveCubesOrthoEntityData> &entities,
            const glm::mat4 &view,
            const glm::mat4 &projection)
        {
            const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
            glm::vec4 nearPoint = inverseViewProjection * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
            glm::vec4 farPoint = inverseViewProjection * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;
            const glm::vec3 origin = glm::vec3(nearPoint);
            const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
            uint32_t selected = UINT32_MAX;
            float nearestDistance = std::numeric_limits<float>::infinity();
            for (uint32_t index = 0u; index < entities.size(); ++index)
            {
                float distance = 0.0f;
                if (intersectOrthoCubeRay(origin, direction, entities[index].model, distance) &&
                    distance < nearestDistance)
                {
                    nearestDistance = distance;
                    selected = index;
                }
            }
            return selected;
        }

        /** Builds a face-separated unit BoxGeometry. */
        void buildOrthoCubeGeometry(
            eastl::vector<WebglInteractiveCubesOrthoHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 positions[8u] = {
                {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
                {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
                {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
            };
            const uint32_t faces[6u][4u] = {
                {0u, 1u, 2u, 3u}, {4u, 7u, 6u, 5u},
                {0u, 3u, 7u, 4u}, {1u, 5u, 6u, 2u},
                {3u, 2u, 6u, 7u}, {0u, 4u, 5u, 1u},
            };
            const glm::vec3 normals[6u] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            };
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                {
                    vertices.push_back({
                        glm::vec4(positions[faces[face][corner]], 1.0f),
                        glm::vec4(normals[face], 0.0f)});
                }
                indices.insert(indices.end(), {
                    base + 0u, base + 1u, base + 2u,
                    base + 0u, base + 2u, base + 3u,
                });
            }
        }

        /** Returns the Three orthographic projection for the frozen extent. */
        glm::mat4 makeOrthoProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::ortho(-25.0f * aspect, 25.0f * aspect,
                              -25.0f, 25.0f, 0.1f, 100.0f);
        }

        /** Validates the initial, animated, and pointer-hit replay scenarios. */
        void validateOrthoCubesOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool hit = options.scenarioId == "cube-hit" && options.targetFrame == 61u;
            if (options.caseId != "webgl_interactive_cubes_ortho" ||
                (!initial && !animated && !hit) || options.width != 800u ||
                options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_interactive_cubes_ortho scenario does not match the locked r185 contract.");
            }
        }
    } // namespace

    void WebglInteractiveCubesOrthoRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOrthoCubesOptions(options);
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;
        entities.clear();
        entities.resize(CubeCount);
        uint32_t randomState = options.randomSeed;
        for (uint32_t sample = 0u; sample < InitialThreeUuidRandomSamples; ++sample)
        {
            (void)orthoCubeRandom(randomState);
        }
        for (uint32_t entityIndex = 0u; entityIndex < CubeCount; ++entityIndex)
        {
            WebglInteractiveCubesOrthoEntityData &entity = entities[entityIndex];
            buildOrthoCubeGeometry(entity.vertices, entity.indices);
            const uint32_t packedColor = static_cast<uint32_t>(orthoCubeRandom(randomState) * 0x00ffffffu);
            for (uint32_t sample = 0u; sample < PerCubeThreeUuidRandomSamples; ++sample)
            {
                (void)orthoCubeRandom(randomState);
            }
            const glm::vec3 position(
                orthoCubeRandom(randomState) * 40.0f - 20.0f,
                orthoCubeRandom(randomState) * 40.0f - 20.0f,
                orthoCubeRandom(randomState) * 40.0f - 20.0f);
            const glm::vec3 rotation(
                orthoCubeRandom(randomState) * float(2.0 * Pi),
                orthoCubeRandom(randomState) * float(2.0 * Pi),
                orthoCubeRandom(randomState) * float(2.0 * Pi));
            const glm::vec3 scale(
                orthoCubeRandom(randomState) + 0.5f,
                orthoCubeRandom(randomState) + 0.5f,
                orthoCubeRandom(randomState) + 0.5f);
            entity.model = glm::translate(glm::mat4(1.0f), position) *
                glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
                glm::scale(glm::mat4(1.0f), scale);
            entity.materialData.baseColor = glm::vec4(
                orthoCubesSrgbByteToLinear((packedColor >> 16u) & 0xffu),
                orthoCubesSrgbByteToLinear((packedColor >> 8u) & 0xffu),
                orthoCubesSrgbByteToLinear(packedColor & 0xffu), 1.0f);
            entity.materialData.emissive = glm::vec4(0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.objectData.lightDirectionAndIntensity = glm::vec4(
                glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)), 3.0f);
        }
        updateObjectData(options.width, options.height, options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_interactive_cubes_ortho could not create its Scene Set encoder.");
        }
        for (uint32_t entityIndex = 0u; entityIndex < CubeCount; ++entityIndex)
        {
            WebglInteractiveCubesOrthoEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix =
                eastl::string("InteractiveCubesOrtho-") + eastl::to_string(entityIndex);
            appendOrthoCubeBuffer(allocation,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::vertices,
                prefix + "-vertices", entity.vertices.data(),
                entity.vertices.size() * sizeof(WebglInteractiveCubesOrthoHostVertex), 1u);
            appendOrthoCubeBuffer(allocation,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::indices,
                prefix + "-indices", entity.indices.data(),
                entity.indices.size() * sizeof(uint32_t), 1u);
            appendOrthoCubeBuffer(allocation,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::objects,
                prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendOrthoCubeBuffer(allocation,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::instances,
                prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendOrthoCubeBuffer(allocation,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::materials,
                prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::updateObjectData(
        uint32_t width, uint32_t height, uint32_t frameIndex)
    {
        const double theta = (0.1 + 0.1 * double(frameIndex)) * Pi / 180.0;
        const glm::dvec3 cameraPosition(
            25.0 * std::sin(theta), 25.0 * std::sin(theta), 25.0 * std::cos(theta));
        const glm::mat4 view = glm::mat4(glm::lookAt(
            cameraPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 projection = makeOrthoProjection(width, height);
        currentViewMatrix = view;
        currentProjectionMatrix = projection;
        // Keep the directional light in the same view space as the cube
        // normals while the orthographic camera orbits the scene.
        const glm::vec3 viewLightDirection = glm::normalize(
            glm::mat3(view) * glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
        for (WebglInteractiveCubesOrthoEntityData &entity : entities)
        {
            entity.objectData.modelView = view * entity.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::mat4(glm::inverseTranspose(
                glm::mat3(entity.objectData.modelView)));
            entity.objectData.lightDirectionAndIntensity = glm::vec4(viewLightDirection, 3.0f);
        }
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        selectedCubeIndex = options.scenarioId == "cube-hit"
            ? findOrthoCenterPointerCube(entities, currentViewMatrix, currentProjectionMatrix)
            : UINT32_MAX;
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_interactive_cubes_ortho could not create its update encoder.");
        }
        for (const WebglInteractiveCubesOrthoEntityData &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        if (options.scenarioId == "cube-hit" && selectedCubeIndex < entities.size())
        {
            WebglInteractiveCubesOrthoHostMaterialData material = entities[selectedCubeIndex].materialData;
            material.emissive = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            encoder->setBufferComponentData(entities[selectedCubeIndex].entityIndex,
                WebglInteractiveCubesOrthoSceneRenderSetComponents::materials,
                &material, sizeof(material), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareOrthoCubesPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frameIndex,
        uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareOrthoCubesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
               << "\"caseId\":\"webgl_interactive_cubes_ortho\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\""
               << options.pipeline.c_str() << "\",\"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width
               << ",\"height\":" << height << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}"
               << ",\"singleSamplePolicy\":{\"sampleCount\":1,\"msaaEnabled\":false,\"simulateMsaa\":false}}\n";
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareOrthoCubesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_interactive_cubes_ortho\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n  \"implementationLevel\":\"strict-pass\",\n"
               << "  \"gpuWorkDslOnly\":true,\n  \"assetBacked\":false,\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":2000,\n  \"entityCount\":2000,\n"
               << "  \"instanceCount\":2000,\n  \"instanceCounts\":[1],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"assetAndAlgorithmState\":\"cpu-boxgeometry-xorshift-orthographic-lambert\",\n"
               << "  \"renderSetType\":\"WebglInteractiveCubesOrthoSceneRenderSet\",\n"
               << "  \"scenePasses\":[{\"name\":\"main-lambert-ortho\",\"renderClass\":\"WebglInteractiveCubesOrthoScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetType\":\"WebglInteractiveCubesOrthoSceneRenderSet\",\"renderableObjectCount\":2000,\"entityCount\":2000,\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[\"WebglInteractiveCubesOrthoScenePass\"],\"entities\":[";
        for (uint32_t index = 0u; index < CubeCount; ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\"cube-"
                   << index << "\",\"instanceCount\":1}";
        }
        output << "]}]}\n";
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options, uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("webgl_interactive_cubes_ortho RGBA capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_interactive_cubes_ortho has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglInteractiveCubesOrthoRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
