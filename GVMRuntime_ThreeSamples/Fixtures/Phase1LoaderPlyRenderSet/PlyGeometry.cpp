#include "PlyGeometry.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/geometric.hpp>

#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Describes one scalar or list property from a PLY element declaration. */
        struct PlyPropertyDescription
        {
            std::string type;
            std::string countType;
            std::string itemType;
            std::string name;
            bool isList = false;
        };

        /** Describes one ordered PLY element and every declared property. */
        struct PlyElementDescription
        {
            std::string name;
            uint32_t count = 0u;
            eastl::vector<PlyPropertyDescription> properties;
        };

        /** Stores the parsed PLY header, body byte offset, and ordered element declarations. */
        struct PlyHeaderDescription
        {
            PlyEncoding encoding = PlyEncoding::Ascii;
            size_t bodyOffset = 0u;
            eastl::vector<PlyElementDescription> elements;
        };

        /** Reads one bounded source file as exact bytes. */
        eastl::vector<uint8_t> readPlyBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open PLY asset: " + path.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error("PLY asset has an invalid byte count: " + path.string());
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete PLY asset: " + path.string());
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one exact PLY byte sequence. */
        eastl::string calculatePlySha256(const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error("PLY asset is too large for SHA-256 identity calculation.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
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

        /** Parses one checked unsigned decimal count from a PLY header token. */
        uint32_t parsePlyCount(const std::string &token, const char *label)
        {
            size_t parsedLength = 0u;
            unsigned long parsed = 0u;
            try
            {
                parsed = std::stoul(token, &parsedLength, 10);
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(std::string("Invalid PLY ") + label + " count.");
            }
            if (parsedLength != token.size() || parsed > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error(std::string("Invalid PLY ") + label + " count.");
            }
            return static_cast<uint32_t>(parsed);
        }

        /** Finds the exact byte immediately following the PLY end_header line terminator. */
        size_t findPlyBodyOffset(const eastl::vector<uint8_t> &bytes)
        {
            constexpr char EndHeader[] = "end_header";
            constexpr size_t EndHeaderLength = sizeof(EndHeader) - 1u;
            for (size_t offset = 0u; offset + EndHeaderLength < bytes.size(); ++offset)
            {
                if (std::memcmp(bytes.data() + offset, EndHeader, EndHeaderLength) != 0)
                {
                    continue;
                }
                size_t lineEnd = offset + EndHeaderLength;
                if (lineEnd < bytes.size() && bytes[lineEnd] == '\r')
                {
                    ++lineEnd;
                }
                if (lineEnd >= bytes.size() || bytes[lineEnd] != '\n')
                {
                    throw std::runtime_error("PLY end_header must end with LF or CRLF.");
                }
                return lineEnd + 1u;
            }
            throw std::runtime_error("PLY source is missing end_header.");
        }

        /** Parses and validates the ordered PLY header declarations used by both decoders. */
        PlyHeaderDescription parsePlyHeader(const eastl::vector<uint8_t> &bytes)
        {
            PlyHeaderDescription header;
            header.bodyOffset = findPlyBodyOffset(bytes);
            const std::string headerText(reinterpret_cast<const char *>(bytes.data()), header.bodyOffset);
            std::istringstream input(headerText);
            input.imbue(std::locale::classic());
            std::string line;
            if (!std::getline(input, line) || (line != "ply" && line != "ply\r"))
            {
                throw std::runtime_error("PLY source must begin with the canonical ply line.");
            }
            PlyElementDescription *currentElement = nullptr;
            bool foundFormat = false;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                std::istringstream lineInput(line);
                lineInput.imbue(std::locale::classic());
                std::string keyword;
                lineInput >> keyword;
                if (keyword.empty() || keyword == "comment" || keyword == "obj_info")
                {
                    continue;
                }
                if (keyword == "format")
                {
                    std::string format;
                    std::string version;
                    lineInput >> format >> version;
                    if (version != "1.0")
                    {
                        throw std::runtime_error("PLY format version must be 1.0.");
                    }
                    if (format == "ascii")
                    {
                        header.encoding = PlyEncoding::Ascii;
                    }
                    else if (format == "binary_little_endian")
                    {
                        header.encoding = PlyEncoding::BinaryLittleEndian;
                    }
                    else
                    {
                        throw std::runtime_error("PLY encoding is outside the frozen ASCII/little-endian contract.");
                    }
                    foundFormat = true;
                    continue;
                }
                if (keyword == "element")
                {
                    std::string name;
                    std::string count;
                    lineInput >> name >> count;
                    if (name.empty() || count.empty())
                    {
                        throw std::runtime_error("Malformed PLY element declaration.");
                    }
                    header.elements.push_back({.name = name, .count = parsePlyCount(count, name.c_str())});
                    currentElement = &header.elements.back();
                    continue;
                }
                if (keyword == "property")
                {
                    if (currentElement == nullptr)
                    {
                        throw std::runtime_error("PLY property appears before any element declaration.");
                    }
                    PlyPropertyDescription property;
                    lineInput >> property.type;
                    if (property.type == "list")
                    {
                        property.isList = true;
                        lineInput >> property.countType >> property.itemType >> property.name;
                    }
                    else
                    {
                        lineInput >> property.name;
                    }
                    if (property.name.empty())
                    {
                        throw std::runtime_error("Malformed PLY property declaration.");
                    }
                    currentElement->properties.push_back(property);
                    continue;
                }
                if (keyword == "end_header")
                {
                    break;
                }
                throw std::runtime_error("Unsupported PLY header directive: " + keyword);
            }
            if (!foundFormat)
            {
                throw std::runtime_error("PLY header is missing a format declaration.");
            }
            return header;
        }

        /** Validates that the source matches the exact position and face-list layout of this case. */
        void validatePlySchema(const PlyHeaderDescription &header, const PlyElementDescription *&vertexElement, const PlyElementDescription *&faceElement)
        {
            vertexElement = nullptr;
            faceElement = nullptr;
            for (const PlyElementDescription &element : header.elements)
            {
                if (element.name == "vertex")
                {
                    vertexElement = &element;
                }
                else if (element.name == "face")
                {
                    faceElement = &element;
                }
                else
                {
                    throw std::runtime_error("PLY source contains an unsupported element: " + element.name);
                }
            }
            if (vertexElement == nullptr || faceElement == nullptr || vertexElement->properties.size() != 3u || faceElement->properties.size() != 1u)
            {
                throw std::runtime_error("PLY source must contain position-only vertices and one face index list.");
            }
            constexpr const char *PositionNames[] = {"x", "y", "z"};
            for (size_t index = 0u; index < 3u; ++index)
            {
                const PlyPropertyDescription &property = vertexElement->properties[index];
                if (property.isList || property.name != PositionNames[index] || (property.type != "float" && property.type != "float32"))
                {
                    throw std::runtime_error("PLY vertex properties must be float x, y, z in order.");
                }
            }
            const PlyPropertyDescription &faceProperty = faceElement->properties[0u];
            if (!faceProperty.isList || (faceProperty.name != "vertex_indices" && faceProperty.name != "vertex_index") || (faceProperty.countType != "uchar" && faceProperty.countType != "uint8") || (faceProperty.itemType != "int" && faceProperty.itemType != "int32"))
            {
                throw std::runtime_error("PLY face property must be list uchar int vertex_indices.");
            }
        }

        /** Appends one validated triangle or Three-compatible quad triangulation. */
        void appendPlyFace(eastl::vector<uint32_t> &indices, const eastl::array<uint32_t, 4u> &faceIndices, uint32_t faceSize, uint32_t vertexCount)
        {
            if (faceSize != 3u && faceSize != 4u)
            {
                throw std::runtime_error("PLY face is neither a triangle nor a supported quad.");
            }
            for (uint32_t index = 0u; index < faceSize; ++index)
            {
                if (faceIndices[index] >= vertexCount)
                {
                    throw std::runtime_error("PLY face index exceeds the declared vertex count.");
                }
            }
            indices.push_back(faceIndices[0u]);
            indices.push_back(faceIndices[1u]);
            indices.push_back(faceSize == 3u ? faceIndices[2u] : faceIndices[3u]);
            if (faceSize == 4u)
            {
                indices.push_back(faceIndices[1u]);
                indices.push_back(faceIndices[2u]);
                indices.push_back(faceIndices[3u]);
            }
        }

        /** Decodes the exact ASCII body while preserving float32 vertex storage. */
        void parseAsciiPlyBody(const eastl::vector<uint8_t> &bytes, const PlyHeaderDescription &header, uint32_t vertexCount, uint32_t faceCount, PlyGeometry &geometry)
        {
            const std::string body(reinterpret_cast<const char *>(bytes.data() + header.bodyOffset), bytes.size() - header.bodyOffset);
            std::istringstream input(body);
            input.imbue(std::locale::classic());
            geometry.positions.reserve(vertexCount);
            for (uint32_t vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex)
            {
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                if (!(input >> x >> y >> z))
                {
                    throw std::runtime_error("ASCII PLY ended inside its vertex array.");
                }
                geometry.positions.push_back(glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
            }
            geometry.indices.reserve(uint64_t(faceCount) * 3u);
            for (uint32_t faceIndex = 0u; faceIndex < faceCount; ++faceIndex)
            {
                uint32_t faceSize = 0u;
                if (!(input >> faceSize) || faceSize > 4u)
                {
                    throw std::runtime_error("ASCII PLY contains an invalid face size.");
                }
                eastl::array<uint32_t, 4u> faceIndices = {};
                for (uint32_t index = 0u; index < faceSize; ++index)
                {
                    int64_t signedIndex = -1;
                    if (!(input >> signedIndex) || signedIndex < 0 || uint64_t(signedIndex) > std::numeric_limits<uint32_t>::max())
                    {
                        throw std::runtime_error("ASCII PLY contains an invalid face index.");
                    }
                    faceIndices[index] = static_cast<uint32_t>(signedIndex);
                }
                appendPlyFace(geometry.indices, faceIndices, faceSize, vertexCount);
            }
            std::string trailingToken;
            if (input >> trailingToken)
            {
                throw std::runtime_error("ASCII PLY has unexpected data after its declared elements.");
            }
        }

        /** Reads one checked little-endian uint32 from a bounded binary body. */
        uint32_t readLittleEndianUint32(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error("Binary PLY ended inside a 32-bit value.");
            }
            const uint32_t value = uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1u]) << 8u) | (uint32_t(bytes[offset + 2u]) << 16u) | (uint32_t(bytes[offset + 3u]) << 24u);
            offset += 4u;
            return value;
        }

        /** Reads one checked little-endian float32 without host alignment assumptions. */
        float readLittleEndianFloat32(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            const uint32_t bits = readLittleEndianUint32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /** Decodes the exact binary-little-endian body and rejects trailing bytes. */
        void parseBinaryPlyBody(const eastl::vector<uint8_t> &bytes, const PlyHeaderDescription &header, uint32_t vertexCount, uint32_t faceCount, PlyGeometry &geometry)
        {
            size_t offset = header.bodyOffset;
            geometry.positions.reserve(vertexCount);
            for (uint32_t vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex)
            {
                const float x = readLittleEndianFloat32(bytes, offset);
                const float y = readLittleEndianFloat32(bytes, offset);
                const float z = readLittleEndianFloat32(bytes, offset);
                geometry.positions.push_back(glm::vec3(x, y, z));
            }
            geometry.indices.reserve(uint64_t(faceCount) * 3u);
            for (uint32_t faceIndex = 0u; faceIndex < faceCount; ++faceIndex)
            {
                if (offset >= bytes.size())
                {
                    throw std::runtime_error("Binary PLY ended before a face list count.");
                }
                const uint32_t faceSize = bytes[offset++];
                if (faceSize > 4u)
                {
                    throw std::runtime_error("Binary PLY contains an invalid face size.");
                }
                eastl::array<uint32_t, 4u> faceIndices = {};
                for (uint32_t index = 0u; index < faceSize; ++index)
                {
                    const uint32_t encodedIndex = readLittleEndianUint32(bytes, offset);
                    if ((encodedIndex & 0x80000000u) != 0u)
                    {
                        throw std::runtime_error("Binary PLY contains a negative face index.");
                    }
                    faceIndices[index] = encodedIndex;
                }
                appendPlyFace(geometry.indices, faceIndices, faceSize, vertexCount);
            }
            if (offset != bytes.size())
            {
                throw std::runtime_error("Binary PLY has unexpected bytes after its declared elements.");
            }
        }

        /** Reproduces Three's indexed computeVertexNormals accumulation and normalization order. */
        void computeThreeVertexNormals(PlyGeometry &geometry)
        {
            geometry.normals.assign(geometry.positions.size(), glm::vec3(0.0f));
            for (size_t indexOffset = 0u; indexOffset < geometry.indices.size(); indexOffset += 3u)
            {
                const uint32_t indexA = geometry.indices[indexOffset];
                const uint32_t indexB = geometry.indices[indexOffset + 1u];
                const uint32_t indexC = geometry.indices[indexOffset + 2u];
                const glm::dvec3 positionA(geometry.positions[indexA]);
                const glm::dvec3 positionB(geometry.positions[indexB]);
                const glm::dvec3 positionC(geometry.positions[indexC]);
                const glm::dvec3 faceNormal = glm::cross(positionC - positionB, positionA - positionB);
                geometry.normals[indexA] = glm::vec3(glm::dvec3(geometry.normals[indexA]) + faceNormal);
                geometry.normals[indexB] = glm::vec3(glm::dvec3(geometry.normals[indexB]) + faceNormal);
                geometry.normals[indexC] = glm::vec3(glm::dvec3(geometry.normals[indexC]) + faceNormal);
            }
            for (glm::vec3 &normal : geometry.normals)
            {
                const double length = glm::length(glm::dvec3(normal));
                normal = length > 0.0 ? glm::vec3(glm::dvec3(normal) / length) : glm::vec3(0.0f);
            }
        }
    } // namespace

    PlyGeometry loadPlyGeometry(const std::filesystem::path &path, PlyEncoding expectedEncoding)
    {
        const eastl::vector<uint8_t> bytes = readPlyBytes(path);
        const PlyHeaderDescription header = parsePlyHeader(bytes);
        if (header.encoding != expectedEncoding)
        {
            throw std::runtime_error("PLY source encoding differs from the required scenario contract.");
        }
        const PlyElementDescription *vertexElement = nullptr;
        const PlyElementDescription *faceElement = nullptr;
        validatePlySchema(header, vertexElement, faceElement);

        PlyGeometry geometry;
        geometry.encoding = header.encoding;
        geometry.sourceVertexCount = vertexElement->count;
        geometry.sourceFaceCount = faceElement->count;
        geometry.sourceSha256 = calculatePlySha256(bytes);
        if (header.encoding == PlyEncoding::Ascii)
        {
            parseAsciiPlyBody(bytes, header, vertexElement->count, faceElement->count, geometry);
        }
        else
        {
            parseBinaryPlyBody(bytes, header, vertexElement->count, faceElement->count, geometry);
        }
        computeThreeVertexNormals(geometry);
        return geometry;
    }
} // namespace GVM::ThreeSamples
