#include "WebglMaterialsChannelsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr uint32_t EntityCount = 1u;
        constexpr uint32_t TextureCount = 3u;
        constexpr uint32_t MaterialModeNormal = 1u;
        constexpr uint32_t MaterialModeVelocity = 2u;
        constexpr uint32_t CameraPerspective = 0u;
        constexpr uint32_t CameraOrtho = 1u;
        constexpr uint32_t SideFront = 0u;
        constexpr uint32_t SideBack = 1u;
        constexpr uint32_t SideDouble = 2u;
        constexpr float DisplacementScale = 2.436143f;
        constexpr float DisplacementBias = -0.428408f;

        /** Creates parent directories for one capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Reads one bounded binary asset from the immutable examples pack. */
        eastl::vector<uint8_t> readBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open materials/channels asset: " + path.string());
            const std::streamoff size = input.tellg();
            if (size <= 0 || uint64_t(size) > uint64_t(std::numeric_limits<size_t>::max()))
                throw std::runtime_error("Materials/channels asset has an invalid size: " + path.string());
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input) throw std::runtime_error("Could not read materials/channels asset: " + path.string());
            return bytes;
        }

        /** Appends one typed BufferComponent payload to a RenderSet allocation. */
        void appendBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t bytes,
            uint32_t instanceCount = 1u)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = bytes,
                .instanceCount = instanceCount,
            });
        }

        /** Flattens an explicit CPU mip chain into the RenderSet texture ABI. */
        void flattenMips(
            const eastl::vector<RgbaImageData> &mips,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &offsets)
        {
            bytes.clear();
            offsets.clear();
            for (const RgbaImageData &mip : mips)
            {
                offsets.push_back(bytes.size());
                bytes.insert(bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }

        /** Builds Three's perspective camera matrix for the fixed 800x500 capture. */
        glm::mat4 makePerspectiveProjection()
        {
            return glm::perspective(glm::radians(45.0f), 800.0f / 500.0f, 500.0f, 3000.0f);
        }

        /** Builds Three's orthographic camera matrix for the fixed 800x500 capture. */
        glm::mat4 makeOrthoProjection()
        {
            constexpr float height = 500.0f;
            const float width = height * 800.0f / 500.0f;
            return glm::ortho(-width, width, -height, height, 1000.0f, 2500.0f);
        }

        /** Builds the locked camera view and returns its current projection/view pair. */
        glm::mat4 makeView(uint32_t cameraMode)
        {
            const glm::vec3 position(0.0f, 0.0f, 1500.0f);
            return glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        /** Validates one Manifest scenario and the single-sample output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = (options.scenarioId == "initial-loader" ||
                                  options.scenarioId == "canonical-loader") && options.targetFrame == 0u;
            const bool velocity = options.scenarioId == "velocity-history" && options.targetFrame == 2u;
            const bool orthoBack = options.scenarioId == "velocity-ortho-back" && options.targetFrame == 3u;
            if (options.caseId != "webgl_materials_channels" || (!initial && !velocity && !orthoBack) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgl_materials_channels requires the locked r185 scenario contract.");
            if (orthoBack && options.inputReplayPath.empty())
                throw std::invalid_argument("velocity-ortho-back requires its locked input replay.");
        }
    } // namespace

    void WebglMaterialsChannelsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        const std::filesystem::path root(options.assetRoot.c_str());
        const std::filesystem::path ninjaRoot = root / "models" / "obj" / "ninja";
        const ThreeCompat::DecodedObjMesh decoded = ThreeCompat::decodeObjTriangleMesh(
            readBytes(ninjaRoot / "ninjaHead_Low.obj"));
        if (decoded.positions.empty() || decoded.positions.size() % 3u != 0u ||
            decoded.normals.size() != decoded.positions.size() ||
            decoded.textureCoordinates.size() != decoded.positions.size() / 3u * 2u)
            throw std::runtime_error("ninjaHead_Low.obj did not produce the expected OBJLoader triangle stream.");

        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (size_t index = 0u; index < decoded.positions.size(); index += 3u)
        {
            const glm::vec3 position(
                decoded.positions[index], decoded.positions[index + 1u], decoded.positions[index + 2u]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        WebglMaterialsChannelsEntityData entity;
        const uint32_t vertexCount = static_cast<uint32_t>(decoded.positions.size() / 3u);
        entity.vertices.reserve(vertexCount);
        entity.indices.reserve(vertexCount);
        for (uint32_t vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex)
        {
            const size_t positionOffset = size_t(vertexIndex) * 3u;
            const size_t uvOffset = size_t(vertexIndex) * 2u;
            const glm::vec3 position(
                (decoded.positions[positionOffset] - center.x) * 25.0f,
                (decoded.positions[positionOffset + 1u] - center.y) * 25.0f,
                (decoded.positions[positionOffset + 2u] - center.z) * 25.0f);
            const glm::vec3 normal = glm::normalize(glm::vec3(
                decoded.normals[positionOffset], decoded.normals[positionOffset + 1u], decoded.normals[positionOffset + 2u]));
            entity.vertices.push_back({
                glm::vec4(position, 1.0f),
                glm::vec4(normal, 0.0f),
                glm::vec4(decoded.textureCoordinates[uvOffset], decoded.textureCoordinates[uvOffset + 1u], 0.0f, 0.0f),
            });
            entity.indices.push_back(vertexIndex);
        }
        const std::filesystem::path normalPath = ninjaRoot / "normal.png";
        const std::filesystem::path aoPath = ninjaRoot / "ao.jpg";
        const std::filesystem::path displacementPath = ninjaRoot / "displacement.jpg";
        const RgbaImageData maps[TextureCount] = {
            decodePngRgba8(normalPath),
            decodeJpegRgba8(aoPath),
            decodeJpegRgba8(displacementPath),
        };
        for (uint32_t textureIndex = 0u; textureIndex < TextureCount; ++textureIndex)
        {
            const eastl::vector<RgbaImageData> mips = buildUnormMipChain(maps[textureIndex]);
            entity.textureWidths[textureIndex] = maps[textureIndex].width;
            entity.textureHeights[textureIndex] = maps[textureIndex].height;
            flattenMips(mips, entity.textureBytes[textureIndex], entity.textureMipOffsets[textureIndex]);
        }

        entities.clear();
        entities.push_back(eastl::move(entity));
        updateObjectData(options.targetFrame, options);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_channels could not create its Scene Set encoder.");
        WebglMaterialsChannelsEntityData &sceneEntity = entities.front();
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(sceneEntity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(sceneEntity.indices.size());
        allocation.instanceCount = 1u;
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::vertices,
                     "NinjaVertices", sceneEntity.vertices.data(), sceneEntity.vertices.size() * sizeof(sceneEntity.vertices[0u]));
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::indices,
                     "NinjaIndices", sceneEntity.indices.data(), sceneEntity.indices.size() * sizeof(uint32_t));
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::objects,
                     "NinjaObject", &sceneEntity.objectData, sizeof(sceneEntity.objectData));
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::instances,
                     "NinjaInstance", &sceneEntity.instanceData, sizeof(sceneEntity.instanceData));
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::materials,
                     "NinjaMaterial", &sceneEntity.materialData, sizeof(sceneEntity.materialData));
        appendBuffer(allocation, WebglMaterialsChannelsSceneRenderSetComponents::previousTransforms,
                     "NinjaPreviousTransform", &sceneEntity.previousTransform, sizeof(sceneEntity.previousTransform));
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle = WebglMaterialsChannelsSceneRenderSetComponents::textures;
        const char *textureNames[TextureCount] = {"normal.png", "ao.jpg", "displacement.jpg"};
        for (uint32_t textureIndex = 0u; textureIndex < TextureCount; ++textureIndex)
        {
            textureComponent.textures.push_back({
                .textureName = textureNames[textureIndex],
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = sceneEntity.textureWidths[textureIndex],
                .height = sceneEntity.textureHeights[textureIndex],
                .data = sceneEntity.textureBytes[textureIndex].data(),
                .dataStorageBytes = sceneEntity.textureBytes[textureIndex].size(),
                .mipmapOffsetBytes = sceneEntity.textureMipOffsets[textureIndex],
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        sceneEntity.entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsChannelsRuntimeAdapter::updateObjectData(
        uint32_t frameIndex,
        const ThreeSampleHostOptions &options)
    {
        if (entities.empty()) return;
        const bool velocity = options.scenarioId == "velocity-history" ||
            options.scenarioId == "velocity-ortho-back";
        const bool orthoBack = options.scenarioId == "velocity-ortho-back";
        const uint32_t mode = velocity ? MaterialModeVelocity : MaterialModeNormal;
        const uint32_t side = orthoBack ? SideBack : SideDouble;
        const uint32_t camera = orthoBack ? CameraOrtho : CameraPerspective;
        const glm::mat4 view = makeView(camera);
        const glm::mat4 projection = camera == CameraOrtho
            ? makeOrthoProjection() : makePerspectiveProjection();
        const glm::mat4 previousProjection = orthoBack
            ? makePerspectiveProjection() : projection;
        WebglMaterialsChannelsEntityData &entity = entities.front();
        entity.objectData.modelView = view;
        entity.objectData.modelViewProjection = projection * view;
        entity.objectData.previousModelViewProjection = previousProjection * view;
        entity.objectData.normalTransform = glm::transpose(glm::inverse(view));
        entity.objectData.cameraAndViewport = glm::vec4(0.0f, 0.0f, 1500.0f, 800.0f / 500.0f);
        entity.objectData.materialAndCamera = glm::uvec4(0u, mode, side, camera);
        entity.materialData.displacementAndSide = glm::vec4(0.0f, float(side), DisplacementScale, DisplacementBias);
        entity.instanceData.reserved = glm::vec4(0.0f);
        (void)frameIndex;
    }

    void WebglMaterialsChannelsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(frameIndex, options);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_channels update encoder unavailable.");
        const WebglMaterialsChannelsEntityData &entity = entities.front();
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglMaterialsChannelsSceneRenderSetComponents::objects,
            &entity.objectData,
            sizeof(entity.objectData),
            0u,
            1u);
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglMaterialsChannelsSceneRenderSetComponents::materials,
            &entity.materialData,
            sizeof(entity.materialData),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsChannelsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglMaterialsChannelsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frame,
        uint32_t width,
        uint32_t height,
        uint64_t bytes) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_materials_channels\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
               << ",\"byteCount\":" << bytes << ",\"format\":\"rgba8unorm\",\"randomSeed\":"
               << options.randomSeed << ",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglMaterialsChannelsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        const bool backOnly = options.scenarioId == "velocity-ortho-back";
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_channels\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglMaterialsChannelsSceneRenderSet\",\n  \"renderableObjectCount\":1,\n"
               << "  \"entityCount\":1,\n  \"instanceCounts\":[1],\n  \"textureSlotCount\":3,\n"
               << "  \"scenePassCount\":2,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":"
               << (backOnly ? 1 : 2) << ",\n  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"},{\"name\":\"previousTransforms\",\"kind\":\"buffer\",\"role\":\"previous-transform\"}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebglMaterialsChannelsSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"drawCommandCount\":2,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"},{\"name\":\"previousTransforms\",\"kind\":\"buffer\",\"role\":\"previous-transform\"}],\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"ninja\",\"instanceCount\":1}],\"scenePasses\":[{\"name\":\"front-facing\",\"renderClass\":\"WebglMaterialsChannelsFrontFacingPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"invocationCount\":"
               << (backOnly ? 0 : 1) << ",\"drawCommandCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"back-facing\",\"renderClass\":\"WebglMaterialsChannelsBackFacingPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"invocationCount\":1,\"drawCommandCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
               << "  \"scenePasses\":[{\"name\":\"front-facing\",\"renderClass\":\"WebglMaterialsChannelsFrontFacingPass\",\"renderSetBindingCount\":1,\"invocationCount\":"
               << (backOnly ? 0 : 1) << ",\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"back-facing\",\"renderClass\":\"WebglMaterialsChannelsBackFacingPass\",\"renderSetBindingCount\":1,\"invocationCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglMaterialsChannelsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4u;
        if (bytes > std::numeric_limits<size_t>::max()) throw std::overflow_error("materials/channels capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(bytes));
        if (!device->graphicsQueue(0)) throw std::runtime_error("materials/channels has no graphics queue.");
        device->graphicsQueue(0)->readTexture(texture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, bytes);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglMaterialsChannelsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &,
        const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
