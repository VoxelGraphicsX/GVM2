#include "DracoAsset.hpp"

#include <draco/attributes/geometry_attribute.h>
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>

#include <glm/geometric.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr char LockedBunnySha256[] =
            "3bb08f257d873f69ded447e07c2dd4e9d7a264d58a686c88978c38430c5f6eb4";

        /** Computes the lowercase SHA-256 lock for one in-memory asset. */
        eastl::string calculateDracoAssetSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error("The Draco asset is too large to hash.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
            constexpr char Hex[] = "0123456789abcdef";
            eastl::string encoded;
            encoded.resize(digest.size() * 2u);
            for (size_t index = 0u; index < digest.size(); ++index)
            {
                encoded[index * 2u] = Hex[digest[index] >> 4u];
                encoded[index * 2u + 1u] = Hex[digest[index] & 0x0fu];
            }
            return encoded;
        }

        /** Reads an immutable binary asset without accepting partial streams. */
        eastl::vector<char> readDracoAssetBytes(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error("Could not open the locked Draco asset.");
            }
            const std::streamsize byteCount = stream.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error("The locked Draco asset is empty.");
            }
            stream.seekg(0, std::ios::beg);
            eastl::vector<char> bytes(static_cast<size_t>(byteCount));
            if (!stream.read(bytes.data(), byteCount))
            {
                throw std::runtime_error("Could not read the complete Draco asset.");
            }
            return bytes;
        }

        /** Reproduces Three BufferGeometry's indexed area-weighted vertex normals. */
        void generateDracoVertexNormals(DracoMeshAsset &asset)
        {
            asset.normals.assign(asset.positions.size(), glm::vec3(0.0f));
            for (size_t index = 0u; index < asset.indices.size(); index += 3u)
            {
                const uint32_t a = asset.indices[index + 0u];
                const uint32_t b = asset.indices[index + 1u];
                const uint32_t c = asset.indices[index + 2u];
                const glm::vec3 faceNormal = glm::cross(asset.positions[c] - asset.positions[b], asset.positions[a] - asset.positions[b]);
                asset.normals[a] += faceNormal;
                asset.normals[b] += faceNormal;
                asset.normals[c] += faceNormal;
            }
            for (glm::vec3 &normal : asset.normals)
            {
                const float lengthSquared = glm::dot(normal, normal);
                normal = lengthSquared > 0.0f ? normal * glm::inversesqrt(lengthSquared) : glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    } // namespace

    DracoMeshAsset loadDracoMeshAsset(const std::filesystem::path &path)
    {
        const eastl::vector<char> bytes = readDracoAssetBytes(path);
        const eastl::string sourceSha256 = calculateDracoAssetSha256(bytes.data(), bytes.size());
        if (sourceSha256 != LockedBunnySha256)
        {
            throw std::runtime_error("The locked Draco bunny SHA-256 changed.");
        }
        draco::DecoderBuffer buffer;
        buffer.Init(bytes.data(), bytes.size());
        draco::Decoder decoder;
        const auto geometryType = decoder.GetEncodedGeometryType(&buffer);
        if (!geometryType.ok() || geometryType.value() != draco::TRIANGULAR_MESH)
        {
            throw std::runtime_error("The locked Draco asset is not a triangular mesh.");
        }
        auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
        if (!decoded.ok() || decoded.value() == nullptr)
        {
            throw std::runtime_error("The locked Draco mesh could not be decoded.");
        }
        std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
        const int positionAttributeId = mesh->GetNamedAttributeId(draco::GeometryAttribute::POSITION);
        if (positionAttributeId < 0)
        {
            throw std::runtime_error("The Draco mesh has no POSITION attribute.");
        }
        const draco::PointAttribute *positions = mesh->attribute(positionAttributeId);
        if (positions == nullptr || positions->num_components() != 3)
        {
            throw std::runtime_error("The Draco POSITION attribute is not float3-compatible.");
        }

        DracoMeshAsset asset;
        asset.sourceSha256 = sourceSha256;
        asset.positions.resize(mesh->num_points());
        for (draco::PointIndex point(0); point < mesh->num_points(); ++point)
        {
            const draco::AttributeValueIndex valueIndex = positions->mapped_index(point);
            float value[3] = {};
            if (!positions->ConvertValue<float, 3>(valueIndex, value))
            {
                throw std::runtime_error("A Draco POSITION value could not be converted.");
            }
            asset.positions[point.value()] = glm::vec3(value[0], value[1], value[2]);
        }
        asset.indices.reserve(static_cast<size_t>(mesh->num_faces()) * 3u);
        for (draco::FaceIndex faceIndex(0); faceIndex < mesh->num_faces(); ++faceIndex)
        {
            const draco::Mesh::Face &face = mesh->face(faceIndex);
            asset.indices.push_back(face[0].value());
            asset.indices.push_back(face[1].value());
            asset.indices.push_back(face[2].value());
        }
        generateDracoVertexNormals(asset);
        return asset;
    }
} // namespace GVM::ThreeSamples
