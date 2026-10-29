#include "WebglPostprocessingFxaaRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle BatchSceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebglPostprocessingFxaaHostVertex) == 80u);
        static_assert(sizeof(WebglPostprocessingFxaaHostObjectData) == 144u);
        static_assert(sizeof(WebglPostprocessingFxaaHostInstanceData) == 80u);
        static_assert(sizeof(WebglPostprocessingFxaaHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareFxaaOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Appends one typed host payload to a RenderSet allocation descriptor. */
        void appendFxaaBufferPayload(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Advances the exact xorshift32 stream installed by the reference capture bootstrap. */
        float nextFxaaReferenceRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Converts one Three.js XYZ Euler rotation into the exact compose quaternion. */
        glm::quat makeFxaaThreeXyzQuaternion(const glm::vec3 &rotation)
        {
            const float cosineX = std::cos(rotation.x * 0.5f);
            const float cosineY = std::cos(rotation.y * 0.5f);
            const float cosineZ = std::cos(rotation.z * 0.5f);
            const float sineX = std::sin(rotation.x * 0.5f);
            const float sineY = std::sin(rotation.y * 0.5f);
            const float sineZ = std::sin(rotation.z * 0.5f);
            return glm::quat(
                cosineX * cosineY * cosineZ -
                    sineX * sineY * sineZ,
                sineX * cosineY * cosineZ +
                    cosineX * sineY * sineZ,
                cosineX * sineY * cosineZ -
                    sineX * cosineY * sineZ,
                cosineX * cosineY * sineZ +
                    sineX * sineY * cosineZ);
        }

        /** Builds the r185 TetrahedronGeometry and one hundred seeded instance transforms for FXAA. */
        WebglPostprocessingFxaaEntityState buildFxaaEntity(uint32_t frameIndex)
        {
            WebglPostprocessingFxaaEntityState entity;
            entity.logicalId = "fxaa-tetrahedra";
            constexpr float Radius = 10.0f;
            constexpr float Coordinate = Radius * 0.5773502691896258f;
            const glm::vec3 sourcePositions[4u] = {
                {Coordinate, Coordinate, Coordinate},
                {-Coordinate, -Coordinate, Coordinate},
                {-Coordinate, Coordinate, -Coordinate},
                {Coordinate, -Coordinate, -Coordinate},
            };
            constexpr uint32_t sourceIndices[12u] = {
                2u, 1u, 0u, 0u, 3u, 2u, 1u, 3u, 0u, 2u, 3u, 1u,
            };
            entity.vertices.reserve(12u);
            entity.indices.reserve(12u);
            for (uint32_t triangle = 0u; triangle < 4u; ++triangle)
            {
                const glm::vec3 a = sourcePositions[sourceIndices[triangle * 3u]];
                const glm::vec3 b = sourcePositions[sourceIndices[triangle * 3u + 1u]];
                const glm::vec3 c = sourcePositions[sourceIndices[triangle * 3u + 2u]];
                const glm::vec3 normal = glm::normalize(glm::cross(c - b, a - b));
                const glm::vec3 trianglePositions[3u] = {a, b, c};
                for (const glm::vec3 &position : trianglePositions)
                {
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(1.0f),
                        glm::vec4(0.0f),
                        glm::vec4(position, 1.0f),
                    });
                    entity.indices.push_back(static_cast<uint32_t>(entity.indices.size()));
                }
            }

            const float autoRotation = -static_cast<float>(frameIndex + 1u) *
                                       3.14159265358979323846f / 900.0f;
            const glm::vec3 cameraPosition(
                std::sin(autoRotation) * 500.0f,
                0.0f,
                std::cos(autoRotation) * 500.0f);
            const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const float fieldOfView = 45.0f * 3.14159265358979323846f / 180.0f;
            const float nearDistance = 1.0f;
            const float farDistance = 2000.0f;
            const float top = nearDistance * std::tan(fieldOfView * 0.5f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = nearDistance / right;
            projection[1u][1u] = -nearDistance / top;
            projection[2u][2u] = -farDistance / (farDistance - nearDistance);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = -farDistance * nearDistance / (farDistance - nearDistance);
            entity.objectData.modelViewProjection = projection * view;
            entity.objectData.modelView = view;
            entity.objectData.parameters = glm::vec4(autoRotation, 0.0f, 0.0f, 10.0f);

            uint32_t randomState = 3466452579u;
            entity.instances.resize(100u);
            for (uint32_t instanceIndex = 0u; instanceIndex < 100u; ++instanceIndex)
            {
                const glm::vec3 translation(
                    nextFxaaReferenceRandom(randomState) * 500.0f - 250.0f,
                    nextFxaaReferenceRandom(randomState) * 500.0f - 250.0f,
                    nextFxaaReferenceRandom(randomState) * 500.0f - 250.0f);
                const float scaleValue = nextFxaaReferenceRandom(randomState) * 2.0f + 1.0f;
                const glm::vec3 rotation(
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f,
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f,
                    nextFxaaReferenceRandom(randomState) * 3.14159265358979323846f);
                const glm::quat orientation =
                    makeFxaaThreeXyzQuaternion(rotation);
                const glm::mat4 model =
                    glm::translate(glm::mat4(1.0f), translation) *
                    glm::toMat4(orientation) *
                    glm::scale(
                        glm::mat4(1.0f),
                        glm::vec3(scaleValue));
                WebglPostprocessingFxaaHostInstanceData &instance = entity.instances[instanceIndex];
                instance.transformColumn0 = model[0u];
                instance.transformColumn1 = model[1u];
                instance.transformColumn2 = model[2u];
                instance.transformColumn3 = model[3u];
                instance.color = glm::vec4(
                    orientation.x, orientation.y, orientation.z, orientation.w);
            }
            entity.materialData.baseColor = glm::vec4(
                0.93011086f, 0.03189603f, 0.03189603f, 1.0f);
            entity.materialData.emissiveAndOpacity = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            entity.materialData.modeAndParameters = glm::vec4(10.0f, 1.0f, 0.0f, 0.0f);
            return entity;
        }

        /** Writes one tightly packed RGBA8 capture file. */
        void writeFxaaRgba(const eastl::string &pathValue, const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path outputPath(pathValue.c_str());
            prepareFxaaOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write Phase 1 batch RGBA capture.");
        }
    } // namespace

    void WebglPostprocessingFxaaRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        device = inDevice;
        caseId = options.caseId;
        if (caseId != "webgl_postprocessing_fxaa")
        {
            throw std::invalid_argument("FXAA adapter received an unexpected case id.");
        }
        entities.reserve(1u);
        const auto encoder = renderer.createRenderSetCommandEncoder(BatchSceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not create the FXAA Scene RenderSet encoder.");
        entities.push_back(buildFxaaEntity(options.targetFrame));
        entities.back().entityIndex = allocateEntity(*encoder, entities.back());
        renderer.executeRenderSetCommand(BatchSceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglPostprocessingFxaaRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const WebglPostprocessingFxaaEntityState &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = static_cast<uint32_t>(entity.instances.size());
        const eastl::string prefix = "WebglPostprocessingFxaa" + entity.logicalId;
        appendFxaaBufferPayload(allocation, WebglPostprocessingFxaaSceneRenderSetComponents::vertices,
            prefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(WebglPostprocessingFxaaHostVertex), 1u);
        appendFxaaBufferPayload(allocation, WebglPostprocessingFxaaSceneRenderSetComponents::indices,
            prefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t), 1u);
        appendFxaaBufferPayload(allocation, WebglPostprocessingFxaaSceneRenderSetComponents::objects,
            prefix + "Object", &entity.objectData, sizeof(entity.objectData), 1u);
        appendFxaaBufferPayload(allocation, WebglPostprocessingFxaaSceneRenderSetComponents::instances,
            prefix + "Instances", entity.instances.data(), entity.instances.size() * sizeof(WebglPostprocessingFxaaHostInstanceData),
            static_cast<uint32_t>(entity.instances.size()));
        appendFxaaBufferPayload(allocation, WebglPostprocessingFxaaSceneRenderSetComponents::materials,
            prefix + "Material", &entity.materialData, sizeof(entity.materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglPostprocessingFxaaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingFxaaRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("Batch capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebglPostprocessingFxaaRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        writeFxaaRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(options.captureMetadataPath.c_str());
            prepareFxaaOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed << ",\n"
                   << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n  \"format\":\"rgba8unorm\"\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path path(options.sceneSnapshotPath.c_str());
            prepareFxaaOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\n  \"caseId\":\"" << caseId.c_str() << "\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"strict-pass\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":false,\n  \"assetHashes\":[],\n"
                   << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
                   << "  \"sceneRoots\":";
            output
                    << "[{\"id\":\"scene\",\"renderSetCount\":1,"
                    << "\"renderSetId\":\"webgl-postprocessing-fxaa-scene-set\","
                    << "\"renderSetType\":\"WebglPostprocessingFxaaSceneRenderSet\","
                    << "\"renderableObjectCount\":1,\"entityCount\":1,"
                    << "\"entities\":[{\"entityId\":" << entities[0].entityIndex
                    << ",\"logicalRenderableId\":\"fxaa-tetrahedra\","
                    << "\"instanceCount\":100}],"
                    << "\"componentSchema\":["
                    << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                    << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                    << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                    << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                    << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
                    << "\"drawCommandCount\":2,\"directDrawFallback\":false,"
                    << "\"scenePasses\":[{\"name\":\"main\","
                    << "\"renderClass\":\"WebglPostprocessingFxaaMainPass\","
                    << "\"renderSetId\":\"webgl-postprocessing-fxaa-scene-set\","
                    << "\"renderSetBindingCount\":1,"
                    << "\"drawMode\":\"render-set-indexed-indirect\","
                    << "\"invocationCount\":2,\"drawCommandCount\":2,"
                    << "\"usesStandaloneGeometry\":false,"
                    << "\"usesExplicitDrawCount\":false}]}],\n";
            output
                   << "  \"renderableObjectCount\":" << entities.size() << ",\n"
                   << "  \"entityCount\":" << entities.size() << ",\n"
                   << "  \"instanceCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].instances.size();
            }
            output << "],\n  \"vertexCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].vertices.size();
            }
            output << "],\n  \"indexCounts\":[";
            for (size_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            {
                if (entityIndex != 0u) output << ',';
                output << entities[entityIndex].indices.size();
            }
            output << "],\n"
                   << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
                   << "  \"scenePassCount\":2,\n"
                   << "  \"screenPassCount\":3,\n"
                   << "  \"scenePasses\":[\"WebglPostprocessingFxaaMainPass\"],\n"
                   << "  \"screenPasses\":[\"output-color-conversion\",\"fxaa\",\"side-by-side-composite\"],\n"
                   << "  \"attachmentFormats\":[\"rgba16float\",\"depth32float\",\"rgba8unorm\"],\n"
                   << "  \"computeDispatchThreads\":[],\n"
                   << "  \"scenePassInvocations\":[{\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"main\",\"invocationCount\":2}],\n"
                   << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"main\",\"entityOrdinal\":0},"
                   << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":1}],\n"
                   << "  \"drawCommandCount\":" << entities.size() * 2u << ",\n"
                   << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false\n}\n";
        }
    }

    void WebglPostprocessingFxaaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
