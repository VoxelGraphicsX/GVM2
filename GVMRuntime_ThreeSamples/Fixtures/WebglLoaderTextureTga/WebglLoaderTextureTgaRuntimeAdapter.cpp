#include "WebglLoaderTextureTgaRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/string.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TgaExtent = 256u;
        constexpr uint32_t TgaMipCount = 9u;
        constexpr uint32_t TgaCount = 2u;
        constexpr const char *TgaNames[TgaCount] = {
            "crate_grey8.tga", "crate_color8.tga"};
        constexpr const char *TgaHashes[TgaCount] = {
            "6ae83da1bc71f79c03b8bf87bfff664a8b81847297806bb2cf12ef96d29588fa",
            "b3c7ca4ea77ecd215f086dc5bea152e183e202c108aa77982130efa3eaed8d9d"};
        constexpr float BoxPositions[TgaCount] = {1.0f, -1.0f};
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebglLoaderTextureTgaHostVertex) == 48u);
        static_assert(sizeof(WebglLoaderTextureTgaHostObjectData) == 224u);
        static_assert(sizeof(WebglLoaderTextureTgaHostInstanceData) == 16u);
        static_assert(sizeof(WebglLoaderTextureTgaHostMaterialData) == 32u);

        /** Reads one bounded file and returns its SHA-256 identity. */
        eastl::string calculateTgaSha256(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a locked TGA asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("A locked TGA asset has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read a complete TGA asset.");
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Decodes the locked TGA formats with the same channel/origin rules as TGALoader. */
        RgbaImageData decodeTgaRgba8(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a locked TGA asset.");
            const std::streamoff size = input.tellg();
            if (size < 18)
                throw std::runtime_error("A TGA header is truncated.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
                throw std::runtime_error("Could not read a complete TGA asset.");

            const auto readU16 = [&bytes](size_t offset) -> uint32_t
            {
                if (offset + 1u >= bytes.size())
                    throw std::runtime_error("A TGA field is out of bounds.");
                return uint32_t(bytes[offset]) |
                    (uint32_t(bytes[offset + 1u]) << 8u);
            };
            const uint8_t idLength = bytes[0u];
            const uint8_t colorMapType = bytes[1u];
            const uint8_t imageType = bytes[2u];
            const uint32_t colorMapLength = readU16(5u);
            const uint32_t colorMapDepth = bytes[7u];
            const uint32_t width = readU16(12u);
            const uint32_t height = readU16(14u);
            const uint32_t pixelDepth = bytes[16u];
            const uint8_t descriptor = bytes[17u];
            const bool indexed = imageType == 1u || imageType == 9u;
            const bool grayscale = imageType == 3u || imageType == 11u;
            const bool rle = imageType == 9u || imageType == 10u || imageType == 11u;
            if (width == 0u || height == 0u || (!indexed && !grayscale && imageType != 2u && imageType != 10u))
                throw std::runtime_error("Unsupported TGA image type or extent.");
            if (indexed && (colorMapType != 1u || colorMapDepth != 24u))
                throw std::runtime_error("Unsupported indexed TGA palette.");
            if (!indexed && colorMapType != 0u)
                throw std::runtime_error("Unexpected TGA color map.");
            const uint32_t sourceBytesPerPixel =
                indexed ? 1u : (pixelDepth / 8u);
            if (sourceBytesPerPixel == 0u || sourceBytesPerPixel > 4u ||
                (grayscale && sourceBytesPerPixel != 1u && sourceBytesPerPixel != 2u) ||
                (!grayscale && !indexed && sourceBytesPerPixel != 3u && sourceBytesPerPixel != 4u))
                throw std::runtime_error("Unsupported TGA pixel depth.");
            size_t offset = 18u + idLength;
            const size_t paletteBytes = size_t(colorMapLength) * (colorMapDepth / 8u);
            if (offset + paletteBytes > bytes.size())
                throw std::runtime_error("TGA palette is out of bounds.");
            const size_t paletteOffset = offset;
            offset += paletteBytes;
            const size_t pixelCount = size_t(width) * height;
            eastl::vector<uint8_t> sourcePixels;
            sourcePixels.reserve(pixelCount * sourceBytesPerPixel);
            if (!rle)
            {
                const size_t pixelBytes = pixelCount * sourceBytesPerPixel;
                if (offset + pixelBytes > bytes.size())
                    throw std::runtime_error("TGA raw image is truncated.");
                sourcePixels.insert(
                    sourcePixels.end(), bytes.begin() + offset,
                    bytes.begin() + offset + pixelBytes);
                offset += pixelBytes;
            }
            while (rle && sourcePixels.size() < pixelCount * sourceBytesPerPixel)
            {
                if (offset >= bytes.size())
                    throw std::runtime_error("TGA pixel data is truncated.");
                const uint8_t packet = bytes[offset++];
                const uint32_t runLength = uint32_t(packet & 0x7fu) + 1u;
                if (sourcePixels.size() + size_t(runLength) * sourceBytesPerPixel >
                    pixelCount * sourceBytesPerPixel)
                    throw std::runtime_error("TGA RLE packet exceeds the image extent.");
                if ((packet & 0x80u) != 0u)
                {
                    if (offset + sourceBytesPerPixel > bytes.size())
                        throw std::runtime_error("TGA RLE pixel is truncated.");
                    for (uint32_t repeat = 0u; repeat < runLength; ++repeat)
                        for (uint32_t channel = 0u; channel < sourceBytesPerPixel; ++channel)
                            sourcePixels.push_back(bytes[offset + channel]);
                    offset += sourceBytesPerPixel;
                }
                else
                {
                    const size_t packetBytes = size_t(runLength) * sourceBytesPerPixel;
                    if (offset + packetBytes > bytes.size())
                        throw std::runtime_error("TGA raw packet is truncated.");
                    sourcePixels.insert(
                        sourcePixels.end(), bytes.begin() + offset,
                        bytes.begin() + offset + packetBytes);
                    offset += packetBytes;
                }
            }

            RgbaImageData image;
            image.width = width;
            image.height = height;
            image.pixels.resize(pixelCount * 4u);
            const uint32_t origin = (descriptor & 0x30u) >> 4u;
            for (uint32_t storageY = 0u; storageY < height; ++storageY)
            {
                for (uint32_t storageX = 0u; storageX < width; ++storageX)
                {
                    const uint32_t x = (origin == 1u || origin == 3u)
                        ? width - 1u - storageX
                        : storageX;
                    const uint32_t y = (origin == 0u || origin == 1u)
                        ? height - 1u - storageY
                        : storageY;
                    const size_t sourceOffset =
                        (size_t(storageY) * width + storageX) * sourceBytesPerPixel;
                    uint8_t red = 0u;
                    uint8_t green = 0u;
                    uint8_t blue = 0u;
                    uint8_t alpha = 255u;
                    if (indexed)
                    {
                        // TGALoader indexes its palette from zero and preserves this r185 behavior.
                        const uint32_t paletteIndex = sourcePixels[sourceOffset];
                        if (paletteIndex >= colorMapLength)
                            throw std::runtime_error("TGA palette index is out of bounds.");
                        const size_t palettePixel = paletteOffset + size_t(paletteIndex) * 3u;
                        blue = bytes[palettePixel];
                        green = bytes[palettePixel + 1u];
                        red = bytes[palettePixel + 2u];
                    }
                    else if (grayscale)
                    {
                        red = green = blue = sourcePixels[sourceOffset];
                        alpha = sourceBytesPerPixel == 2u
                            ? sourcePixels[sourceOffset + 1u]
                            : 255u;
                    }
                    else
                    {
                        blue = sourcePixels[sourceOffset];
                        green = sourcePixels[sourceOffset + 1u];
                        red = sourcePixels[sourceOffset + 2u];
                        alpha = sourceBytesPerPixel == 4u
                            ? sourcePixels[sourceOffset + 3u]
                            : 255u;
                    }
                    const size_t destinationOffset =
                        (size_t(y) * width + x) * 4u;
                    image.pixels[destinationOffset] = red;
                    image.pixels[destinationOffset + 1u] = green;
                    image.pixels[destinationOffset + 2u] = blue;
                    image.pixels[destinationOffset + 3u] = alpha;
                }
            }
            return image;
        }

        /** Builds Three r185's default-segment BoxGeometry with flat normals. */
        void buildTgaBoxGeometry(
            eastl::vector<WebglLoaderTextureTgaHostVertex> &outVertices,
            eastl::vector<uint32_t> &outIndices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(
                1.0f, 1.0f, 1.0f, sourceVertices, outIndices);
            outVertices.clear();
            outVertices.reserve(sourceVertices.size());
            for (uint32_t index = 0u; index < sourceVertices.size(); ++index)
            {
                const TexturedBoxHostVertex &source = sourceVertices[index];
                const uint32_t face = index / 4u;
                glm::vec3 a(0.0f);
                glm::vec3 b(0.0f);
                glm::vec3 c(0.0f);
                const uint32_t faceIndex = face * 6u;
                const auto readPosition = [&sourceVertices](uint32_t vertexIndex)
                {
                    const TexturedBoxHostFloat4 &position =
                        sourceVertices[vertexIndex].position;
                    return glm::vec3(position.x, position.y, position.z);
                };
                a = readPosition(outIndices[faceIndex]);
                b = readPosition(outIndices[faceIndex + 1u]);
                c = readPosition(outIndices[faceIndex + 2u]);
                const glm::vec3 normal = glm::normalize(glm::cross(c - b, a - b));
                outVertices.push_back({
                    .position = glm::vec4(
                        source.position.x,
                        source.position.y,
                        source.position.z,
                        1.0f),
                    .normal = glm::vec4(normal, 0.0f),
                    .textureCoordinate = glm::vec4(
                        source.texCoord.x,
                        source.texCoord.y,
                        0.0f,
                        0.0f),
                });
            }
            if (outVertices.size() != 24u || outIndices.size() != 36u)
                throw std::logic_error("TGA BoxGeometry layout diverged.");
        }

        /** Applies the GVM canvas camera-axis convention to a Three view matrix. */
        glm::mat4 makeTgaCameraFlip()
        {
            return glm::mat4(
                glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        /** Computes the fixed-frame OrbitControls camera view. */
        glm::mat4 makeTgaView(bool orbitReplay)
        {
            const glm::vec3 target(0.0f);
            if (!orbitReplay)
            {
                return makeTgaCameraFlip() * glm::lookAt(
                    glm::vec3(0.0f, 1.0f, 5.0f),
                    target,
                    glm::vec3(0.0f, 1.0f, 0.0f));
            }
            constexpr double Pi = 3.14159265358979323846;
            const double radius = std::sqrt(26.0);
            const double initialPhi = std::acos(1.0 / radius);
            const double theta = 2.0 * Pi * 50.0 / 500.0;
            const double phi = initialPhi + (2.0 * Pi * 30.0 / 500.0);
            const glm::vec3 position(
                static_cast<float>(radius * std::sin(phi) * std::sin(theta)),
                static_cast<float>(radius * std::cos(phi)),
                static_cast<float>(radius * std::sin(phi) * std::cos(theta)));
            return makeTgaCameraFlip() * glm::lookAt(
                position, target, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        /** Appends one typed buffer component to an entity allocation. */
        void appendTgaBuffer(
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

        /** Creates parent folders for one requested TGA artifact. */
        void prepareTgaOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglLoaderTextureTgaRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initialLoader =
            options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool canonicalLoader =
            options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool orbitReplay =
            options.scenarioId == "orbit-replay" && options.targetFrame == 1u;
        if (options.caseId != "webgl_loader_texture_tga" ||
            (!initialLoader && !canonicalLoader && !orbitReplay) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "TGA adapter requires its locked scenarios, extent, seed, and assets.");
        }
        hasReplay = orbitReplay;
        if (orbitReplay)
        {
            if (options.inputReplayPath.empty() ||
                calculateTgaSha256(std::filesystem::path(
                    options.inputReplayPath.c_str())) !=
                    "aeeb5017f6d0ba43ea0d9d9045179f99b0405be2838f9493e543177493b45d54")
            {
                throw std::invalid_argument("TGA input replay differs from its lock.");
            }
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only the TGA orbit scenario accepts replay input.");
        }
        device = inDevice;
        buildTgaBoxGeometry(vertices, indices);
        for (uint32_t textureIndex = 0u; textureIndex < TgaCount; ++textureIndex)
        {
            const std::filesystem::path texturePath =
                std::filesystem::path(options.assetRoot.c_str()) /
                "textures" / TgaNames[textureIndex];
            if (calculateTgaSha256(texturePath) != TgaHashes[textureIndex])
                throw std::runtime_error("A TGA asset differs from the locked r185 input.");
            const RgbaImageData decoded = decodeTgaRgba8(texturePath);
            if (decoded.width != TgaExtent || decoded.height != TgaExtent)
                throw std::runtime_error("A decoded TGA extent differs from r185.");
            const eastl::vector<RgbaImageData> mipChain = buildSrgbMipChain(decoded);
            if (mipChain.size() != TgaMipCount)
                throw std::runtime_error("A TGA texture did not produce nine explicit mips.");
            textures[textureIndex].width = decoded.width;
            textures[textureIndex].height = decoded.height;
            for (const RgbaImageData &mip : mipChain)
            {
                textures[textureIndex].mipOffsets.push_back(
                    textures[textureIndex].bytes.size());
                textures[textureIndex].bytes.insert(
                    textures[textureIndex].bytes.end(),
                    mip.pixels.begin(),
                    mip.pixels.end());
            }
        }

        const glm::mat4 view = makeTgaView(orbitReplay);
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 45.0, 0.1, 100.0);
        const glm::vec3 worldLight = glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f));
        const glm::mat4 lightFlip = glm::mat4(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const glm::mat4 lightView = lightFlip * view;
        const glm::vec3 viewLight = glm::normalize(
            glm::vec3(lightView * glm::vec4(worldLight, 0.0f)));
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error("TGA adapter could not create its Scene Set encoder.");
        for (uint32_t entity = 0u; entity < TgaCount; ++entity)
        {
            glm::mat4 model(1.0f);
            model[3u][0u] = BoxPositions[entity];
            const glm::mat4 modelView = view * model;
            const WebglLoaderTextureTgaHostObjectData objectData = {
                .modelView = modelView,
                .normalTransform = glm::transpose(glm::inverse(modelView)),
                .projection = projection,
                .lightDirectionAndIntensity = glm::vec4(viewLight, 2.5f),
                .ambientIntensityAndReserved = glm::vec4(1.5f, 0.0f, 0.0f, 0.0f),
            };
            const WebglLoaderTextureTgaHostInstanceData instanceData = {
                .reserved = glm::vec4(0.0f),
            };
            const WebglLoaderTextureTgaHostMaterialData materialData = {
                .diffuseColor = glm::vec4(1.0f),
                .specularColorAndShininess = glm::vec4(
                    0.0056053917f, 0.0056053917f, 0.0056053917f, 30.0f),
            };
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix =
                "WebglLoaderTextureTgaEntity" + eastl::to_string(entity);
            appendTgaBuffer(
                allocation,
                WebglLoaderTextureTgaSceneRenderSetComponents::vertices,
                prefix + "Vertices",
                vertices.data(),
                vertices.size() * sizeof(vertices[0u]));
            appendTgaBuffer(
                allocation,
                WebglLoaderTextureTgaSceneRenderSetComponents::indices,
                prefix + "Indices",
                indices.data(),
                indices.size() * sizeof(indices[0u]));
            appendTgaBuffer(
                allocation,
                WebglLoaderTextureTgaSceneRenderSetComponents::objects,
                prefix + "Object",
                &objectData,
                sizeof(objectData));
            appendTgaBuffer(
                allocation,
                WebglLoaderTextureTgaSceneRenderSetComponents::instances,
                prefix + "Instance",
                &instanceData,
                sizeof(instanceData));
            appendTgaBuffer(
                allocation,
                WebglLoaderTextureTgaSceneRenderSetComponents::materials,
                prefix + "Material",
                &materialData,
                sizeof(materialData));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglLoaderTextureTgaSceneRenderSetComponents::textures;
            const WebglLoaderTextureTgaTextureData &texture = textures[entity];
            textureComponent.textures.push_back({
                .textureName = prefix + "Texture",
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = texture.width,
                .height = texture.height,
                .data = texture.bytes.data(),
                .dataStorageBytes = texture.bytes.size(),
                .mipmapOffsetBytes = texture.mipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglLoaderTextureTgaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureTgaRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        eastl::vector<uint8_t> rgba(size_t(width) * height * 4u);
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareTgaOutputPath(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write TGA RGBA capture.");
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareTgaOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_loader_texture_tga\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"sampleCount\":1,\n"
                << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                << "  \"inputReplay\":";
            if (hasReplay)
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgl_loader_texture_tga\",\"scenarioId\":\"orbit-replay\",\"captureFrame\":1,\"sha256\":\"aeeb5017f6d0ba43ea0d9d9045179f99b0405be2838f9493e543177493b45d54\",\"target\":\"body > div:nth-of-type(2) > canvas\",\"eventCount\":3,\"lastEventFrame\":0}";
            }
            else output << "null";
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareTgaOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_loader_texture_tga\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderSetType\":\"WebglLoaderTextureTgaSceneRenderSet\",\n"
                << "  \"renderableObjectCount\":2,\n"
                << "  \"entityCount\":2,\n"
                << "  \"instanceCounts\":[1,1],\n"
                << "  \"drawCommandCount\":2,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"textureSlotCount\":2,\n"
                << "  \"sampleCount\":1\n}\n";
        }
        if (!options.semanticSnapshotPath.empty() &&
            options.scenarioId == "canonical-loader")
        {
            prepareTgaOutputPath(options.semanticSnapshotPath);
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_loader_texture_tga\",\n"
                << "  \"scenarioId\":\"canonical-loader\",\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"assetSha256\":[\"" << TgaHashes[0] << "\",\"" << TgaHashes[1] << "\"],\n"
                << "  \"decodedExtent\":[[256,256],[256,256]],\n"
                << "  \"explicitMipCount\":9,\n"
                << "  \"entityCount\":2\n}\n";
        }
        captureWritten = true;
    }

    void WebglLoaderTextureTgaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        for (WebglLoaderTextureTgaTextureData &texture : textures)
        {
            texture.bytes.clear();
            texture.mipOffsets.clear();
        }
    }
} // namespace GVM::ThreeSamples
