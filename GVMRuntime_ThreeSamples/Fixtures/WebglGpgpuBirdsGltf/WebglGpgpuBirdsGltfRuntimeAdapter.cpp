#include "WebglGpgpuBirdsGltfRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TextureWidth = 64u;
        constexpr uint32_t BirdCount = TextureWidth * TextureWidth;
        constexpr uint32_t DefaultVisibleBirdCount = BirdCount / 4u;
        constexpr uint32_t InteractiveVisibleBirdCount = 1536u;
        constexpr uint32_t MaximumGeometryBirdCount = InteractiveVisibleBirdCount;
        constexpr uint32_t RandomSeed = 0x12345678u;
        constexpr uint32_t ModuleBodyRandomDrawCount = 84u;
        constexpr uint32_t BirdGeometryUuidRandomDrawCount = 4u;
        constexpr uint32_t LoaderRandomDrawCount = 24u;
        constexpr uint32_t GeometryTrailingRandomDrawCount = 8u;
        constexpr uint32_t BeforeSimulationRandomDrawCount = 84u;
        constexpr uint32_t AfterSimulationRandomDrawCount = 80u;
        constexpr uint32_t FirstRenderRandomDrawCount = 8u;
        constexpr uint32_t ModuleBodyRandomState = 2517730243u;
        constexpr uint32_t AfterBirdGeometryUuidRandomState = 1313233274u;
        constexpr uint32_t AfterModelSelectionRandomState = 675422734u;
        constexpr uint32_t LoaderCallbackRandomState = 2335126527u;
        constexpr uint32_t AfterGeometryRandomState = 1993363196u;
        constexpr uint32_t BeforePositionRandomState = 1625089641u;
        constexpr uint32_t AfterPositionRandomState = 2897477130u;
        constexpr uint32_t AfterVelocityRandomState = 3776981558u;
        constexpr uint32_t AfterInitRandomState = 3873058599u;
        constexpr uint32_t FinalReferenceRandomState = 3047991959u;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr char ParrotSha256[] =
            "adcc5dae04d0b16957ce20b7bebb84d41c3bff1c01bf5ab1cf56d06345bc5170";
        constexpr char FlamingoSha256[] =
            "9eabeb58587e4e0d11d5e23b30a26b73e4b8e6e8c83f2c36b3c5c3fe04dcf19f";
        constexpr char InteractiveReplaySha256[] =
            "9207082c832c3ebb2f86a87cc14a2fe71f940847f4d8b458240197c8ff2ac333";
        constexpr uint32_t GlbMagic = 0x46546c67u;
        constexpr uint32_t GlbVersion = 2u;
        constexpr uint32_t JsonChunkType = 0x4e4f534au;
        constexpr uint32_t BinaryChunkType = 0x004e4942u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(float4) == 16u);
        static_assert(sizeof(WebglGpgpuBirdsGltfVertex) == 64u);
        static_assert(sizeof(WebglGpgpuBirdsGltfSimulationState) == 32u);
        static_assert(sizeof(GpgpuBirdsGltfHostFloat4) == 16u);
        static_assert(sizeof(GpgpuBirdsGltfHostUint4) == 16u);
        static_assert(sizeof(GpgpuBirdsGltfHostObjectData) == 160u);
        static_assert(sizeof(GpgpuBirdsGltfHostInstanceData) == 16u);
        static_assert(sizeof(GpgpuBirdsGltfHostMaterialData) == 32u);

        /** Stores one validated GLB JSON document and its embedded binary chunk. */
        struct DecodedGlbContainer
        {
            nlohmann::json document;
            eastl::vector<uint8_t> binaryChunk;
            eastl::string sourceSha256;
        };

        /** Creates parent directories for one explicitly requested output path. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Returns one lowercase SHA-256 digest for a bounded host byte range. */
        eastl::string calculateSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_gpgpu_birds_gltf SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Reads one required binary artifact without implicit path fallback. */
        eastl::vector<uint8_t> readRequiredBytes(
            const std::filesystem::path &filePath,
            const char *label)
        {
            std::ifstream input(filePath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    (eastl::string("Could not open ") + label + ": " +
                     filePath.string().c_str())
                        .c_str());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error(
                    (eastl::string(label) + " has an invalid size.").c_str());
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                throw std::runtime_error(
                    (eastl::string("Could not read complete ") + label + ".")
                        .c_str());
            }
            return bytes;
        }

        /** Reads one little-endian uint32 from a validated byte range. */
        uint32_t readUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset + sizeof(uint32_t) > bytes.size())
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds_gltf encountered a truncated uint32.");
            }
            uint32_t value = 0u;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        /** Decodes and validates a self-contained GLB container. */
        DecodedGlbContainer decodeGlbContainer(
            const std::filesystem::path &filePath,
            const char *expectedSha256)
        {
            const eastl::vector<uint8_t> bytes =
                readRequiredBytes(filePath, "locked bird GLB");
            DecodedGlbContainer result;
            result.sourceSha256 = calculateSha256(bytes.data(), bytes.size());
            if (result.sourceSha256 != expectedSha256)
            {
                throw std::runtime_error(
                    "A bird GLB SHA-256 diverged from the r185 asset lock.");
            }
            if (bytes.size() < 28u || readUint32(bytes, 0u) != GlbMagic ||
                readUint32(bytes, 4u) != GlbVersion ||
                readUint32(bytes, 8u) != bytes.size())
            {
                throw std::runtime_error(
                    "A locked bird GLB has an invalid version-2 header.");
            }
            const uint32_t jsonLength = readUint32(bytes, 12u);
            if (readUint32(bytes, 16u) != JsonChunkType ||
                20u + uint64_t(jsonLength) + 8u > bytes.size())
            {
                throw std::runtime_error(
                    "A locked bird GLB has an invalid JSON chunk.");
            }
            const char *jsonBegin =
                reinterpret_cast<const char *>(bytes.data() + 20u);
            result.document = nlohmann::json::parse(
                jsonBegin,
                jsonBegin + jsonLength);
            const size_t binaryHeaderOffset = 20u + jsonLength;
            const uint32_t binaryLength =
                readUint32(bytes, binaryHeaderOffset);
            if (readUint32(bytes, binaryHeaderOffset + 4u) != BinaryChunkType ||
                binaryHeaderOffset + 8u + uint64_t(binaryLength) !=
                    bytes.size())
            {
                throw std::runtime_error(
                    "A locked bird GLB has an invalid embedded binary chunk.");
            }
            result.binaryChunk.resize(binaryLength);
            std::memcpy(
                result.binaryChunk.data(),
                bytes.data() + binaryHeaderOffset + 8u,
                binaryLength);
            return result;
        }

        /** Returns the scalar component count for one supported glTF accessor type. */
        uint32_t accessorComponentCount(const std::string &type)
        {
            if (type == "SCALAR") return 1u;
            if (type == "VEC2") return 2u;
            if (type == "VEC3") return 3u;
            if (type == "VEC4") return 4u;
            throw std::runtime_error(
                "A bird GLB accessor uses an unsupported type.");
        }

        /** Reads one tightly or strided Float32 accessor into an EASTL vector. */
        eastl::vector<float> readFloatAccessor(
            const DecodedGlbContainer &container,
            uint32_t accessorIndex,
            uint32_t expectedComponentCount)
        {
            const auto &accessor =
                container.document.at("accessors").at(accessorIndex);
            if (accessor.at("componentType").get<uint32_t>() != 5126u ||
                accessor.contains("sparse"))
            {
                throw std::runtime_error(
                    "A bird GLB Float32 accessor uses an unsupported encoding.");
            }
            const uint32_t componentCount =
                accessorComponentCount(accessor.at("type").get<std::string>());
            if (componentCount != expectedComponentCount)
            {
                throw std::runtime_error(
                    "A bird GLB accessor component count diverged from its semantic.");
            }
            const uint32_t count = accessor.at("count").get<uint32_t>();
            const auto &view = container.document.at("bufferViews").at(
                accessor.at("bufferView").get<uint32_t>());
            const uint64_t baseOffset =
                uint64_t(view.value("byteOffset", 0u)) +
                uint64_t(accessor.value("byteOffset", 0u));
            const uint64_t packedStride =
                uint64_t(componentCount) * sizeof(float);
            const uint64_t stride = view.value(
                "byteStride",
                static_cast<uint32_t>(packedStride));
            if (stride < packedStride ||
                (count != 0u &&
                 baseOffset + uint64_t(count - 1u) * stride + packedStride >
                     container.binaryChunk.size()))
            {
                throw std::runtime_error(
                    "A bird GLB Float32 accessor exceeds its binary chunk.");
            }
            eastl::vector<float> result;
            result.resize(size_t(count) * componentCount);
            for (uint32_t element = 0u; element < count; ++element)
            {
                std::memcpy(
                    result.data() + size_t(element) * componentCount,
                    container.binaryChunk.data() +
                        baseOffset + uint64_t(element) * stride,
                    static_cast<size_t>(packedStride));
            }
            return result;
        }

        /** Reads one unsigned scalar index accessor into canonical uint32 values. */
        eastl::vector<uint32_t> readIndexAccessor(
            const DecodedGlbContainer &container,
            uint32_t accessorIndex)
        {
            const auto &accessor =
                container.document.at("accessors").at(accessorIndex);
            if (accessor.at("type").get<std::string>() != "SCALAR" ||
                accessor.contains("sparse"))
            {
                throw std::runtime_error(
                    "A bird GLB index accessor uses an unsupported encoding.");
            }
            const uint32_t componentType =
                accessor.at("componentType").get<uint32_t>();
            const uint32_t componentBytes = componentType == 5123u
                                                ? 2u
                                                : componentType == 5125u ? 4u : 0u;
            if (componentBytes == 0u)
            {
                throw std::runtime_error(
                    "A bird GLB index accessor is not Uint16 or Uint32.");
            }
            const uint32_t count = accessor.at("count").get<uint32_t>();
            const auto &view = container.document.at("bufferViews").at(
                accessor.at("bufferView").get<uint32_t>());
            const uint64_t baseOffset =
                uint64_t(view.value("byteOffset", 0u)) +
                uint64_t(accessor.value("byteOffset", 0u));
            const uint64_t stride =
                view.value("byteStride", componentBytes);
            if (stride < componentBytes ||
                (count != 0u &&
                 baseOffset + uint64_t(count - 1u) * stride + componentBytes >
                     container.binaryChunk.size()))
            {
                throw std::runtime_error(
                    "A bird GLB index accessor exceeds its binary chunk.");
            }
            eastl::vector<uint32_t> result;
            result.resize(count);
            for (uint32_t element = 0u; element < count; ++element)
            {
                const uint8_t *source = container.binaryChunk.data() +
                    baseOffset + uint64_t(element) * stride;
                if (componentType == 5123u)
                {
                    uint16_t value = 0u;
                    std::memcpy(&value, source, sizeof(value));
                    result[element] = value;
                }
                else
                {
                    std::memcpy(&result[element], source, sizeof(uint32_t));
                }
            }
            return result;
        }

        /** Returns the smallest power of two greater than or equal to one value. */
        uint32_t nextPowerOfTwo(uint32_t value)
        {
            if (value == 0u)
            {
                throw std::invalid_argument(
                    "webgl_gpgpu_birds_gltf requires a positive texture extent.");
            }
            uint32_t result = 1u;
            while (result < value)
            {
                if (result > std::numeric_limits<uint32_t>::max() / 2u)
                {
                    throw std::overflow_error(
                        "webgl_gpgpu_birds_gltf power-of-two extent overflowed.");
                }
                result *= 2u;
            }
            return result;
        }

        /** Decodes one locked bird GLB and bakes its exact nearest morph payload. */
        GpgpuBirdsGltfAsset loadBirdAsset(
            const std::filesystem::path &filePath,
            const char *name,
            const char *expectedSha256,
            uint32_t expectedVertexCount,
            uint32_t expectedIndexCount,
            uint32_t expectedMorphTargetCount,
            uint32_t expectedDurationFrames)
        {
            const DecodedGlbContainer container =
                decodeGlbContainer(filePath, expectedSha256);
            if (container.document.at("scenes").size() != 1u ||
                container.document.at("nodes").size() != 1u ||
                container.document.at("meshes").size() != 1u ||
                container.document.at("materials").size() != 1u ||
                container.document.at("buffers").size() != 1u)
            {
                throw std::runtime_error(
                    "A bird GLB no longer has the locked single-primitive graph.");
            }
            const auto &mesh = container.document.at("meshes").at(0u);
            if (mesh.at("primitives").size() != 1u)
            {
                throw std::runtime_error(
                    "A bird GLB no longer contains exactly one primitive.");
            }
            const auto &primitive = mesh.at("primitives").at(0u);
            if (primitive.value("mode", 4u) != 4u ||
                primitive.contains("extensions"))
            {
                throw std::runtime_error(
                    "A bird GLB primitive requires unsupported topology or extensions.");
            }
            const auto &attributes = primitive.at("attributes");
            GpgpuBirdsGltfAsset asset;
            asset.name = name;
            asset.sourceSha256 = container.sourceSha256;
            asset.positions = readFloatAccessor(
                container,
                attributes.at("POSITION").get<uint32_t>(),
                3u);
            asset.colors = readFloatAccessor(
                container,
                attributes.at("COLOR_0").get<uint32_t>(),
                3u);
            asset.indices = readIndexAccessor(
                container,
                primitive.at("indices").get<uint32_t>());
            asset.vertexCount =
                static_cast<uint32_t>(asset.positions.size() / 3u);
            asset.indexCount = static_cast<uint32_t>(asset.indices.size());
            for (const auto &target : primitive.at("targets"))
            {
                asset.morphTargets.push_back(readFloatAccessor(
                    container,
                    target.at("POSITION").get<uint32_t>(),
                    3u));
            }
            asset.morphTargetCount =
                static_cast<uint32_t>(asset.morphTargets.size());
            const auto &animation =
                container.document.at("animations").at(0u);
            const uint32_t timeAccessorIndex = animation.at("samplers")
                                                   .at(0u)
                                                   .at("input")
                                                   .get<uint32_t>();
            const float durationSeconds = container.document.at("accessors")
                                              .at(timeAccessorIndex)
                                              .at("max")
                                              .at(0u)
                                              .get<float>();
            asset.animationDurationFrames = static_cast<uint32_t>(
                std::round(double(durationSeconds) * 60.0));
            if (asset.vertexCount != expectedVertexCount ||
                asset.indexCount != expectedIndexCount ||
                asset.morphTargetCount != expectedMorphTargetCount ||
                asset.animationDurationFrames != expectedDurationFrames ||
                asset.colors.size() != asset.positions.size())
            {
                throw std::runtime_error(
                    "A bird GLB semantic count diverged from the r185 lock.");
            }
            for (const uint32_t index : asset.indices)
            {
                if (index >= asset.vertexCount)
                {
                    throw std::runtime_error(
                        "A bird GLB index exceeds its vertex range.");
                }
            }
            asset.animationTextureWidth = nextPowerOfTwo(asset.vertexCount);
            asset.animationTextureHeight =
                nextPowerOfTwo(asset.animationDurationFrames);
            asset.animationTexture.resize(
                size_t(asset.animationTextureWidth) *
                asset.animationTextureHeight,
                float4(0.0f));
            for (uint32_t y = 0u;
                 y < asset.animationTextureHeight;
                 ++y)
            {
                const double morphPosition =
                    double(y) / double(asset.animationDurationFrames) *
                    double(asset.morphTargetCount);
                const uint32_t currentMorph =
                    static_cast<uint32_t>(std::floor(morphPosition)) %
                    asset.morphTargetCount;
                const uint32_t nextMorph =
                    (currentMorph + 1u) % asset.morphTargetCount;
                const double amount =
                    morphPosition - std::floor(morphPosition);
                const double inverseAmount = 1.0 - amount;
                for (uint32_t x = 0u;
                     x < asset.animationTextureWidth;
                     ++x)
                {
                    float4 value(0.0f);
                    if (y < asset.animationDurationFrames)
                    {
                        if (x < asset.vertexCount)
                        {
                            const size_t source = size_t(x) * 3u;
                            const auto &first =
                                asset.morphTargets[currentMorph];
                            const auto &second = asset.morphTargets[nextMorph];
                            const double firstX =
                                inverseAmount * double(first[source]);
                            const double secondX =
                                amount * double(second[source]);
                            const double firstY =
                                inverseAmount * double(first[source + 1u]);
                            const double secondY =
                                amount * double(second[source + 1u]);
                            const double firstZ =
                                inverseAmount * double(first[source + 2u]);
                            const double secondZ =
                                amount * double(second[source + 2u]);
                            value.x = static_cast<float>(firstX + secondX);
                            value.y = static_cast<float>(firstY + secondY);
                            value.z = static_cast<float>(firstZ + secondZ);
                        }
                        value.w = 1.0f;
                    }
                    asset.animationTexture[
                        size_t(y) * asset.animationTextureWidth + x] = value;
                }
            }
            asset.morphTextureSha256 = calculateSha256(
                asset.animationTexture.data(),
                asset.animationTexture.size() * sizeof(float4));
            return asset;
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript double. */
        double nextRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Consumes an exact draw count and validates the resulting reference state. */
        void consumeRandomDraws(
            ThreeCompat::DeterministicRandom &random,
            uint32_t drawCount,
            uint32_t expectedState,
            const char *stage)
        {
            for (uint32_t draw = 0u; draw < drawCount; ++draw)
            {
                (void)nextRandomUnit(random);
            }
            if (random.getState() != expectedState)
            {
                throw std::runtime_error(
                    (eastl::string("Deterministic random state diverged at ") +
                     stage + ".")
                        .c_str());
            }
        }

        /** Builds the physically batched Parrot prefix while consuming every upstream seed draw. */
        void buildPhysicalGeometry(
            const GpgpuBirdsGltfAsset &asset,
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<WebglGpgpuBirdsGltfVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.reserve(
                size_t(asset.vertexCount) * MaximumGeometryBirdCount);
            (void)nextRandomUnit(random);
            double birdRandom = 0.0;
            for (uint32_t bird = 0u; bird < BirdCount; ++bird)
            {
                for (uint32_t vertexIndex = 0u;
                     vertexIndex < asset.vertexCount;
                     ++vertexIndex)
                {
                    if (vertexIndex == 0u)
                    {
                        birdRandom = nextRandomUnit(random);
                    }
                    const double seedZ = nextRandomUnit(random);
                    const double seedW = nextRandomUnit(random);
                    if (bird >= MaximumGeometryBirdCount)
                    {
                        continue;
                    }
                    const size_t source = size_t(vertexIndex) * 3u;
                    WebglGpgpuBirdsGltfVertex vertex;
                    vertex.position = float4(
                        asset.positions[source],
                        asset.positions[source + 1u],
                        asset.positions[source + 2u],
                        1.0f);
                    vertex.color = float4(
                        asset.colors[source],
                        asset.colors[source + 1u],
                        asset.colors[source + 2u],
                        1.0f);
                    vertex.animationReference = float4(
                        float(vertexIndex) /
                            float(asset.animationTextureWidth),
                        float(asset.animationDurationFrames) /
                            float(asset.animationTextureHeight),
                        float(bird % TextureWidth) / float(TextureWidth),
                        float(bird / TextureWidth) / float(TextureWidth));
                    vertex.seeds = float4(
                        float(bird),
                        static_cast<float>(birdRandom),
                        static_cast<float>(seedZ),
                        static_cast<float>(seedW));
                    vertices.push_back(vertex);
                }
            }
            consumeRandomDraws(
                random,
                GeometryTrailingRandomDrawCount,
                AfterGeometryRandomState,
                "batched geometry completion");

            indices.reserve(
                size_t(asset.indexCount) * MaximumGeometryBirdCount);
            for (uint32_t bird = 0u;
                 bird < MaximumGeometryBirdCount;
                 ++bird)
            {
                const uint32_t vertexOffset = bird * asset.vertexCount;
                for (const uint32_t index : asset.indices)
                {
                    indices.push_back(index + vertexOffset);
                }
            }
        }

        /** Builds exact initial RGBA32Float position and velocity payloads. */
        void buildInitialSimulation(
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<float4> &position,
            eastl::vector<float4> &velocity)
        {
            position.resize(BirdCount);
            for (float4 &value : position)
            {
                value = float4(
                    static_cast<float>(nextRandomUnit(random) * 800.0 - 400.0),
                    static_cast<float>(nextRandomUnit(random) * 800.0 - 400.0),
                    static_cast<float>(nextRandomUnit(random) * 800.0 - 400.0),
                    1.0f);
            }
            if (random.getState() != AfterPositionRandomState)
            {
                throw std::runtime_error(
                    "Initial position random state diverged from the r185 probe.");
            }
            velocity.resize(BirdCount);
            for (float4 &value : velocity)
            {
                value = float4(
                    static_cast<float>((nextRandomUnit(random) - 0.5) * 10.0),
                    static_cast<float>((nextRandomUnit(random) - 0.5) * 10.0),
                    static_cast<float>((nextRandomUnit(random) - 0.5) * 10.0),
                    1.0f);
            }
            if (random.getState() != AfterVelocityRandomState)
            {
                throw std::runtime_error(
                    "Initial velocity random state diverged from the r185 probe.");
            }
        }

        /** Builds the fixed r185 perspective and translation-only view matrices. */
        void buildCameraMatrices(
            glm::mat4 &projectionMatrix,
            glm::mat4 &viewMatrix)
        {
            const double nearDistance = 1.0;
            const double farDistance = 3000.0;
            const double top = nearDistance *
                               std::tan(75.0 * Pi / 180.0 * 0.5);
            const double height = top * 2.0;
            const double width = height * 1.6;
            projectionMatrix = glm::mat4(0.0f);
            projectionMatrix[0u][0u] = static_cast<float>(
                2.0 * nearDistance / width);
            projectionMatrix[1u][1u] = static_cast<float>(
                2.0 * nearDistance / height);
            projectionMatrix[2u][2u] = static_cast<float>(
                -(farDistance + nearDistance) /
                (farDistance - nearDistance));
            projectionMatrix[2u][3u] = -1.0f;
            projectionMatrix[3u][2u] = static_cast<float>(
                -(2.0 * farDistance * nearDistance) /
                (farDistance - nearDistance));
            viewMatrix = glm::mat4(1.0f);
            viewMatrix[3u][2u] = -350.0f;
        }

        /** Appends one raw typed payload to a RenderSet entity allocation. */
        void appendBufferPayload(
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

        /** Computes one tightly packed final RGBA8 capture byte count. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * height;
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_gpgpu_birds_gltf RGBA8 size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglGpgpuBirdsGltfRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_gpgpu_birds_gltf")
        {
            throw std::invalid_argument(
                "WebglGpgpuBirdsGltf requires its exact case-id.");
        }
        if (options.width != 800u || options.height != 500u ||
            options.randomSeed != RandomSeed)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirdsGltf requires 800x500 and seed 0x12345678.");
        }
        const bool initialScenario =
            options.scenarioId == "initial-parrot-visible";
        const bool fixedScenario =
            options.scenarioId == "fixed-parrot-flock";
        interactiveScenario =
            options.scenarioId == "count-size-pointer";
        if (!initialScenario && !fixedScenario && !interactiveScenario)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirdsGltf received an unknown scenario-id.");
        }
        const uint32_t expectedFrame = initialScenario ? 1u : 120u;
        if (options.targetFrame != expectedFrame ||
            options.frameCount != expectedFrame + 1u)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirdsGltf frame count diverged from its scenario lock.");
        }
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "WebglGpgpuBirdsGltf requires explicit --asset-root.");
        }
        if (interactiveScenario)
        {
            const eastl::vector<uint8_t> replay = readRequiredBytes(
                std::filesystem::path(options.inputReplayPath.c_str()),
                "birds_gltf replay");
            inputReplaySha256 =
                calculateSha256(replay.data(), replay.size());
            if (inputReplaySha256 != InteractiveReplaySha256)
            {
                throw std::runtime_error(
                    "birds_gltf replay diverged from the captured Oracle lock.");
            }
            const eastl::vector<uint8_t> canonicalState = readRequiredBytes(
                std::filesystem::path(options.canonicalStatePath.c_str()),
                "birds_gltf canonical state");
            canonicalStateSha256 = calculateSha256(
                canonicalState.data(),
                canonicalState.size());
        }
        else if (!options.inputReplayPath.empty() ||
                 !options.canonicalStatePath.empty())
        {
            throw std::invalid_argument(
                "Default birds_gltf scenarios must not receive replay inputs.");
        }

        device = inDevice;
        const std::filesystem::path gltfRoot =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf";
        parrotAsset = loadBirdAsset(
            gltfRoot / "Parrot.glb",
            "Parrot",
            ParrotSha256,
            497u,
            1878u,
            12u,
            72u);
        flamingoAsset = loadBirdAsset(
            gltfRoot / "Flamingo.glb",
            "Flamingo",
            FlamingoSha256,
            337u,
            1626u,
            14u,
            84u);
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        consumeRandomDraws(
            random,
            ModuleBodyRandomDrawCount,
            ModuleBodyRandomState,
            "module body");
        consumeRandomDraws(
            random,
            BirdGeometryUuidRandomDrawCount,
            AfterBirdGeometryUuidRandomState,
            "BirdGeometry UUID");
        const uint32_t selectedModel = static_cast<uint32_t>(
            std::floor(nextRandomUnit(random) * 2.0));
        if (selectedModel != 0u ||
            random.getState() != AfterModelSelectionRandomState)
        {
            throw std::runtime_error(
                "The deterministic r185 stream no longer selects Parrot.");
        }
        consumeRandomDraws(
            random,
            LoaderRandomDrawCount,
            LoaderCallbackRandomState,
            "GLTFLoader callback");
        buildPhysicalGeometry(
            parrotAsset,
            random,
            maximumVertices,
            maximumIndices);
        consumeRandomDraws(
            random,
            BeforeSimulationRandomDrawCount,
            BeforePositionRandomState,
            "simulation initialization");
        buildInitialSimulation(random, initialPosition, initialVelocity);
        consumeRandomDraws(
            random,
            AfterSimulationRandomDrawCount,
            AfterInitRandomState,
            "init completion");
        consumeRandomDraws(
            random,
            FirstRenderRandomDrawCount,
            FinalReferenceRandomState,
            "first compiled render");

        initialPositionSha256 = calculateSha256(
            initialPosition.data(),
            initialPosition.size() * sizeof(float4));
        initialVelocitySha256 = calculateSha256(
            initialVelocity.data(),
            initialVelocity.size() * sizeof(float4));
        geometrySha256 = calculateSha256(
            maximumVertices.data(),
            maximumVertices.size() * sizeof(WebglGpgpuBirdsGltfVertex));
        simulationStates.resize(BirdCount);
        for (uint32_t bird = 0u; bird < BirdCount; ++bird)
        {
            simulationStates[bird].positionAndPhase = initialPosition[bird];
            simulationStates[bird].velocityAndReserved = initialVelocity[bird];
        }
        buildCameraMatrices(projectionMatrix, viewMatrix);
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRendererImpl &renderer,
        uint32_t birdCount,
        float size,
        bool replaceExisting)
    {
        if (birdCount == 0u || birdCount > MaximumGeometryBirdCount)
        {
            throw std::invalid_argument(
                "birds_gltf visible count exceeds the prepared geometry prefix.");
        }
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "birds_gltf could not create its Scene RenderSet encoder.");
        }
        if (replaceExisting)
        {
            if (entityState.entityIndex == UINT32_MAX)
            {
                throw std::logic_error(
                    "birds_gltf cannot replace an unallocated entity.");
            }
            entityState.removedEntityIndex = entityState.entityIndex;
            encoder->removeEntity(entityState.entityIndex);
        }

        GpgpuBirdsGltfHostObjectData objectData = {};
        objectData.projectionMatrix = projectionMatrix;
        objectData.viewMatrix = viewMatrix;
        objectData.cameraPositionAndTime = {0.0f, 0.0f, 350.0f, 0.0f};
        objectData.sizeAndFogRange = {size, 100.0f, 1000.0f, 0.0f};
        const GpgpuBirdsGltfHostInstanceData instanceData = {
            .translationAndScale = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        const GpgpuBirdsGltfHostMaterialData materialData = {
            .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
            .modelAndReserved = {0u, 0u, 0u, 0u},
        };
        const uint32_t vertexCount = parrotAsset.vertexCount * birdCount;
        const uint32_t indexCount = parrotAsset.indexCount * birdCount;
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = vertexCount;
        allocation.indicesCount = indexCount;
        allocation.instanceCount = 1u;
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::vertices,
            "BirdsGltfVertices",
            maximumVertices.data(),
            uint64_t(vertexCount) * sizeof(WebglGpgpuBirdsGltfVertex),
            1u);
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::indices,
            "BirdsGltfIndices",
            maximumIndices.data(),
            uint64_t(indexCount) * sizeof(uint32_t),
            1u);
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::objects,
            "BirdsGltfObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::instances,
            "BirdsGltfInstance",
            &instanceData,
            sizeof(instanceData),
            1u);
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::materials,
            "BirdsGltfMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        appendBufferPayload(
            allocation,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::simulationStates,
            "BirdsGltfSimulationStates",
            simulationStates.data(),
            uint64_t(simulationStates.size()) *
                sizeof(WebglGpgpuBirdsGltfSimulationState),
            BirdCount);
        GVM::Core::RenderSetTextureComponentAllocInfo animationTextures;
        animationTextures.textureComponentHandle =
            WebglGpgpuBirdsGltfSceneRenderSetComponents::animationTextures;
        animationTextures.textures.push_back({
            .textureName = "BirdsGltfParrotMorphAnimation",
            .format = GVM::RHI::TextureFormat::RGBA32Float,
            .width = parrotAsset.animationTextureWidth,
            .height = parrotAsset.animationTextureHeight,
            .data = parrotAsset.animationTexture.data(),
            .dataStorageBytes =
                uint64_t(parrotAsset.animationTexture.size()) * sizeof(float4),
            .mipmapOffsetBytes = {0u},
        });
        allocation.textureInfos.push_back(eastl::move(animationTextures));
        entityState.entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        entityState.birdCount = birdCount;
        entityState.size = size;
        entityState.reallocated = replaceExisting;
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::updateEntityComponents(
        GVM::Core::AbstractRendererImpl &renderer,
        uint32_t frameIndex)
    {
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "birds_gltf could not create its component update encoder.");
        }
        GpgpuBirdsGltfHostObjectData objectData = {};
        objectData.projectionMatrix = projectionMatrix;
        objectData.viewMatrix = viewMatrix;
        objectData.cameraPositionAndTime = {
            0.0f,
            0.0f,
            350.0f,
            static_cast<float>(
                double(frameIndex) * FrameStepMilliseconds / 1000.0),
        };
        objectData.sizeAndFogRange = {
            entityState.size,
            100.0f,
            1000.0f,
            0.0f,
        };
        encoder->setBufferComponentData(
            entityState.entityIndex,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::objects,
            &objectData,
            sizeof(objectData),
            0u,
            1u);
        encoder->setBufferComponentData(
            entityState.entityIndex,
            WebglGpgpuBirdsGltfSceneRenderSetComponents::simulationStates,
            simulationStates.data(),
            uint64_t(simulationStates.size()) *
                sizeof(WebglGpgpuBirdsGltfSimulationState),
            0u,
            BirdCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        if (frameIndex != processedFrameCount)
        {
            throw std::logic_error(
                "birds_gltf callbacks must advance sequentially.");
        }
        if (!simulationStepper || !positionTextureProvider ||
            !velocityTextureProvider || !simulationStepProvider)
        {
            throw std::logic_error(
                "birds_gltf generated simulation callbacks are unavailable.");
        }
        if (interactiveScenario && frameIndex == 0u)
        {
            allocateEntity(
                renderer,
                InteractiveVisibleBirdCount,
                0.26f,
                true);
        }

        WebglGpgpuBirdsGltfSimulationUniforms uniforms;
        uniforms.deltaAndTime = float4(
            frameIndex == 0u
                ? 0.0f
                : static_cast<float>(FrameStepMilliseconds / 1000.0),
            static_cast<float>(double(frameIndex) * FrameStepMilliseconds),
            0.0f,
            0.0f);
        uniforms.distancesAndFreedom =
            float4(20.0f, 20.0f, 20.0f, 0.75f);
        if (frameIndex == 0u)
        {
            uniforms.predator = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        else if (interactiveScenario && frameIndex == 60u)
        {
            uniforms.predator = float4(-0.25f, 0.0f, 0.0f, 0.0f);
        }
        else
        {
            uniforms.predator = float4(12.5f, -20.0f, 0.0f, 0.0f);
        }
        simulationStepper(uniforms);
        if (simulationStepProvider() != frameIndex + 1u)
        {
            throw std::runtime_error(
                "birds_gltf generated and host simulation counts diverged.");
        }
        eastl::vector<float4> positions(BirdCount);
        eastl::vector<float4> velocities(BirdCount);
        device->graphicsQueue(0)
            ->readTexture(
                positionTextureProvider(),
                positions.data(),
                positions.size() * sizeof(float4))
            ->readTexture(
                velocityTextureProvider(),
                velocities.data(),
                velocities.size() * sizeof(float4))
            ->submit();
        for (uint32_t bird = 0u; bird < BirdCount; ++bird)
        {
            simulationStates[bird].positionAndPhase = positions[bird];
            simulationStates[bird].velocityAndReserved = velocities[bird];
        }
        currentPositionSha256 = calculateSha256(
            positions.data(),
            positions.size() * sizeof(float4));
        currentVelocitySha256 = calculateSha256(
            velocities.data(),
            velocities.size() * sizeof(float4));
        updateEntityComponents(renderer, frameIndex);
        ++processedFrameCount;
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "birds_gltf capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(
            options,
            frameIndex,
            width,
            height,
            byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        simulationStepper = nullptr;
        positionTextureProvider = nullptr;
        velocityTextureProvider = nullptr;
        simulationStepProvider = nullptr;
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete birds_gltf RGBA output.");
        }
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open birds_gltf metadata output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_gpgpu_birds_gltf\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str()
               << "\",\n"
               << "  \"backend\": \""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << FinalReferenceRandomState
               << ",\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u
               << ",\n"
               << "  \"simulationRenderPassCount\": "
               << uint64_t(frameIndex + 1u) * 2u << ",\n"
               << "  \"simulationReadbackCount\": " << frameIndex + 1u
               << ",\n"
               << "  \"currentPositionSha256\": \""
               << currentPositionSha256.c_str() << "\",\n"
               << "  \"currentVelocitySha256\": \""
               << currentVelocitySha256.c_str() << "\",\n"
               << "  \"inputReplay\": ";
        if (!interactiveScenario)
        {
            output << "null,\n";
        }
        else
        {
            output << "{\n"
                   << "    \"sha256\": \"" << inputReplaySha256.c_str()
                   << "\",\n"
                   << "    \"caseId\": \"webgl_gpgpu_birds_gltf\",\n"
                   << "    \"scenarioId\": \"count-size-pointer\",\n"
                   << "    \"captureFrame\": 120,\n"
                   << "    \"eventCount\": 1,\n"
                   << "    \"target\": \"body > div:nth-of-type(2) > canvas\"\n"
                   << "  },\n";
        }
        output << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u
               << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglGpgpuBirdsGltfRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open birds_gltf Scene snapshot output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_gpgpu_birds_gltf\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"upstreamCommit\": \"2431a09f46f34c560bc8e44b33be0e567723d5b9\",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderSetRuntimeInstanceCount\": 1,\n"
               << "  \"entityCount\": 1,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"entityInstanceCounts\": [1],\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"birdCount\": " << entityState.birdCount << ",\n"
               << "  \"vertexCount\": "
               << uint64_t(parrotAsset.vertexCount) * entityState.birdCount
               << ",\n"
               << "  \"indexCount\": "
               << uint64_t(parrotAsset.indexCount) * entityState.birdCount
               << ",\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"scenePassSequence\": ["
                  "{\"sceneRoot\":\"scene\",\"scenePass\":"
                  "\"main-standard-morph-flock\"}],\n"
               << "  \"renderSetType\": \"WebglGpgpuBirdsGltfSceneRenderSet\",\n"
               << "  \"componentSchema\": [\"vertices\",\"indices\","
                  "\"objects\",\"instances\",\"materials\","
                  "\"simulationStates\",\"animationTextures\"],\n"
               << "  \"simulationStateCount\": 4096,\n"
               << "  \"simulationBridge\": \"dsl-rgba32float-readback-to-buffer-component\",\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u
               << ",\n"
               << "  \"simulationRenderPassesPerStep\": 2,\n"
               << "  \"fixedNeighborIterationCountPerBird\": 4096,\n"
               << "  \"renderSetReallocated\": "
               << (entityState.reallocated ? "true" : "false") << ",\n"
               << "  \"removedEntity\": ";
        if (entityState.removedEntityIndex == UINT32_MAX)
        {
            output << "null,\n";
        }
        else
        {
            output << entityState.removedEntityIndex << ",\n";
        }
        output << "  \"currentEntity\": " << entityState.entityIndex
               << ",\n"
               << "  \"assetSnapshot\": {\n"
               << "    \"Parrot\": {\"sha256\":\""
               << parrotAsset.sourceSha256.c_str()
               << "\",\"vertices\":497,\"indices\":1878,"
                  "\"morphTargets\":12,\"durationFrames\":72},\n"
               << "    \"Flamingo\": {\"sha256\":\""
               << flamingoAsset.sourceSha256.c_str()
               << "\",\"vertices\":337,\"indices\":1626,"
                  "\"morphTargets\":14,\"durationFrames\":84}\n"
               << "  },\n"
               << "  \"parrotMorphTextureSha256\": \""
               << parrotAsset.morphTextureSha256.c_str() << "\",\n"
               << "  \"flamingoMorphTextureSha256\": \""
               << flamingoAsset.morphTextureSha256.c_str() << "\",\n"
               << "  \"initialPositionSha256\": \""
               << initialPositionSha256.c_str() << "\",\n"
               << "  \"initialVelocitySha256\": \""
               << initialVelocitySha256.c_str() << "\",\n"
               << "  \"geometrySha256\": \""
               << geometrySha256.c_str() << "\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sampleCppGpuCommands\": [\"readTexture\","
                  "\"setBufferComponentData\"]\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
