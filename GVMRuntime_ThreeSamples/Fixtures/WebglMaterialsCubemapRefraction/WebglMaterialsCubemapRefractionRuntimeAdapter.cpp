#include "WebglMaterialsCubemapRefractionRuntimeAdapter.hpp"

#include "Fixtures/Phase1LoaderPlyRenderSet/PlyGeometry.hpp"
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
        constexpr uint32_t EntityCount = 3u;
        constexpr uint32_t CubeFaceCount = 6u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;
        constexpr const char *CanonicalSceneSha256 = "837f769e67155dc4a6e9a90683a61d6f6b4571a2a97d79586831011f450e66e2";
        constexpr const char *MouseReplaySha256 = "8d56c4fe171f85fa9a5a385e741320f4f02d11924c2e2c767fb5c29a3b90c4cf";
        constexpr const char *CubeFaceNames[CubeFaceCount] = {
            "px.jpg", "nx.jpg", "py.jpg", "ny.jpg", "pz.jpg", "nz.jpg"};

        /** Creates the parent directory for one optional capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Resolves the locked r185 examples directory from either asset-root layout. */
        std::filesystem::path resolveExamplesRoot(const std::filesystem::path &assetRoot)
        {
            if (std::filesystem::exists(assetRoot / "models" / "ply" / "binary" / "Lucy100k.ply"))
                return assetRoot;
            const std::filesystem::path nested = assetRoot / "examples";
            if (std::filesystem::exists(nested / "models" / "ply" / "binary" / "Lucy100k.ply"))
                return nested;
            throw std::runtime_error("The asset pack does not contain models/ply/binary/Lucy100k.ply.");
        }

        /** Validates the fixed refraction scenarios and single-sample output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "mouse-camera" && options.targetFrame == 30u;
            const bool snapshot = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            if (options.caseId != "webgl_materials_cubemap_refraction" || (!initial && !animated && !snapshot) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgl_materials_cubemap_refraction requires its locked r185 scenario contract.");
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Loads one JPEG cube face and retains its explicit sRGB mip chain. */
        void loadCubeFace(const std::filesystem::path &path,
                          eastl::vector<uint8_t> &bytes,
                          eastl::vector<uint64_t> &mipOffsets,
                          uint32_t &width,
                          uint32_t &height)
        {
            const RgbaImageData base = decodeJpegRgba8(path);
            const eastl::vector<RgbaImageData> mips = buildSrgbMipChain(base);
            if (mips.empty()) throw std::runtime_error("Refraction cube face has no mip levels: " + path.string());
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

        /** Converts an authored sRGB byte channel to the Three linear working space. */
        float srgbByteToLinear(uint32_t value)
        {
            const float channel = float(value) / 255.0f;
            return channel <= 0.04045f
                ? channel / 12.92f
                : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }

        /** Expands the indexed PLY using the smooth normals produced by Three's computeVertexNormals(). */
        void buildLucyGeometry(const PlyGeometry &geometry,
                               WebglMaterialsCubemapRefractionEntityData &entity)
        {
            if (geometry.positions.empty() || geometry.indices.size() % 3u != 0u)
                throw std::runtime_error("Lucy100k.ply did not produce triangle indices.");
            if (geometry.normals.size() != geometry.positions.size())
                throw std::runtime_error("Lucy100k.ply did not produce Three-compatible vertex normals.");
            entity.vertices.reserve(geometry.indices.size());
            entity.indices.reserve(geometry.indices.size());
            for (size_t offset = 0u; offset < geometry.indices.size(); offset += 3u)
            {
                const uint32_t a = geometry.indices[offset + 0u];
                const uint32_t b = geometry.indices[offset + 1u];
                const uint32_t c = geometry.indices[offset + 2u];
                if (a >= geometry.positions.size() || b >= geometry.positions.size() || c >= geometry.positions.size())
                    throw std::runtime_error("Lucy100k.ply contains an out-of-range face index.");
                // Three's PLY loader keeps the source winding.  The host viewport has a
                // reflected Y convention, so the shader applies the matching clip-space flip.
                const uint32_t order[3u] = {a, b, c};
                for (const uint32_t index : order)
                {
                    const glm::vec3 position = geometry.positions[index];
                    const glm::vec3 normal = geometry.normals[index];
                    entity.vertices.push_back({glm::vec4(position, 1.0f), glm::vec4(normal, 0.0f)});
                    entity.indices.push_back(static_cast<uint32_t>(entity.indices.size()));
                }
            }
        }

        /** Updates camera matrices and basis vectors consumed by the DSL. */
        void updateCamera(WebglMaterialsCubemapRefractionEntityData &entity,
                          uint32_t frameIndex,
                          uint32_t width,
                          uint32_t height)
        {
            static_cast<void>(frameIndex);
            // The frozen runner dispatches a pointer event while the upstream sample
            // listens for the legacy `mousemove` event on document.  Consequently the
            // locked r185 replay is intentionally a no-op, and the camera remains at
            // its initial position for the mouse-camera snapshot.
            const glm::vec3 cameraPosition(
                0.0f,
                0.0f,
                -4000.0f);
            const glm::mat4 view = glm::lookAt(
                cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = glm::perspective(
                glm::radians(50.0f), float(width) / float(height), 1.0f, 100000.0f);
            entity.objectData.modelView = view * entity.objectData.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPositionAndFlags = glm::vec4(cameraPosition, 0.0f);
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            entity.objectData.cameraRightAndTanHalfFov = glm::vec4(right, std::tan(glm::radians(25.0f)));
            entity.objectData.cameraUpAndAspect = glm::vec4(up, float(width) / float(height));
            entity.objectData.cameraForwardAndReserved = glm::vec4(forward, 0.0f);
        }
    } // namespace

    void WebglMaterialsCubemapRefractionRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        const std::filesystem::path examplesRoot = resolveExamplesRoot(std::filesystem::path(options.assetRoot.c_str()));
        const PlyGeometry lucy = loadPlyGeometry(
            examplesRoot / "models" / "ply" / "binary" / "Lucy100k.ply",
            PlyEncoding::BinaryLittleEndian);
        if (lucy.sourceVertexCount == 0u || lucy.sourceFaceCount == 0u)
            throw std::runtime_error("Lucy100k.ply has no source counts.");

        entities.clear();
        entities.resize(EntityCount);
        const glm::vec3 positions[EntityCount] = {
            {0.0f, 0.0f, 0.0f}, {-1500.0f, 0.0f, 0.0f}, {1500.0f, 0.0f, 0.0f}};
        const glm::vec4 colors[EntityCount] = {
            {1.0f, 1.0f, 1.0f, 1.0f},
            {srgbByteToLinear(0xccu), srgbByteToLinear(0xffu), srgbByteToLinear(0xfdu), 1.0f},
            {srgbByteToLinear(0xccu), srgbByteToLinear(0xddu), 1.0f, 1.0f}};
        const float refractionRatios[EntityCount] = {0.98f, 0.985f, 0.98f};
        const float reflectivities[EntityCount] = {1.0f, 1.0f, 0.9f};
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &entity = entities[index];
            buildLucyGeometry(lucy, entity);
            entity.objectData.model = glm::scale(
                glm::translate(glm::mat4(1.0f), positions[index]), glm::vec3(1.5f));
            entity.baseObjectData = entity.objectData;
            entity.materialData.baseColor = colors[index];
            entity.materialData.parameters = glm::vec4(refractionRatios[index], reflectivities[index], 0.0f, 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            for (uint32_t face = 0u; face < CubeFaceCount; ++face)
            {
                loadCubeFace(examplesRoot / "textures" / "cube" / "Park3Med" / CubeFaceNames[face],
                             entity.textureBytes[face], entity.textureMipOffsets[face],
                             entity.textureWidths[face], entity.textureHeights[face]);
            }
            updateCamera(entity, options.targetFrame, options.width, options.height);
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_cubemap_refraction could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("LucyRefraction-") + eastl::to_string(index);
            appendBuffer(allocation, WebglMaterialsCubemapRefractionSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendBuffer(allocation, WebglMaterialsCubemapRefractionSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglMaterialsCubemapRefractionSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglMaterialsCubemapRefractionSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglMaterialsCubemapRefractionSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            if (index == 0u)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textures;
                textures.textureComponentHandle = WebglMaterialsCubemapRefractionSceneRenderSetComponents::textures;
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

    void WebglMaterialsCubemapRefractionRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        for (auto &entity : entities)
        {
            entity.objectData = entity.baseObjectData;
            updateCamera(entity, frameIndex, options.width, options.height);
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_materials_cubemap_refraction could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglMaterialsCubemapRefractionSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frame, uint32_t width,
        uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"source\":\"gvm-three-r185\",\n"
               << "  \"caseId\":\"webgl_materials_cubemap_refraction\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\":" << frame << ",\n"
               << "  \"randomSeed\":" << options.randomSeed << ",\n"
               << "  \"width\":" << width << ",\n"
               << "  \"height\":" << height << ",\n"
               << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\":" << byteCount << ",\n"
               << "  \"format\":\"rgba8unorm\",\n"
               << "  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n"
               << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
               << "  \"inputReplay\":";
        if (options.scenarioId == "mouse-camera")
        {
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_materials_cubemap_refraction\",\"scenarioId\":\"mouse-camera\",\"captureFrame\":30,\"sha256\":\""
                   << MouseReplaySha256
                   << "\",\"target\":\"canvas[width=\\\"800\\\"][height=\\\"500\\\"]\",\"eventCount\":1,\"lastEventFrame\":0}";
        }
        else
        {
            output << "null";
        }
        output << "\n}\n";
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_cubemap_refraction\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"asset\":\"models/ply/binary/Lucy100k.ply + textures/cube/Park3Med/*.jpg\",\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglMaterialsCubemapRefractionSceneRenderSet\",\n  \"renderableObjectCount\":3,\n"
               << "  \"entityCount\":3,\n  \"instanceCounts\":[1,1,1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":2,\n"
               << "  \"screenPasses\":[\"cube-atlas-gutter-build\",\"cube-atlas-background\"],\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglMaterialsCubemapRefractionSceneRenderSet\",\"renderableObjectCount\":3,\"entityCount\":3,\"entities\":["
               << "{\"entityId\":" << entities[0u].entityIndex << ",\"logicalRenderableId\":\"lucy-center\",\"instanceCount\":1},"
               << "{\"entityId\":" << entities[1u].entityIndex << ",\"logicalRenderableId\":\"lucy-left\",\"instanceCount\":1},"
               << "{\"entityId\":" << entities[2u].entityIndex << ",\"logicalRenderableId\":\"lucy-right\",\"instanceCount\":1}],"
               << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
               << "\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglMaterialsCubemapRefractionMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglMaterialsCubemapRefractionMainPass\",\"sceneRoot\":\"scene\",\"renderSetBindingCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::writeSemanticSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.semanticSnapshotPath.empty() || options.scenarioId != "canonical-loader") return;
        const std::filesystem::path path(options.semanticSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        if (!output) throw std::runtime_error("Could not open cubemap refraction semantic snapshot path.");
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_materials_cubemap_refraction\",\n"
               << "  \"scenarioId\":\"canonical-loader\",\n"
               << "  \"frame\":" << frame << ",\n"
               << "  \"kind\":\"loader-snapshot\",\n"
               << "  \"canonicalState\":\"canonical-loaded-scene\",\n"
               << "  \"result\":{\"renderableObjectCount\":3,\"sceneRootCount\":1,\"canonicalSceneSha256\":\""
               << CanonicalSceneSha256 << "\"}\n"
               << "}\n";
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("cubemap refraction capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("cubemap refraction has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglMaterialsCubemapRefractionRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
