#include "WebglGeometriesRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>
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
        constexpr uint32_t EntityCount = 16u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double EpochMilliseconds = 1700000000000.0;

        static_assert(sizeof(WebglGeometriesHostVertex) == 48u);
        static_assert(sizeof(WebglGeometriesHostObjectData) == 144u);
        static_assert(sizeof(WebglGeometriesHostInstanceData) == 16u);
        static_assert(sizeof(WebglGeometriesHostMaterialData) == 32u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareGeometriesOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Writes one optional UTF-8 evidence artifact to its explicit path. */
        void writeGeometriesTextArtifact(
            const eastl::string &pathValue,
            const eastl::string &payload)
        {
            if (pathValue.empty())
            {
                return;
            }
            const std::filesystem::path outputPath(
                pathValue.c_str());
            prepareGeometriesOutputPath(
                outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            output << payload.c_str();
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write a webgl_geometries evidence artifact.");
            }
        }

        /** Validates one of the two frozen webgl_geometries scenarios. */
        void validateGeometriesScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial-grid" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-grid" &&
                options.targetFrame == 60u;
            if (options.caseId != "webgl_geometries" ||
                (!initial && !animated) ||
                !options.inputReplayPath.empty() ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_geometries requires its locked case, scenarios, extent, seed, and asset root.");
            }
        }

        /** Converts one double-precision Three matrix to the Float32 component ABI. */
        glm::mat4 convertGeometriesMatrix(
            const glm::dmat4 &value)
        {
            glm::mat4 result(1.0f);
            for (uint32_t column = 0u;
                 column < 4u;
                 ++column)
            {
                result[column] =
                    glm::vec4(value[column]);
            }
            return result;
        }

        /** Builds Three's exact XYZ Euler quaternion without losing the large virtual-clock angle. */
        glm::dquat makeGeometriesRotation(
            double rotationX,
            double rotationY)
        {
            const double cosineX =
                std::cos(rotationX * 0.5);
            const double sineX =
                std::sin(rotationX * 0.5);
            const double cosineY =
                std::cos(rotationY * 0.5);
            const double sineY =
                std::sin(rotationY * 0.5);
            return glm::dquat(
                cosineX * cosineY,
                sineX * cosineY,
                cosineX * sineY,
                sineX * sineY);
        }

        /** Builds the OpenGL projection matrix consumed before DSL depth conversion. */
        glm::mat4 makeGeometriesProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2000.0;
            const double top =
                NearDistance *
                std::tan(45.0 * Pi / 360.0);
            const double height = 2.0 * top;
            const double width =
                1.6 * height;
            glm::dmat4 projection(0.0);
            projection[0u][0u] =
                2.0 * NearDistance / width;
            projection[1u][1u] =
                2.0 * NearDistance / height;
            projection[2u][2u] =
                -(FarDistance + NearDistance) /
                (FarDistance - NearDistance);
            projection[2u][3u] = -1.0;
            projection[3u][2u] =
                -2.0 * FarDistance * NearDistance /
                (FarDistance - NearDistance);
            return convertGeometriesMatrix(
                projection);
        }

        /** Packs the explicit sRGB mip chain used by every entity texture component. */
        void buildGeometriesTextureUpload(
            const std::filesystem::path &assetPath,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &mipOffsets,
            uint32_t &width,
            uint32_t &height)
        {
            const RgbaImageData base =
                decodeJpegRgba8(assetPath);
            width = base.width;
            height = base.height;
            const eastl::vector<RgbaImageData> mips =
                buildSrgbMipChain(base);
            bytes.clear();
            mipOffsets.clear();
            for (const RgbaImageData &mip : mips)
            {
                mipOffsets.push_back(bytes.size());
                bytes.insert(
                    bytes.end(),
                    mip.pixels.begin(),
                    mip.pixels.end());
            }
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendGeometriesBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Allocates one complete geometry entity through existing RenderSet semantics. */
        void allocateGeometriesEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglGeometriesEntityData &entity,
            uint32_t textureWidth,
            uint32_t textureHeight,
            const eastl::vector<uint8_t> &textureBytes,
            const eastl::vector<uint64_t> &textureMipOffsets)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(
                    entity.vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(
                    entity.indices.size());
            allocation.instanceCount = 1u;
            appendGeometriesBufferPayload(
                allocation,
                WebglGeometriesSceneRenderSetComponents::vertices,
                entity.logicalId + "-vertices",
                entity.vertices.data(),
                entity.vertices.size() *
                    sizeof(WebglGeometriesHostVertex));
            appendGeometriesBufferPayload(
                allocation,
                WebglGeometriesSceneRenderSetComponents::indices,
                entity.logicalId + "-indices",
                entity.indices.data(),
                entity.indices.size() *
                    sizeof(uint32_t));
            appendGeometriesBufferPayload(
                allocation,
                WebglGeometriesSceneRenderSetComponents::objects,
                entity.logicalId + "-object",
                &entity.objectData,
                sizeof(entity.objectData));
            appendGeometriesBufferPayload(
                allocation,
                WebglGeometriesSceneRenderSetComponents::instances,
                entity.logicalId + "-instance",
                &entity.instanceData,
                sizeof(entity.instanceData));
            appendGeometriesBufferPayload(
                allocation,
                WebglGeometriesSceneRenderSetComponents::materials,
                entity.logicalId + "-material",
                &entity.materialData,
                sizeof(entity.materialData));
            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle =
                WebglGeometriesSceneRenderSetComponents::textures;
            textureInfo.textures.push_back({
                .textureName =
                    entity.logicalId + "-uv-grid",
                .format =
                    GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = textureWidth,
                .height = textureHeight,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = textureMipOffsets,
            });
            allocation.textureInfos.push_back(
                eastl::move(textureInfo));
            encoder.allocEntity(allocation);
        }
    } // namespace

    void WebglGeometriesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateGeometriesScenario(options);
        device = inDevice;
        const auto meshes =
            decodeWebglGeometriesBundle(
                std::filesystem::path(
                    GVM_THREE_SAMPLE_SOURCE_ROOT) /
                "Fixtures" /
                "WebglGeometries" /
                "Assets" /
                "webgl_geometries_meshes.bin");
        if (meshes.size() != EntityCount)
        {
            throw std::logic_error(
                "webgl_geometries must decode exactly sixteen meshes.");
        }

        const double milliseconds =
            EpochMilliseconds +
            double(options.targetFrame) *
                (1000.0 / 60.0);
        const double timer =
            milliseconds * 0.0001;
        const glm::dvec3 cameraPosition(
            std::cos(timer) * 800.0,
            500.0,
            std::sin(timer) * 800.0);
        const glm::dmat4 view =
            glm::lookAt(
                cameraPosition,
                glm::dvec3(0.0),
                glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 rotation =
            glm::toMat4(
                makeGeometriesRotation(
                    timer * 5.0,
                    timer * 2.5));
        const glm::mat4 projection =
            makeGeometriesProjection();
        const glm::dvec3 translations[EntityCount] = {
            {-300.0, 0.0, 300.0},
            {-100.0, 0.0, 300.0},
            {100.0, 0.0, 300.0},
            {300.0, 0.0, 300.0},
            {-300.0, 0.0, 100.0},
            {-100.0, 0.0, 100.0},
            {100.0, 0.0, 100.0},
            {300.0, 0.0, 100.0},
            {-300.0, 0.0, -100.0},
            {-100.0, 0.0, -100.0},
            {100.0, 0.0, -100.0},
            {300.0, 0.0, -100.0},
            {-300.0, 0.0, -300.0},
            {-100.0, 0.0, -300.0},
            {100.0, 0.0, -300.0},
            {300.0, 0.0, -300.0},
        };
        const double scales[EntityCount] = {
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 5.0, 30.0,
        };

        entities.clear();
        entities.reserve(EntityCount);
        for (uint32_t index = 0u;
             index < EntityCount;
             ++index)
        {
            const WebglGeometriesBundleMesh &mesh =
                meshes[index];
            WebglGeometriesEntityData entity;
            entity.logicalId = mesh.name;
            entity.vertices.reserve(
                mesh.positions.size());
            for (size_t vertexIndex = 0u;
                 vertexIndex < mesh.positions.size();
                 ++vertexIndex)
            {
                entity.vertices.push_back({
                    glm::vec4(
                        mesh.positions[vertexIndex],
                        1.0f),
                    glm::vec4(
                        mesh.normals[vertexIndex],
                        0.0f),
                    glm::vec4(
                        mesh.textureCoordinates[vertexIndex],
                        0.0f,
                        0.0f),
                });
            }
            entity.indices = mesh.indices;
            const glm::dmat4 model =
                glm::translate(
                    glm::dmat4(1.0),
                    translations[index]) *
                rotation *
                glm::scale(
                    glm::dmat4(1.0),
                    glm::dvec3(scales[index]));
            entity.objectData.modelView =
                convertGeometriesMatrix(
                    view * model);
            entity.objectData.projection =
                projection;
            entity.objectData.ambientPointIntensity =
                glm::vec4(
                    0.9057419899f,
                    2.5f,
                    0.0f,
                    0.0f);
            entity.instanceData.reserved =
                glm::vec4(0.0f);
            entity.materialData.diffuseAndShininess =
                glm::vec4(1.0f, 1.0f, 1.0f, 30.0f);
            const float specularLinear =
                std::pow(
                    (17.0f / 255.0f + 0.055f) /
                        1.055f,
                    2.4f);
            entity.materialData.specular =
                glm::vec4(
                    specularLinear,
                    specularLinear,
                    specularLinear,
                    0.0f);
            entities.push_back(
                eastl::move(entity));
        }

        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        buildGeometriesTextureUpload(
            std::filesystem::path(
                options.assetRoot.c_str()) /
                "textures" /
                "uv_grid_opengl.jpg",
            textureBytes,
            textureMipOffsets,
            textureWidth,
            textureHeight);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_geometries could not create its Scene Set encoder.");
        }
        for (const auto &entity : entities)
        {
            allocateGeometriesEntity(
                *encoder,
                entity,
                textureWidth,
                textureHeight,
                textureBytes,
                textureMipOffsets);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglGeometriesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometriesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) *
            uint64_t(height) *
            4u;
        if (byteCount >
            std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_geometries RGBA capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareGeometriesOutputPath(
                outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary |
                    std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(
                    rgba.data()),
                static_cast<std::streamsize>(
                    rgba.size()));
        }
        writeGeometriesTextArtifact(
            options.captureMetadataPath,
            eastl::string("{\"schemaVersion\":1,\"caseId\":\"webgl_geometries\",\"scenarioId\":\"") +
                options.scenarioId +
                "\",\"pipeline\":\"" +
                options.pipeline +
                "\",\"backend\":\"" +
                threeSampleBackendName(options.backend) +
                "\",\"frame\":" +
                eastl::to_string(frameIndex) +
                ",\"randomSeed\":" +
                eastl::to_string(options.randomSeed) +
                ",\"width\":" +
                eastl::to_string(width) +
                ",\"height\":" +
                eastl::to_string(height) +
                ",\"rowStrideBytes\":" +
                eastl::to_string(uint64_t(width) * 4u) +
                ",\"byteCount\":" +
                eastl::to_string(byteCount) +
                ",\"format\":\"rgba8unorm\",\"inputReplay\":null}\n");
        uint64_t vertexCount = 0u;
        uint64_t indexCount = 0u;
        for (const auto &entity : entities)
        {
            vertexCount += entity.vertices.size();
            indexCount += entity.indices.size();
        }
        writeGeometriesTextArtifact(
            options.sceneSnapshotPath,
            eastl::string("{\"schemaVersion\":1,\"caseId\":\"webgl_geometries\",\"scenarioId\":\"") +
                options.scenarioId +
                "\",\"frame\":" +
                eastl::to_string(frameIndex) +
                ",\"implementationLevel\":\"strict-pass\",\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1,\"renderableObjectCount\":16,\"entityCount\":16,\"instanceCount\":16,\"vertexCount\":" +
                eastl::to_string(vertexCount) +
                ",\"indexCount\":" +
                eastl::to_string(indexCount) +
                ",\"scenePassCount\":2,\"screenPassCount\":0,\"drawCommandCount\":2,\"renderSetType\":\"WebglGeometriesSceneRenderSet\",\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglGeometriesSceneRenderSet\",\"renderableObjectCount\":16,\"entityCount\":16,\"entities\":["
                "{\"entityId\":0,\"logicalRenderableId\":\"geometry-0\",\"instanceCount\":1},"
                "{\"entityId\":1,\"logicalRenderableId\":\"geometry-1\",\"instanceCount\":1},"
                "{\"entityId\":2,\"logicalRenderableId\":\"geometry-2\",\"instanceCount\":1},"
                "{\"entityId\":3,\"logicalRenderableId\":\"geometry-3\",\"instanceCount\":1},"
                "{\"entityId\":4,\"logicalRenderableId\":\"geometry-4\",\"instanceCount\":1},"
                "{\"entityId\":5,\"logicalRenderableId\":\"geometry-5\",\"instanceCount\":1},"
                "{\"entityId\":6,\"logicalRenderableId\":\"geometry-6\",\"instanceCount\":1},"
                "{\"entityId\":7,\"logicalRenderableId\":\"geometry-7\",\"instanceCount\":1},"
                "{\"entityId\":8,\"logicalRenderableId\":\"geometry-8\",\"instanceCount\":1},"
                "{\"entityId\":9,\"logicalRenderableId\":\"geometry-9\",\"instanceCount\":1},"
                "{\"entityId\":10,\"logicalRenderableId\":\"geometry-10\",\"instanceCount\":1},"
                "{\"entityId\":11,\"logicalRenderableId\":\"geometry-11\",\"instanceCount\":1},"
                "{\"entityId\":12,\"logicalRenderableId\":\"geometry-12\",\"instanceCount\":1},"
                "{\"entityId\":13,\"logicalRenderableId\":\"geometry-13\",\"instanceCount\":1},"
                "{\"entityId\":14,\"logicalRenderableId\":\"geometry-14\",\"instanceCount\":1},"
                "{\"entityId\":15,\"logicalRenderableId\":\"geometry-15\",\"instanceCount\":1}],"
                "\"componentSchema\":["
                "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"shared-uv-grid-fixed-slot\"}],"
                "\"drawCommandCount\":2,\"directDrawFallback\":false,\"scenePasses\":["
                "{\"name\":\"main-double-sided-back\",\"renderClass\":\"WebglGeometriesBackPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                "{\"name\":\"main-double-sided-front\",\"renderClass\":\"WebglGeometriesFrontPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
                "\"scenePassSequence\":["
                "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-double-sided-back\"},"
                "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-double-sided-front\"}]}\n");
        writeGeometriesTextArtifact(
            options.semanticSnapshotPath,
            "{\"schemaVersion\":1,\"caseId\":\"webgl_geometries\",\"geometryCount\":16,\"texture\":\"uv_grid_opengl.jpg\",\"singleSample\":true}\n");
        captureWritten = true;
    }

    void WebglGeometriesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
    }
} // namespace GVM::ThreeSamples
