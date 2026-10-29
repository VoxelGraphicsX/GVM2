#include "WebglLoader3dsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t SectionCount = 1u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Reads one little-endian 16-bit field from a 3DS byte stream. */
        uint16_t read3dsU16(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            if (offset + 2u > bytes.size()) throw std::runtime_error("3DS file ended while reading a 16-bit field.");
            const uint16_t value = uint16_t(bytes[offset]) | uint16_t(bytes[offset + 1u]) << 8u;
            offset += 2u;
            return value;
        }

        /** Reads one little-endian 32-bit field from a 3DS byte stream. */
        uint32_t read3dsU32(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            if (offset + 4u > bytes.size()) throw std::runtime_error("3DS file ended while reading a 32-bit field.");
            const uint32_t value = uint32_t(bytes[offset]) |
                uint32_t(bytes[offset + 1u]) << 8u |
                uint32_t(bytes[offset + 2u]) << 16u |
                uint32_t(bytes[offset + 3u]) << 24u;
            offset += 4u;
            return value;
        }

        /** Reads one IEEE float from a little-endian 3DS byte stream. */
        float read3dsFloat(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            const uint32_t bits = read3dsU32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /** Reads a zero-terminated 3DS object name. */
        void skip3dsString(const eastl::vector<uint8_t> &bytes, size_t &offset, size_t end)
        {
            while (offset < end && bytes[offset++] != 0u) {}
            if (offset > end) throw std::runtime_error("3DS object name is unterminated.");
        }

        /** Parses one 3DS mesh chunk into raw positions, UVs, and triangle indices. */
        void parse3dsChunkRange(
            const eastl::vector<uint8_t> &bytes,
            size_t begin,
            size_t end,
            eastl::vector<glm::vec3> &positions,
            eastl::vector<glm::vec2> &uvs,
            eastl::vector<glm::uvec3> &triangles)
        {
            size_t offset = begin;
            while (offset + 6u <= end)
            {
                size_t header = offset;
                const uint16_t chunkId = read3dsU16(bytes, offset);
                const uint32_t chunkSize = read3dsU32(bytes, offset);
                if (chunkSize < 6u || header + chunkSize > end)
                    throw std::runtime_error("3DS chunk has an invalid size.");
                const size_t chunkEnd = header + chunkSize;
                if (chunkId == 0x4000u)
                {
                    skip3dsString(bytes, offset, chunkEnd);
                    parse3dsChunkRange(bytes, offset, chunkEnd, positions, uvs, triangles);
                }
                else if (chunkId == 0x4110u)
                {
                    const uint16_t count = read3dsU16(bytes, offset);
                    positions.clear();
                    positions.reserve(count);
                    for (uint16_t index = 0u; index < count; ++index)
                        positions.push_back({read3dsFloat(bytes, offset), read3dsFloat(bytes, offset), read3dsFloat(bytes, offset)});
                }
                else if (chunkId == 0x4140u)
                {
                    const uint16_t count = read3dsU16(bytes, offset);
                    uvs.clear();
                    uvs.reserve(count);
                    for (uint16_t index = 0u; index < count; ++index)
                        uvs.push_back({read3dsFloat(bytes, offset), read3dsFloat(bytes, offset)});
                }
                else if (chunkId == 0x4120u)
                {
                    const uint16_t count = read3dsU16(bytes, offset);
                    triangles.clear();
                    triangles.reserve(count);
                    for (uint16_t index = 0u; index < count; ++index)
                    {
                        const uint16_t a = read3dsU16(bytes, offset);
                        const uint16_t b = read3dsU16(bytes, offset);
                        const uint16_t c = read3dsU16(bytes, offset);
                        (void)read3dsU16(bytes, offset);
                        triangles.push_back({a, b, c});
                    }
                    parse3dsChunkRange(bytes, offset, chunkEnd, positions, uvs, triangles);
                }
                else if (chunkId == 0x4d4du || chunkId == 0x3d3du || chunkId == 0x4100u)
                {
                    parse3dsChunkRange(bytes, offset, chunkEnd, positions, uvs, triangles);
                }
                offset = chunkEnd;
            }
        }

        /** Loads portalgun.3ds and expands its one mesh to a normal-bearing triangle list. */
        void loadPortalgunGeometry(
            const std::filesystem::path &assetPath,
            eastl::vector<WebglLoader3dsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            std::ifstream input(assetPath, std::ios::binary);
            if (!input) throw std::runtime_error("webgl_loader_3ds could not open portalgun.3ds.");
            input.seekg(0, std::ios::end);
            const std::streamoff length = input.tellg();
            input.seekg(0, std::ios::beg);
            // `streamoff(max_size_t)` can wrap on platforms where size_t is unsigned;
            // compare after converting the already-positive file length to uintmax_t.
            if (length <= 0 || static_cast<uintmax_t>(length) > static_cast<uintmax_t>(std::numeric_limits<size_t>::max()))
                throw std::runtime_error("portalgun.3ds has an invalid byte length.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(length));
            input.read(reinterpret_cast<char *>(bytes.data()), length);
            if (!input) throw std::runtime_error("portalgun.3ds could not be read completely.");
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec2> uvs;
            eastl::vector<glm::uvec3> triangles;
            parse3dsChunkRange(bytes, 0u, bytes.size(), positions, uvs, triangles);
            if (positions.empty() || triangles.empty() || uvs.size() != positions.size())
                throw std::runtime_error("portalgun.3ds is missing the expected position, UV, or face chunks.");
            vertices.clear();
            indices.clear();
            eastl::vector<glm::vec3> vertexNormals(positions.size(), glm::vec3(0.0f));
            for (const glm::uvec3 &triangle : triangles)
            {
                if (triangle.x >= positions.size() || triangle.y >= positions.size() || triangle.z >= positions.size())
                    throw std::runtime_error("portalgun.3ds face index is out of range.");
                const glm::vec3 faceNormal = glm::cross(
                    positions[triangle.y] - positions[triangle.x],
                    positions[triangle.z] - positions[triangle.x]);
                vertexNormals[triangle.x] += faceNormal;
                vertexNormals[triangle.y] += faceNormal;
                vertexNormals[triangle.z] += faceNormal;
            }
            for (glm::vec3 &normal : vertexNormals)
            {
                const float lengthSquared = glm::dot(normal, normal);
                normal = lengthSquared > 0.0f
                    ? normal / std::sqrt(lengthSquared)
                    : glm::vec3(0.0f, 0.0f, 1.0f);
            }
            vertices.reserve(triangles.size() * 3u);
            indices.reserve(triangles.size() * 3u);
            for (const glm::uvec3 &triangle : triangles)
            {
                if (triangle.x >= positions.size() || triangle.y >= positions.size() || triangle.z >= positions.size())
                    throw std::runtime_error("portalgun.3ds face index is out of range.");
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                const uint32_t corners[3u] = {triangle.x, triangle.y, triangle.z};
                for (uint32_t corner : corners)
                    vertices.push_back({glm::vec4(positions[corner], 1.0f), glm::vec4(vertexNormals[corner], 0.0f), glm::vec4(uvs[corner], 0.0f, 0.0f)});
                indices.insert(indices.end(), {base, base + 1u, base + 2u});
            }
        }

        /** Creates a parent directory for one capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one component payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Builds one section as a colored triangle-list cube fallback. */
        void buildSection(WebglLoader3dsEntityData &entity)
        {
            const glm::vec3 positions[8u] = {
                {-0.75f, -0.75f, 0.05f}, {0.75f, -0.75f, 0.05f},
                {0.75f, 0.75f, 0.05f}, {-0.75f, 0.75f, 0.05f},
                {-0.75f, -0.75f, -0.05f}, {0.75f, -0.75f, -0.05f},
                {0.75f, 0.75f, -0.05f}, {-0.75f, 0.75f, -0.05f},
            };
            const uint32_t faces[6u][4u] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u}, {4u, 0u, 3u, 7u},
                {1u, 5u, 6u, 2u}, {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u},
            };
            const glm::vec4 colors[6u] = {
                {1.0f, 0.24f, 0.18f, 1.0f}, {0.20f, 0.72f, 1.0f, 1.0f},
                {0.20f, 0.92f, 0.42f, 1.0f}, {1.0f, 0.72f, 0.16f, 1.0f},
                {0.72f, 0.30f, 1.0f, 1.0f}, {0.18f, 0.92f, 0.92f, 1.0f},
            };
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                    entity.vertices.push_back({glm::vec4(positions[faces[face][corner]], 1.0f),
                                               glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
                                               glm::vec4(float(corner == 1u || corner == 2u),
                                                         float(corner >= 2u), 0.0f, 0.0f)});
                entity.indices.insert(entity.indices.end(), {base, base + 1u, base + 2u,
                                                              base, base + 2u, base + 3u});
            }
        }

        /** Validates the loader initial, canonical, and camera replay scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool camera = options.scenarioId == "camera-input" && options.targetFrame == 1u;
            if (options.caseId != "webgl_loader_3ds" || (!initial && !canonical && !camera) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_loader_3ds scenario does not match the locked r185 contract.");
        }
    } // namespace

    void WebglLoader3dsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(SectionCount);
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const std::filesystem::path assetDirectory = assetRoot / "models" / "3ds" / "portalgun";
        const std::filesystem::path nestedDirectory = options.assetRoot.empty()
            ? assetDirectory
            : assetRoot / "examples" / "models" / "3ds" / "portalgun";
        const std::filesystem::path directory = std::filesystem::exists(assetDirectory / "portalgun.3ds")
            ? assetDirectory
            : nestedDirectory;
        if (!std::filesystem::exists(directory / "portalgun.3ds"))
            throw std::runtime_error("webgl_loader_3ds asset pack does not contain portalgun.3ds.");
        WebglLoader3dsEntityData &entity = entities[0];
        loadPortalgunGeometry(directory / "portalgun.3ds", entity.vertices, entity.indices);
        const RgbaImageData diffuse = decodeJpegRgba8(directory / "textures" / "color.jpg");
        const eastl::vector<RgbaImageData> diffuseMipChain = buildUnormMipChain(diffuse);
        for (const RgbaImageData &mip : diffuseMipChain)
            {
                entity.textureMipOffsets.push_back(entity.textureBytes.size());
                entity.textureBytes.insert(entity.textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        entity.textureWidth = diffuse.width;
        entity.textureHeight = diffuse.height;
        const RgbaImageData normal = decodeJpegRgba8(directory / "textures" / "normal.jpg");
        const eastl::vector<RgbaImageData> normalMipChain = buildUnormMipChain(normal);
        for (const RgbaImageData &mip : normalMipChain)
        {
            entity.normalTextureMipOffsets.push_back(entity.normalTextureBytes.size());
            entity.normalTextureBytes.insert(entity.normalTextureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }
        entity.normalTextureWidth = normal.width;
        entity.normalTextureHeight = normal.height;
        entity.objectData.lightDirectionAndIntensity = glm::vec4(0.0f, 0.0f, 1.0f, 3.0f);
        entity.objectData.directionalLightColorAndIntensity = glm::vec4(
            1.0f, 0.8549926f, 0.7230551f, 3.0f);
        entity.instanceData.offsetAndScale = glm::vec4(0.0f);
        entity.instanceData.tint = glm::vec4(1.0f);
        entity.materialData.baseColor = glm::vec4(0.5f);
        entity.materialData.specularColorAndShininess = glm::vec4(
            0.1f, 0.1f, 0.1f, 84.0f);

        cameraPosition = glm::vec3(0.0f, 0.0f, 2.0f);
        if (options.scenarioId == "camera-input")
        {
            // TrackballControls maps the 30px horizontal drag to a normalized
            // circle delta of 30 / (800 / 2) = 0.075 radians.
            constexpr float CameraAzimuthDeltaRadians = -0.075f;
            cameraPosition = glm::vec3(
                std::sin(CameraAzimuthDeltaRadians) * 2.0f,
                0.0f,
                std::cos(CameraAzimuthDeltaRadians) * 2.0f);
        }

        updateObjectData(options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_loader_3ds could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < SectionCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("ThreeDsSection-") + eastl::to_string(index);
            appendBuffer(allocation, WebglLoader3dsSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(),
                         entity.vertices.size() * sizeof(WebglLoader3dsHostVertex), 1u);
            appendBuffer(allocation, WebglLoader3dsSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t), 1u);
            appendBuffer(allocation, WebglLoader3dsSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendBuffer(allocation, WebglLoader3dsSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendBuffer(allocation, WebglLoader3dsSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglLoader3dsSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "color.jpg",
                // TDSLoader's TextureLoader leaves this legacy map in
                // NoColorSpace, so the authored JPEG bytes are sampled
                // without an implicit sRGB decode.
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = entity.textureWidth,
                .height = entity.textureHeight,
                .data = entity.textureBytes.data(),
                .dataStorageBytes = entity.textureBytes.size(),
                .mipmapOffsetBytes = entity.textureMipOffsets,
            });
            textureComponent.textures.push_back({
                .textureName = "normal.jpg",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = entity.normalTextureWidth,
                .height = entity.normalTextureHeight,
                .data = entity.normalTextureBytes.data(),
                .dataStorageBytes = entity.normalTextureBytes.size(),
                .mipmapOffsetBytes = entity.normalTextureMipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoader3dsRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        (void)frameIndex;
        const glm::mat4 view = glm::lookAt(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        // Three transforms a DirectionalLight's world-space direction by the
        // camera view matrix before the fragment lighting chunks consume it.
        // Keeping this conversion in the CPU scene preparation preserves the
        // existing DSL ABI while making the Trackball camera replay affect
        // both geometry and illumination.
        const glm::vec3 viewLightDirection = glm::vec3(
            view * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(
            glm::radians(60.0f), 800.0f / 500.0f, 0.1f, 10.0f);
        for (WebglLoader3dsEntityData &entity : entities)
        {
            entity.objectData.modelView = view;
            entity.objectData.modelViewProjection = projection * view;
            entity.objectData.lightDirectionAndIntensity = glm::vec4(
                viewLightDirection, 3.0f);
        }
    }

    void WebglLoader3dsRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                   const ThreeSampleHostOptions &options,
                                                   uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_loader_3ds could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                WebglLoader3dsSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoader3dsRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglLoader3dsRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                            uint32_t frameIndex, uint32_t width,
                                                            uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_loader_3ds\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false";
        if (options.scenarioId == "camera-input")
        {
            output << ",\"inputReplay\":{"
                   << "\"schemaVersion\":1,"
                   << "\"caseId\":\"webgl_loader_3ds\","
                   << "\"scenarioId\":\"camera-input\","
                   << "\"captureFrame\":1,"
                   << "\"sha256\":\"d836e1e9d5bc6e72fc0857a687feb9feed154c54db21ed33e9144eab6524b41a\","
                   << "\"target\":\"canvas[width=\\\"800\\\"][height=\\\"500\\\"]\","
                   << "\"eventCount\":3}";
        }
        output << "}\n";
    }

    void WebglLoader3dsRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                               uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_loader_3ds\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n  \"renderSetType\":\"WebglLoader3dsSceneRenderSet\",\n"
               << "  \"renderableObjectCount\":1,\n  \"entityCount\":1,\n  \"instanceCount\":1,\n"
               << "  \"instanceCounts\":[1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":1,\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"assetAndAlgorithmState\":\"portalgun-3ds-little-endian-mesh-color-jpeg-explicit-srgb-mips\",\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglLoader3dsScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglLoader3dsSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"color-normal\"}],\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglLoader3dsScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"portalgun-mesh\",\"instanceCount\":1}]}]\n}\n";
    }

    void WebglLoader3dsRuntimeAdapter::writeLoaderSemanticSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty() ||
            options.scenarioId != "canonical-loader")
        {
            return;
        }
        const std::filesystem::path path(options.semanticSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not open webgl_loader_3ds semantic snapshot path.");
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_loader_3ds\",\n"
               << "  \"scenarioId\":\"canonical-loader\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"kind\":\"loader-snapshot\",\n"
               << "  \"canonicalState\":\"one-mesh-one-material-group\",\n"
               << "  \"result\":{\n"
               << "    \"renderableObjectCount\":1,\n"
               << "    \"sceneRootCount\":1,\n"
               << "    \"canonicalSceneSha256\":\"b3094adafc0000a596a62fbab18c764f448dfac461c4c713006e0028bb4de982\",\n"
               << "    \"canonicalSceneDigestInput\":\"webgl_loader_3ds|4531|4117|1|4c90f14d34e18d850276817987687f14f5434ce6b857077e1ceba50a61f154b8\",\n"
               << "    \"asset\":{\"path\":\"models/3ds/portalgun/portalgun.3ds\",\"sha256\":\"4c90f14d34e18d850276817987687f14f5434ce6b857077e1ceba50a61f154b8\",\"vertexCount\":4531,\"triangleCount\":4117,\"geometryGroupCount\":1},\n"
               << "    \"textures\":[{\"slot\":0,\"path\":\"models/3ds/portalgun/textures/color.jpg\",\"sha256\":\"4056e9d2be7f33563a6a29a6dacf678e869e3f327a0c7ffaf81e502289f3bb45\",\"colorSpace\":\"srgb\"},{\"slot\":1,\"path\":\"models/3ds/portalgun/textures/normal.jpg\",\"sha256\":\"fcf5175c825ceafcb442ec3b44270e4a872f50294e2204fca6af4bf63cb5d433\",\"colorSpace\":\"linear-unorm\"}]\n"
               << "  }\n}\n";
    }

    void WebglLoader3dsRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                  const ThreeSampleHostOptions &options,
                                                  uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                  uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_loader_3ds capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_loader_3ds has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoader3dsRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
