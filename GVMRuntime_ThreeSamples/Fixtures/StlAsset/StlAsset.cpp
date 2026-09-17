#include "StlAsset.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Reads one complete bounded asset file. */
        eastl::vector<uint8_t> readStlBytes(const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open STL asset: " + assetPath.string());
            const std::streamsize byteCount = input.tellg();
            if (byteCount < 0 || static_cast<uint64_t>(byteCount) >
                    std::numeric_limits<size_t>::max())
                throw std::runtime_error("STL asset has an invalid byte count.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read the complete STL asset.");
            return bytes;
        }

        /** Calculates one lowercase SHA-256 lock for the exact encoded asset bytes. */
        eastl::string calculateStlSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            std::ostringstream text;
            text << std::hex << std::setfill('0');
            for (uint8_t value : digest) text << std::setw(2) << unsigned(value);
            return text.str().c_str();
        }

        /** Reads one little-endian unsigned 16-bit value from a checked byte range. */
        uint16_t readStlUint16(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 2u)
                throw std::runtime_error("STL payload contains a truncated uint16 value.");
            return uint16_t(bytes[offset]) | uint16_t(bytes[offset + 1u]) << 8u;
        }

        /** Reads one little-endian unsigned 32-bit value from a checked byte range. */
        uint32_t readStlUint32(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
                throw std::runtime_error("STL payload contains a truncated uint32 value.");
            return uint32_t(bytes[offset]) |
                uint32_t(bytes[offset + 1u]) << 8u |
                uint32_t(bytes[offset + 2u]) << 16u |
                uint32_t(bytes[offset + 3u]) << 24u;
        }

        /** Reads one IEEE-754 little-endian float from a checked byte range. */
        float readStlFloat(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            const uint32_t word = readStlUint32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &word, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("STL payload contains a non-finite float.");
            return value;
        }

        /** Converts an sRGB unit channel to the linear Color value emitted by Three r185. */
        float stlSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Detects the exact binary size rule used before Three STLLoader's ASCII prefix fallback. */
        bool isBinaryStl(const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >= 84u)
            {
                const uint64_t expected = 84u + uint64_t(readStlUint32(bytes, 80u)) * 50u;
                if (expected == bytes.size()) return true;
            }
            for (size_t offset = 0u; offset < 5u && offset + 5u <= bytes.size(); ++offset)
            {
                if (std::memcmp(bytes.data() + offset, "solid", 5u) == 0) return false;
            }
            return true;
        }

        /** Decodes one binary STL including Magics header and per-facet color semantics. */
        StlAsset parseBinaryStl(const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() < 84u) throw std::runtime_error("Binary STL header is truncated.");
            StlAsset asset;
            asset.binary = true;
            asset.facetCount = readStlUint32(bytes, 80u);
            if (84u + uint64_t(asset.facetCount) * 50u != bytes.size())
                throw std::runtime_error("Binary STL facet count does not match its byte count.");

            glm::vec4 defaultColor(1.0f);
            for (size_t index = 0u; index + 10u <= 80u; ++index)
            {
                if (std::memcmp(bytes.data() + index, "COLOR=", 6u) != 0) continue;
                asset.hasColors = true;
                defaultColor = glm::vec4(
                    stlSrgbToLinear(float(bytes[index + 6u]) / 255.0f),
                    stlSrgbToLinear(float(bytes[index + 7u]) / 255.0f),
                    stlSrgbToLinear(float(bytes[index + 8u]) / 255.0f), 1.0f);
                asset.alpha = float(bytes[index + 9u]) / 255.0f;
                break;
            }

            asset.vertices.reserve(size_t(asset.facetCount) * 3u);
            for (uint32_t face = 0u; face < asset.facetCount; ++face)
            {
                const size_t start = 84u + size_t(face) * 50u;
                const glm::vec4 normal(
                    readStlFloat(bytes, start), readStlFloat(bytes, start + 4u),
                    readStlFloat(bytes, start + 8u), 0.0f);
                glm::vec4 color = defaultColor;
                if (asset.hasColors)
                {
                    const uint16_t packedColor = readStlUint16(bytes, start + 48u);
                    if ((packedColor & 0x8000u) == 0u)
                    {
                        color = glm::vec4(
                            stlSrgbToLinear(float(packedColor & 0x1fu) / 31.0f),
                            stlSrgbToLinear(float((packedColor >> 5u) & 0x1fu) / 31.0f),
                            stlSrgbToLinear(float((packedColor >> 10u) & 0x1fu) / 31.0f), 1.0f);
                    }
                }
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const size_t vertexOffset = start + 12u + size_t(corner) * 12u;
                    asset.vertices.push_back({
                        .position = glm::vec4(
                            readStlFloat(bytes, vertexOffset),
                            readStlFloat(bytes, vertexOffset + 4u),
                            readStlFloat(bytes, vertexOffset + 8u), 1.0f),
                        .normal = normal,
                        .color = color,
                    });
                }
            }
            return asset;
        }

        /** Decodes the canonical ASCII facet grammar accepted by the frozen r185 assets. */
        StlAsset parseAsciiStl(const eastl::vector<uint8_t> &bytes)
        {
            StlAsset asset;
            std::istringstream input(std::string(
                reinterpret_cast<const char *>(bytes.data()), bytes.size()));
            std::string token;
            while (input >> token)
            {
                if (token != "facet") continue;
                if (!(input >> token) || token != "normal")
                    throw std::runtime_error("ASCII STL facet is missing its normal.");
                glm::vec4 normal(0.0f, 0.0f, 0.0f, 0.0f);
                if (!(input >> normal.x >> normal.y >> normal.z))
                    throw std::runtime_error("ASCII STL facet normal is malformed.");
                uint32_t vertexCount = 0u;
                while (input >> token)
                {
                    if (token == "vertex")
                    {
                        StlAssetVertex vertex;
                        if (!(input >> vertex.position.x >> vertex.position.y >> vertex.position.z))
                            throw std::runtime_error("ASCII STL vertex is malformed.");
                        vertex.position.w = 1.0f;
                        vertex.normal = normal;
                        asset.vertices.push_back(vertex);
                        ++vertexCount;
                    }
                    else if (token == "endfacet")
                    {
                        break;
                    }
                }
                if (vertexCount != 3u)
                    throw std::runtime_error("ASCII STL facet does not contain exactly three vertices.");
                ++asset.facetCount;
            }
            if (asset.facetCount == 0u)
                throw std::runtime_error("ASCII STL contains no facets.");
            return asset;
        }
    } // namespace

    StlAsset loadStlAsset(const std::filesystem::path &assetPath)
    {
        const eastl::vector<uint8_t> bytes = readStlBytes(assetPath);
        StlAsset asset = isBinaryStl(bytes)
            ? parseBinaryStl(bytes)
            : parseAsciiStl(bytes);
        asset.sha256 = calculateStlSha256(bytes);
        return asset;
    }
} // namespace GVM::ThreeSamples
