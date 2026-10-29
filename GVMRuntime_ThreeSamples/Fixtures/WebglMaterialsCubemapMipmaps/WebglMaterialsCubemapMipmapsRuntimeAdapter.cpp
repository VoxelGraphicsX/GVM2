#include "WebglMaterialsCubemapMipmapsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 2u;
        constexpr uint32_t CubeFaceCount = 6u;
        constexpr uint32_t ManualMipCount = 9u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;
        constexpr const char *CubeFaceNames[CubeFaceCount] = {
            "c00", "c01", "c02", "c03", "c04", "c05"};

        /** Creates parent directories for one requested capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Reads one bounded immutable file from the locked r185 asset pack. */
        eastl::vector<uint8_t> readBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open cubemap mipmap asset: " + path.string());
            const std::streamoff size = input.tellg();
            if (size <= 0 || uint64_t(size) > uint64_t(std::numeric_limits<size_t>::max()))
                throw std::runtime_error("Cubemap mipmap asset has an invalid size: " + path.string());
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
                throw std::runtime_error("Could not read cubemap mipmap asset: " + path.string());
            return bytes;
        }

        /** Resolves either the examples root or its parent asset-pack root. */
        std::filesystem::path resolveExamplesRoot(const std::filesystem::path &assetRoot)
        {
            const std::filesystem::path direct = assetRoot / "textures" / "cube" / "angus";
            if (std::filesystem::exists(direct)) return assetRoot;
            const std::filesystem::path nested = assetRoot / "examples";
            if (std::filesystem::exists(nested / "textures" / "cube" / "angus")) return nested;
            throw std::runtime_error("The asset pack does not contain textures/cube/angus.");
        }

        /** Appends one typed RenderSet buffer payload. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
                          const void *value,
                          uint64_t bytes)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = bytes,
                .instanceCount = 1u,
            });
        }

        /** Builds the indexed SphereGeometry(100,128,128) stream used upstream. */
        void buildSphere(WebglMaterialsCubemapMipmapsEntityData &entity)
        {
            constexpr uint32_t WidthSegments = 128u;
            constexpr uint32_t HeightSegments = 128u;
            constexpr float Radius = 100.0f;
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            entity.indices.reserve(WidthSegments * HeightSegments * 6u);
            for (uint32_t iy = 0u; iy <= HeightSegments; ++iy)
            {
                const float v = float(iy) / float(HeightSegments);
                const float theta = v * Pi;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                for (uint32_t ix = 0u; ix <= WidthSegments; ++ix)
                {
                    const float u = float(ix) / float(WidthSegments);
                    const float phi = u * Pi * 2.0f;
                    const glm::vec3 normal(
                        sinTheta * std::cos(phi), cosTheta,
                        sinTheta * std::sin(phi));
                    entity.vertices.push_back({glm::vec4(normal * Radius, 1.0f),
                                                glm::vec4(normal, 0.0f)});
                }
            }
            for (uint32_t iy = 0u; iy < HeightSegments; ++iy)
            {
                for (uint32_t ix = 0u; ix < WidthSegments; ++ix)
                {
                    const uint32_t a = iy * (WidthSegments + 1u) + ix + 1u;
                    const uint32_t b = iy * (WidthSegments + 1u) + ix;
                    const uint32_t c = (iy + 1u) * (WidthSegments + 1u) + ix;
                    const uint32_t d = (iy + 1u) * (WidthSegments + 1u) + ix + 1u;
                    if (iy != 0u) entity.indices.insert(entity.indices.end(), {a, b, d});
                    if (iy != HeightSegments - 1u) entity.indices.insert(entity.indices.end(), {b, c, d});
                }
            }
        }

        /** Packs generated and manual Angus mip levels side by side in one face texture. */
        void loadPackedFace(const std::filesystem::path &root,
                            uint32_t face,
                            eastl::vector<uint8_t> &bytes,
                            eastl::vector<uint64_t> &mipOffsets,
                            uint32_t &width,
                            uint32_t &height)
        {
            bytes.clear();
            mipOffsets.clear();
            const std::string baseFile = "cube_m00_" + std::string(CubeFaceNames[face]) + ".jpg";
            const RgbaImageData base = decodeJpegRgba8(root / baseFile);
            const eastl::vector<RgbaImageData> generatedMips = buildSrgbMipChain(base);
            if (generatedMips.size() != ManualMipCount)
                throw std::runtime_error("Angus generated mip chain does not contain nine levels.");
            for (uint32_t level = 0u; level < ManualMipCount; ++level)
            {
                const std::string file = "cube_m0" + std::to_string(level) + "_" +
                    CubeFaceNames[face] + ".jpg";
                const RgbaImageData manual = decodeJpegRgba8(root / file);
                if (level == 0u)
                {
                    width = base.width * 2u;
                    height = base.height;
                }
                mipOffsets.push_back(bytes.size());
                const RgbaImageData &generated = generatedMips[level];
                if (generated.width != manual.width || generated.height != manual.height)
                    throw std::runtime_error("Angus manual/generated mip dimensions diverged.");
                const uint32_t rowBytes = manual.width * 4u;
                for (uint32_t row = 0u; row < manual.height; ++row)
                {
                    const size_t generatedOffset = size_t(row) * rowBytes;
                    const size_t manualOffset = size_t(row) * rowBytes;
                    bytes.insert(bytes.end(), generated.pixels.begin() + generatedOffset,
                                 generated.pixels.begin() + generatedOffset + rowBytes);
                    bytes.insert(bytes.end(), manual.pixels.begin() + manualOffset,
                                 manual.pixels.begin() + manualOffset + rowBytes);
                }
            }
        }

        /** Updates projection, view, and camera basis for one deterministic frame. */
        void updateMatrices(WebglMaterialsCubemapMipmapsEntityData &entity,
                            uint32_t entityIndex,
                            uint32_t frameIndex,
                            uint32_t width,
                            uint32_t height)
        {
            const float orbit = frameIndex == 121u ? 0.055f : 0.0f;
            const glm::vec3 cameraPosition(
                500.0f * std::sin(orbit),
                0.0f,
                500.0f * std::cos(orbit));
            const glm::mat4 view = glm::lookAt(
                cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = glm::perspective(
                glm::radians(50.0f), float(width) / float(height), 1.0f, 10000.0f);
            entity.objectData = entity.baseObjectData;
            entity.objectData.modelViewProjection = projection * view * entity.baseObjectData.model;
            entity.objectData.cameraPosition = glm::vec4(cameraPosition, float(entityIndex));
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            entity.objectData.cameraRightAndTanHalfFov = glm::vec4(right, std::tan(glm::radians(25.0f)));
            entity.objectData.cameraUpAndAspect = glm::vec4(up, float(width) / float(height));
            entity.objectData.cameraForward = glm::vec4(forward, 0.0f);
        }

        /** Validates the three locked r185 mipmap scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool orbit = options.scenarioId == "mipmap-orbit" && options.targetFrame == 121u;
            if (options.caseId != "webgl_materials_cubemap_mipmaps" || (!initial && !animated && !orbit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgl_materials_cubemap_mipmaps requires its locked r185 scenario contract.");
            if (orbit != !options.inputReplayPath.empty())
                throw std::invalid_argument("Only the cubemap mipmap orbit scenario accepts an input replay.");
        }
    } // namespace

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        const std::filesystem::path root = resolveExamplesRoot(
            std::filesystem::path(options.assetRoot.c_str())) /
            "textures" / "cube" / "angus";
        entities.clear();
        entities.resize(EntityCount);
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            auto &entity = entities[entityIndex];
            buildSphere(entity);
            entity.baseObjectData.model = glm::translate(
                glm::mat4(1.0f), glm::vec3(entityIndex == 0u ? -100.0f : 100.0f, 0.0f, 0.0f));
            entity.baseObjectData.cameraPosition = glm::vec4(0.0f, 0.0f, 500.0f, float(entityIndex));
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColor = glm::vec4(1.0f);
            for (uint32_t face = 0u; face < CubeFaceCount; ++face)
            {
                if (entityIndex == 0u)
                    loadPackedFace(root, face, entity.textureBytes[face], entity.textureMipOffsets[face],
                                   entity.textureWidths[face], entity.textureHeights[face]);
            }
            updateMatrices(entity, entityIndex, options.targetFrame, options.width, options.height);
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not create the cubemap mipmap Scene RenderSet encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            auto &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("AngusSphere-") + eastl::to_string(entityIndex);
            appendBuffer(allocation, WebglMaterialsCubemapMipmapsSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendBuffer(allocation, WebglMaterialsCubemapMipmapsSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglMaterialsCubemapMipmapsSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglMaterialsCubemapMipmapsSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglMaterialsCubemapMipmapsSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            if (entityIndex == 0u)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textures;
                textures.textureComponentHandle = WebglMaterialsCubemapMipmapsSceneRenderSetComponents::textures;
                for (uint32_t face = 0u; face < CubeFaceCount; ++face)
                {
                    textures.textures.push_back({
                        .textureName = prefix + "-" + CubeFaceNames[face],
                        .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                        .width = entity.textureWidths[face],
                        .height = entity.textureHeights[face],
                        .data = entity.textureBytes[face].data(),
                        .dataStorageBytes = entity.textureBytes[face].size(),
                        .mipmapOffsetBytes = entity.textureMipOffsets[face],
                    });
                }
                allocation.textureInfos.push_back(eastl::move(textures));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
            updateMatrices(entities[entityIndex], entityIndex, frameIndex, 800u, 500u);
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not create cubemap mipmap update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglMaterialsCubemapMipmapsSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frame, uint32_t width,
        uint32_t height, uint64_t bytes) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_materials_cubemap_mipmaps\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
               << ",\"byteCount\":" << bytes << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_cubemap_mipmaps\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame << ",\n"
               << "  \"implementationLevel\":\"scaffolded\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglMaterialsCubemapMipmapsSceneRenderSet\",\n  \"renderableObjectCount\":2,\n"
               << "  \"entityCount\":2,\n  \"instanceCount\":2,\n  \"instanceCounts\":[1,1],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"scenePasses\":[{\"name\":\"mipmap-comparison\",\"renderClass\":\"WebglMaterialsCubemapMipmapsScenePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frame, GVM::RHI::Texture texture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frame != options.targetFrame) return;
        const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4u;
        if (bytes > std::numeric_limits<size_t>::max()) throw std::overflow_error("cubemap mipmap capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(bytes));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("cubemap mipmap capture has no graphics queue.");
        queue->readTexture(texture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frame, width, height, bytes);
        writeStructuralSnapshot(options, frame);
        captureWritten = true;
    }

    void WebglMaterialsCubemapMipmapsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
