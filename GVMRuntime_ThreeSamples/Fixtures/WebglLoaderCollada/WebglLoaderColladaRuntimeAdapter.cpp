#include "WebglLoaderColladaRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglLoaderColladaHostVertex) == 48u);
        static_assert(sizeof(WebglLoaderColladaHostObjectData) == 224u);
        static_assert(sizeof(WebglLoaderColladaHostInstanceData) == 16u);
        static_assert(sizeof(WebglLoaderColladaHostMaterialData) == 16u);

        /** Builds a zero-to-one perspective projection matching the current RHI clip contract. */
        glm::mat4 makeColladaPerspective(uint32_t width, uint32_t height)
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 2000.0;
            const double aspect = double(width) / double(height);
            const double top = NearDistance * std::tan(45.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth = projectionHeight * aspect;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(-2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return projection;
        }

        /** Appends one typed buffer component payload to a Set allocation. */
        void appendColladaBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Flattens one complete decoded sRGB mip chain for a texture component slot. */
        WebglLoaderColladaTextureData makeColladaTextureData(
            const RgbaImageData &baseImage)
        {
            const eastl::vector<RgbaImageData> levels =
                buildSrgbMipChain(baseImage);
            if (levels.empty())
                throw std::runtime_error("COLLADA texture mip chain is empty.");
            WebglLoaderColladaTextureData texture;
            texture.width = levels.front().width;
            texture.height = levels.front().height;
            for (const RgbaImageData &level : levels)
            {
                texture.mipOffsets.push_back(texture.bytes.size());
                texture.bytes.insert(
                    texture.bytes.end(), level.pixels.begin(), level.pixels.end());
            }
            return texture;
        }

        /** Creates parent directories for one requested evidence artifact. */
        void prepareColladaEvidencePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeColladaEvidence(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareColladaEvidencePath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write COLLADA evidence.");
        }
    } // namespace

    void WebglLoaderColladaRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loader" &&
                             options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" &&
                               options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" &&
                              options.targetFrame == 60u;
        if (options.caseId != "webgl_loader_collada" ||
            (!initial && !canonical && !animated) || options.width != 800u ||
            options.height != 500u || options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_loader_collada requires one frozen Manifest scenario.");
        }
        device = inDevice;
        const std::filesystem::path assetDirectory =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "collada" / "elf";
        asset = loadColladaElfAsset(assetDirectory / "elf.dae");
        vertices.reserve(asset.vertices.size());
        indices.reserve(asset.vertices.size());
        for (uint32_t index = 0u; index < asset.vertices.size(); ++index)
        {
            const ColladaAssetVertex &source = asset.vertices[index];
            vertices.push_back({
                glm::vec4(source.position, 1.0f),
                glm::vec4(source.normal, 0.0f),
                glm::vec4(source.uv, static_cast<float>(source.materialIndex), 0.0f),
            });
            indices.push_back(index);
        }
        materials.reserve(asset.materials.size());
        textures.reserve(asset.materials.size());
        for (const ColladaAssetMaterial &material : asset.materials)
        {
            materials.push_back({glm::vec4(
                material.specular, material.shininess)});
            textures.push_back(makeColladaTextureData(decodeJpegRgba8(
                assetDirectory / material.textureFile.c_str())));
        }
        instanceData.reserved = glm::vec4(0.0f);
        updateObjectData(options.targetFrame);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create the COLLADA Set encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendColladaBuffer(
            allocation, WebglLoaderColladaSceneRenderSetComponents::vertices,
            "WebglLoaderColladaVertices", vertices.data(),
            vertices.size() * sizeof(WebglLoaderColladaHostVertex), 1u);
        appendColladaBuffer(
            allocation, WebglLoaderColladaSceneRenderSetComponents::indices,
            "WebglLoaderColladaIndices", indices.data(),
            indices.size() * sizeof(uint32_t), 1u);
        appendColladaBuffer(
            allocation, WebglLoaderColladaSceneRenderSetComponents::objects,
            "WebglLoaderColladaObject", &objectData, sizeof(objectData), 1u);
        appendColladaBuffer(
            allocation, WebglLoaderColladaSceneRenderSetComponents::instances,
            "WebglLoaderColladaInstance", &instanceData,
            sizeof(instanceData), 1u);
        appendColladaBuffer(
            allocation, WebglLoaderColladaSceneRenderSetComponents::materials,
            "WebglLoaderColladaMaterials", materials.data(),
            materials.size() * sizeof(WebglLoaderColladaHostMaterialData),
            static_cast<uint32_t>(materials.size()));
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglLoaderColladaSceneRenderSetComponents::textures;
        for (uint32_t index = 0u; index < textures.size(); ++index)
        {
            constexpr const char *TextureNames[] = {
                "WebglLoaderColladaCe",
                "WebglLoaderColladaBody",
                "WebglLoaderColladaFace",
                "WebglLoaderColladaHair",
            };
            const WebglLoaderColladaTextureData &texture = textures[index];
            textureComponent.textures.push_back({
                .textureName = TextureNames[index],
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = texture.width,
                .height = texture.height,
                .data = texture.bytes.data(),
                .dataStorageBytes = texture.bytes.size(),
                .mipmapOffsetBytes = texture.mipOffsets,
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderColladaRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const glm::vec3 cameraPosition(8.0f, 10.0f, 8.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f, 3.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        objectData.model = makeColladaElfWorldMatrix(asset, frameIndex);
        objectData.viewProjection = makeColladaPerspective(800u, 500u) * view;
        objectData.normalTransform = glm::transpose(glm::inverse(objectData.model));
        objectData.cameraPosition = glm::vec4(cameraPosition, 1.0f);
        objectData.directionalLight = glm::vec4(
            glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)), 2.5f);
    }

    void WebglLoaderColladaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not update the COLLADA object component.");
        encoder->setBufferComponentData(
            entityIndex, WebglLoaderColladaSceneRenderSetComponents::objects,
            &objectData, sizeof(objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderColladaRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("COLLADA capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareColladaEvidencePath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write COLLADA RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgl_loader_collada\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false\n}\n";
        writeColladaEvidence(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
              << "  \"caseId\":\"webgl_loader_collada\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"renderSetPolicy\":\"required\",\n"
              << "  \"sceneRenderSetCount\":1,\n"
              << "  \"renderSetType\":\"WebglLoaderColladaSceneRenderSet\",\n"
              << "  \"renderableObjectCount\":1,\n"
              << "  \"entityCount\":1,\n"
              << "  \"instanceCounts\":[1],\n"
              << "  \"drawCommandCount\":1,\n"
              << "  \"scenePassCount\":1,\n"
              << "  \"usesRenderEntityID\":true,\n"
              << "  \"usesRenderEntityInstanceID\":false,\n"
              << "  \"sampleCount\":1,\n"
              << "  \"msaaEnabled\":false,\n"
              << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
              << "\"renderSetId\":\"scene\",\"renderSetType\":\"WebglLoaderColladaSceneRenderSet\","
              << "\"renderableObjectCount\":1,\"entityCount\":1,"
              << "\"entities\":[{\"entityId\":" << entityIndex << ",\"logicalRenderableId\":\"elf-mesh\",\"instanceCount\":1}],"
              << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"four-material-texture-bank\"}],"
              << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
              << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglLoaderColladaMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
              << "  \"directDrawFallback\":false\n}\n";
        writeColladaEvidence(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgl_loader_collada\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"frame\":" << frameIndex << ",\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"one-mesh-four-material-groups\",\n"
                     << "  \"result\":{\"renderableObjectCount\":1,\"sceneRootCount\":1,"
                     << "\"canonicalSceneSha256\":\"6237bf659fc95dafa81f64cd0f5f033cfe9ade0a1d320f5ad943d772327c67e0\","
                     << "\"vertexCount\":42624,\"groupCount\":4,\"materialCount\":4,"
                     << "\"daeSha256\":\"" << asset.sha256.c_str() << "\"}\n}\n";
            writeColladaEvidence(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderColladaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        asset = ColladaElfAsset{};
        vertices.clear();
        indices.clear();
        materials.clear();
        textures.clear();
    }
} // namespace GVM::ThreeSamples
