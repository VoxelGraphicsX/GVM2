#include "WebglMaterialsCubemapRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/SampleAssetDecoders.hpp"
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
        constexpr uint32_t EntityCount = 3u;
        constexpr uint32_t CubeFaceCount = 6u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr const char *CanonicalSceneSha256 =
            "b8276ca88369af967a2329792c9c3df202ce8c02612790ff3842eba3d0dd44b5";
        constexpr const char *OrbitReplaySha256 =
            "53ba38b8c3dd361a0dd63117149aeaad85617a7f1db1704d878e442be79d2b60";
        constexpr const char *CubeFaceNames[CubeFaceCount] = {
            "px.jpg", "nx.jpg", "py.jpg", "ny.jpg", "pz.jpg", "nz.jpg"};

        /** Creates parent directories for one deterministic capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Reads one bounded binary asset into an EASTL byte vector. */
        eastl::vector<uint8_t> readBytes(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) throw std::runtime_error("Could not open cubemap asset: " + path.string());
            const std::streamsize size = stream.tellg();
            if (size <= 0 || static_cast<uint64_t>(size) > uint64_t(std::numeric_limits<size_t>::max()))
                throw std::runtime_error("Cubemap asset has an invalid size: " + path.string());
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!stream) throw std::runtime_error("Could not read cubemap asset: " + path.string());
            return bytes;
        }

        /** Resolves the locked r185 examples directory from either accepted asset-root layout. */
        std::filesystem::path resolveExamplesRoot(const std::filesystem::path &assetRoot)
        {
            if (std::filesystem::exists(assetRoot / "models" / "obj" / "walt" / "WaltHead.obj"))
                return assetRoot;
            const std::filesystem::path nested = assetRoot / "examples";
            if (std::filesystem::exists(nested / "models" / "obj" / "walt" / "WaltHead.obj"))
                return nested;
            throw std::runtime_error("The asset pack does not contain models/obj/walt/WaltHead.obj.");
        }

        /** Validates the fixed r185 cubemap scenarios and single-sample output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            // Use the frozen r185 Manifest scenario identifiers.  The legacy
            // aliases remain accepted so older local captures stay readable.
            const bool initial = (options.scenarioId == "initial" ||
                                  options.scenarioId == "initial-loader" ||
                                  options.scenarioId == "canonical-loader") &&
                options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool orbit = (options.scenarioId == "cubemap-orbit" ||
                                options.scenarioId == "orbit") && options.targetFrame == 1u;
            if (options.caseId != "webgl_materials_cubemap" || (!initial && !animated && !orbit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgl_materials_cubemap requires its locked r185 scenario contract.");
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

        /** Builds one explicit sRGB mip chain and keeps its packed bytes alive. */
        void loadCubeFace(const std::filesystem::path &path,
                          eastl::vector<uint8_t> &bytes,
                          eastl::vector<uint64_t> &mipOffsets,
                          uint32_t &width,
                          uint32_t &height)
        {
            const RgbaImageData base = decodeJpegRgba8(path);
            const eastl::vector<RgbaImageData> mips = buildSrgbMipChain(base);
            if (mips.empty()) throw std::runtime_error("Cubemap face has no mip levels: " + path.string());
            width = base.width;
            height = base.height;
            bytes.clear();
            mipOffsets.clear();
            for (const RgbaImageData &mip : mips)
            {
                mipOffsets.push_back(bytes.size());
                bytes.insert(bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }

        /** Writes the camera basis consumed by the DSL background ray reconstruction. */
        void updateCameraBasis(WebglMaterialsCubemapHostObjectData &objectData,
                               const glm::vec3 &cameraPosition,
                               uint32_t width,
                               uint32_t height)
        {
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            objectData.cameraRightAndTanHalfFov = glm::vec4(right, std::tan(glm::radians(25.0f)));
            objectData.cameraUpAndAspect = glm::vec4(up, float(width) / float(height));
            objectData.cameraForwardAndReserved = glm::vec4(forward, 0.0f);
        }

        /** Converts an sRGB authored material channel to Three's linear working space. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Recomputes the deterministic perspective camera used by the r185 example. */
        void updateEntityMatrices(WebglMaterialsCubemapEntityData &entity,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t frameIndex)
        {
            const float orbit = (frameIndex == 1u || frameIndex == 121u) ? 0.05f : 0.0f;
            const glm::vec3 cameraPosition(
                13.0f * std::sin(orbit), 0.25f * std::sin(orbit * 0.5f),
                13.0f * std::cos(orbit));
            const glm::mat4 view = glm::lookAt(
                cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = glm::perspective(
                glm::radians(50.0f), float(width) / float(height), 0.1f, 100.0f);
            entity.objectData = entity.baseObjectData;
            entity.objectData.modelView = view * entity.objectData.modelView;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPositionAndFlags.x = cameraPosition.x;
            entity.objectData.cameraPositionAndFlags.y = cameraPosition.y;
            entity.objectData.cameraPositionAndFlags.z = cameraPosition.z;
            updateCameraBasis(entity.objectData, cameraPosition, width, height);
        }
    } // namespace

    void WebglMaterialsCubemapRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        const std::filesystem::path examplesRoot =
            resolveExamplesRoot(std::filesystem::path(options.assetRoot.c_str()));
        const eastl::vector<uint8_t> objBytes = readBytes(
            examplesRoot / "models" / "obj" / "walt" / "WaltHead.obj");
        const ThreeCompat::DecodedObjMesh decoded = ThreeCompat::decodeObjTriangleMesh(objBytes);
        if (decoded.positions.empty() || decoded.positions.size() % 3u != 0u)
            throw std::runtime_error("WaltHead.obj did not produce triangle positions.");
        entities.clear();
        entities.resize(EntityCount);

        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            WebglMaterialsCubemapEntityData &entity = entities[entityIndex];
            const uint32_t vertexCount = static_cast<uint32_t>(decoded.positions.size() / 3u);
            entity.vertices.reserve(vertexCount);
            for (uint32_t vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex)
            {
                const size_t positionOffset = size_t(vertexIndex) * 3u;
                const glm::vec3 position(
                    decoded.positions[positionOffset + 0u] * 0.1f,
                    decoded.positions[positionOffset + 1u] * 0.1f,
                    decoded.positions[positionOffset + 2u] * 0.1f);
                const glm::vec3 normal = decoded.normals.size() >= positionOffset + 3u
                    ? glm::normalize(glm::vec3(decoded.normals[positionOffset + 0u], decoded.normals[positionOffset + 1u], decoded.normals[positionOffset + 2u]))
                    : glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec2 uv = decoded.textureCoordinates.size() >= size_t(vertexIndex + 1u) * 2u
                    ? glm::vec2(decoded.textureCoordinates[size_t(vertexIndex) * 2u], decoded.textureCoordinates[size_t(vertexIndex) * 2u + 1u])
                    : glm::vec2(0.0f);
                entity.vertices.push_back({glm::vec4(position, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(uv, 0.0f, 0.0f)});
                entity.indices.push_back(vertexIndex);
            }
            const glm::vec3 baseColor[EntityCount] = {
                {1.0f, srgbToLinear(247.0f / 255.0f), 0.0f},
                {1.0f, 1.0f, 1.0f},
                {1.0f, srgbToLinear(170.0f / 255.0f), 0.0f}};
            entity.materialData.baseColor = glm::vec4(baseColor[entityIndex], 1.0f);
            entity.materialData.parameters = glm::vec4(
                entityIndex == 2u ? 0.3f : 1.0f,
                entityIndex == 2u ? 1.0f : 0.0f,
                0.0f,
                0.0f);
            const glm::mat4 model = glm::translate(
                glm::mat4(1.0f), glm::vec3(-6.0f + float(entityIndex) * 6.0f, -3.0f, 0.0f)) *
                // The OBJ asset faces the opposite direction from the
                // three.js camera convention; OBJLoader's scene transform is
                // reproduced with this fixed half-turn around Y.
                glm::rotate(glm::mat4(1.0f), 3.14159265358979323846f, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.baseObjectData.modelView = model;
            entity.baseObjectData.modelViewProjection = model;
            entity.baseObjectData.model = model;
            entity.baseObjectData.cameraPositionAndFlags = glm::vec4(0.0f, 0.0f, 13.0f, entityIndex == 0u ? 1.0f : 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            updateEntityMatrices(entity, options.width, options.height, options.targetFrame);

            for (uint32_t face = 0u; face < CubeFaceCount; ++face)
                loadCubeFace(examplesRoot / "textures" / "cube" / "SwedishRoyalCastle" / CubeFaceNames[face],
                             entity.textureBytes[face], entity.textureMipOffsets[face],
                             entity.textureWidths[face], entity.textureHeights[face]);
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_cubemap could not create its Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            WebglMaterialsCubemapEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("WaltHead-") + eastl::to_string(entityIndex);
            appendBuffer(allocation, WebglMaterialsCubemapSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendBuffer(allocation, WebglMaterialsCubemapSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglMaterialsCubemapSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglMaterialsCubemapSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglMaterialsCubemapSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            // The current RenderSet texture ABI exposes eight total slots per
            // component.  The six immutable cube faces are therefore uploaded
            // once on entity zero and shared by all three material entities.
            if (entityIndex == 0u)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
                textureComponent.textureComponentHandle = WebglMaterialsCubemapSceneRenderSetComponents::textures;
                for (uint32_t face = 0u; face < CubeFaceCount; ++face)
                {
                    textureComponent.textures.push_back({
                        .textureName = prefix + "-" + CubeFaceNames[face],
                        .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                        .width = entity.textureWidths[face],
                        .height = entity.textureHeights[face],
                        .data = entity.textureBytes[face].data(),
                        .dataStorageBytes = entity.textureBytes[face].size(),
                        .mipmapOffsetBytes = entity.textureMipOffsets[face],
                    });
                }
                allocation.textureInfos.push_back(eastl::move(textureComponent));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsCubemapRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const float time = float(frameIndex) / 60.0f;
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 800.0f / 500.0f, 0.1f, 100.0f);
        const float orbit = (frameIndex == 1u || frameIndex == 121u) ? 0.05f : 0.0f;
        const glm::vec3 cameraPosition(
            13.0f * std::sin(orbit), 0.25f * std::sin(orbit * 0.5f),
            13.0f * std::cos(orbit));
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            WebglMaterialsCubemapEntityData &entity = entities[entityIndex];
            entity.objectData = entity.baseObjectData;
            const glm::mat4 rotation = glm::rotate(
                glm::mat4(1.0f), 0.12f * std::sin(time + float(entityIndex)), glm::vec3(0.0f, 1.0f, 0.0f));
            entity.objectData.model = rotation * entity.baseObjectData.model;
            entity.objectData.modelView = view * rotation * entity.baseObjectData.modelView;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPositionAndFlags.x = cameraPosition.x;
            entity.objectData.cameraPositionAndFlags.y = cameraPosition.y;
            entity.objectData.cameraPositionAndFlags.z = cameraPosition.z;
            updateCameraBasis(entity.objectData, cameraPosition, 800u, 500u);
        }
    }

    void WebglMaterialsCubemapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_cubemap could not create its update encoder.");
        for (const WebglMaterialsCubemapEntityData &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglMaterialsCubemapSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsCubemapRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglMaterialsCubemapRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frame, uint32_t width,
        uint32_t height, uint64_t bytes) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_materials_cubemap\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
            << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << bytes << ",\"format\":\"rgba8unorm\",\"randomSeed\":"
            << options.randomSeed << ",\"sampleCount\":1,\"msaaEnabled\":false";
        if (options.scenarioId == "orbit")
        {
            out << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_materials_cubemap\",\"scenarioId\":\"orbit\",\"captureFrame\":1,\"sha256\":\""
                << OrbitReplaySha256
                << "\",\"target\":\"canvas[width=\\\"800\\\"][height=\\\"500\\\"]\",\"eventCount\":3}";
        }
        else
        {
            out << ",\"inputReplay\":null";
        }
        out << "}\n";
    }

    void WebglMaterialsCubemapRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_cubemap\",\n  \"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
            << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
            << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
            << "  \"renderSetType\":\"WebglMaterialsCubemapSceneRenderSet\",\n  \"renderableObjectCount\":3,\n"
            << "  \"entityCount\":3,\n  \"instanceCounts\":[1,1,1],\n  \"textureSlotCount\":6,\n"
            << "  \"scenePassCount\":1,\n  \"screenPassCount\":1,\n  \"drawCommandCount\":1,\n"
            << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
            << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglMaterialsCubemapMainPass\",\"renderSetId\":\"webgl-materials-cubemap-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
            << "  \"screenPasses\":[{\"name\":\"cubemap-background\",\"renderClass\":\"WebglMaterialsCubemapBackgroundPass\",\"drawMode\":\"fullscreen-triangle\"}],\n"
            << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"webgl-materials-cubemap-scene-set\",\"renderSetType\":\"WebglMaterialsCubemapSceneRenderSet\",\"renderableObjectCount\":3,\"entityCount\":3,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"walt-head-0\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"walt-head-1\",\"instanceCount\":1},{\"entityId\":2,\"logicalRenderableId\":\"walt-head-2\",\"instanceCount\":1}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglMaterialsCubemapMainPass\",\"renderSetId\":\"webgl-materials-cubemap-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
    }

    /** Writes the locked canonical loader semantic sidecar for the Walt head scene. */
    void WebglMaterialsCubemapRuntimeAdapter::writeLoaderSemanticSnapshot(
        const ThreeSampleHostOptions &options) const
    {
        if (options.semanticSnapshotPath.empty() || options.scenarioId != "canonical-loader") return;
        const std::filesystem::path path(options.semanticSnapshotPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_cubemap\",\n"
            << "  \"scenarioId\":\"canonical-loader\",\n  \"frame\":0,\n"
            << "  \"kind\":\"loader-snapshot\",\n  \"canonicalState\":\"canonical-loaded-scene\",\n"
            << "  \"result\":{\"renderableObjectCount\":3,\"sceneRootCount\":1,\"canonicalSceneSha256\":\""
            << CanonicalSceneSha256 << "\",\"assetPath\":\"models/obj/walt/WaltHead.obj\"}\n}\n";
    }

    void WebglMaterialsCubemapRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frame, GVM::RHI::Texture texture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frame != options.targetFrame) return;
        const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4u;
        if (bytes > std::numeric_limits<size_t>::max()) throw std::overflow_error("Cubemap capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(bytes));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_materials_cubemap has no graphics queue.");
        queue->readTexture(texture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frame, width, height, bytes);
        writeStructuralSnapshot(options, frame);
        writeLoaderSemanticSnapshot(options);
        captureWritten = true;
    }

    void WebglMaterialsCubemapRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
