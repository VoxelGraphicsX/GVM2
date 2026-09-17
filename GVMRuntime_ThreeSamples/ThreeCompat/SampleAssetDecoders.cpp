#include "SampleAssetDecoders.hpp"

#include <nlohmann/json.hpp>

#include <EASTL/array.h>

#include <cmath>
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr uint32_t GlbMagic = 0x46546C67u;
        constexpr uint32_t GlbJsonChunk = 0x4E4F534Au;
        constexpr uint32_t GlbBinaryChunk = 0x004E4942u;

        /** Stores one OBJ face vertex before its source indices are resolved. */
        struct ObjFaceVertex
        {
            int32_t positionIndex = 0;
            int32_t textureCoordinateIndex = 0;
            int32_t normalIndex = 0;
            bool hasTextureCoordinate = false;
            bool hasNormal = false;
        };

        /** Returns the next nonempty ASCII token and advances one line cursor. */
        std::string_view readObjToken(
            std::string_view line,
            size_t &cursor)
        {
            while (cursor < line.size() &&
                   (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }
            const size_t start = cursor;
            while (cursor < line.size() &&
                   line[cursor] != ' ' &&
                   line[cursor] != '\t')
            {
                ++cursor;
            }
            return line.substr(start, cursor - start);
        }

        /** Parses one finite OBJ floating-point token. */
        float parseObjFloat(std::string_view token)
        {
            float value = 0.0f;
            const auto result = std::from_chars(
                token.data(),
                token.data() + token.size(),
                value);
            if (result.ec != std::errc() ||
                result.ptr != token.data() + token.size() ||
                !std::isfinite(value))
            {
                throw std::runtime_error(
                    "OBJ contains an invalid floating-point value.");
            }
            return value;
        }

        /** Parses one nonzero signed OBJ attribute index. */
        int32_t parseObjIndex(std::string_view token)
        {
            int32_t value = 0;
            const auto result = std::from_chars(
                token.data(),
                token.data() + token.size(),
                value);
            if (result.ec != std::errc() ||
                result.ptr != token.data() + token.size() ||
                value == 0)
            {
                throw std::runtime_error(
                    "OBJ contains an invalid attribute index.");
            }
            return value;
        }

        /** Parses one v, v/vt, v//vn, or v/vt/vn OBJ face token. */
        ObjFaceVertex parseObjFaceVertex(std::string_view token)
        {
            ObjFaceVertex result;
            const size_t firstSlash = token.find('/');
            if (firstSlash == std::string_view::npos)
            {
                result.positionIndex = parseObjIndex(token);
                return result;
            }
            result.positionIndex =
                parseObjIndex(token.substr(0u, firstSlash));
            const size_t secondSlash =
                token.find('/', firstSlash + 1u);
            const size_t textureEnd =
                secondSlash == std::string_view::npos
                    ? token.size()
                    : secondSlash;
            if (textureEnd > firstSlash + 1u)
            {
                result.textureCoordinateIndex =
                    parseObjIndex(
                        token.substr(
                            firstSlash + 1u,
                            textureEnd - firstSlash - 1u));
                result.hasTextureCoordinate = true;
            }
            if (secondSlash != std::string_view::npos &&
                secondSlash + 1u < token.size())
            {
                result.normalIndex =
                    parseObjIndex(token.substr(secondSlash + 1u));
                result.hasNormal = true;
            }
            return result;
        }

        /** Resolves one positive or relative OBJ index into a zero-based source element. */
        size_t resolveObjIndex(
            int32_t index,
            size_t elementCount)
        {
            const int64_t resolved =
                index > 0
                    ? int64_t(index) - 1
                    : int64_t(elementCount) + int64_t(index);
            if (resolved < 0 ||
                uint64_t(resolved) >= elementCount)
            {
                throw std::runtime_error(
                    "OBJ face index exceeds its source attribute array.");
            }
            return static_cast<size_t>(resolved);
        }

        /** Appends one resolved OBJ face vertex to triangle-expanded output arrays. */
        void appendObjVertex(
            DecodedObjMesh &mesh,
            const ObjFaceVertex &vertex,
            const eastl::vector<float> &positions,
            const eastl::vector<float> &textureCoordinates,
            const eastl::vector<float> &normals)
        {
            const size_t positionIndex =
                resolveObjIndex(
                    vertex.positionIndex,
                    positions.size() / 3u);
            mesh.positions.insert(
                mesh.positions.end(),
                positions.begin() + positionIndex * 3u,
                positions.begin() + positionIndex * 3u + 3u);
            if (vertex.hasNormal)
            {
                const size_t normalIndex =
                    resolveObjIndex(
                        vertex.normalIndex,
                        normals.size() / 3u);
                mesh.normals.insert(
                    mesh.normals.end(),
                    normals.begin() + normalIndex * 3u,
                    normals.begin() + normalIndex * 3u + 3u);
            }
            else
            {
                mesh.normals.insert(
                    mesh.normals.end(),
                    {0.0f, 0.0f, 0.0f});
            }
            if (vertex.hasTextureCoordinate)
            {
                const size_t textureCoordinateIndex =
                    resolveObjIndex(
                        vertex.textureCoordinateIndex,
                        textureCoordinates.size() / 2u);
                mesh.textureCoordinates.insert(
                    mesh.textureCoordinates.end(),
                    textureCoordinates.begin() +
                        textureCoordinateIndex * 2u,
                    textureCoordinates.begin() +
                        textureCoordinateIndex * 2u + 2u);
            }
            else
            {
                mesh.textureCoordinates.insert(
                    mesh.textureCoordinates.end(),
                    {0.0f, 0.0f});
            }
        }

        /** Reads one bounded little-endian unsigned integer. */
        uint32_t readAssetUint32(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error("Asset payload ended while reading a uint32.");
            }
            return uint32_t(bytes[offset]) |
                   (uint32_t(bytes[offset + 1u]) << 8u) |
                   (uint32_t(bytes[offset + 2u]) << 16u) |
                   (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Returns the scalar count for one supported glTF accessor type. */
        uint32_t glbAccessorComponentCount(const std::string &type)
        {
            if (type == "SCALAR") return 1u;
            if (type == "VEC2") return 2u;
            if (type == "VEC3") return 3u;
            if (type == "VEC4") return 4u;
            throw std::runtime_error("GLB accessor type is not supported.");
        }

        /** Reads one tightly packed or strided Float32 GLB accessor. */
        eastl::vector<float> readGlbFloatAccessor(
            const nlohmann::json &document,
            const eastl::vector<uint8_t> &binaryChunk,
            uint32_t accessorIndex,
            uint32_t expectedComponents)
        {
            const auto &accessor = document.at("accessors").at(accessorIndex);
            if (accessor.at("componentType").get<uint32_t>() != 5126u ||
                accessor.contains("sparse") ||
                glbAccessorComponentCount(
                    accessor.at("type").get<std::string>()) !=
                    expectedComponents)
            {
                throw std::runtime_error(
                    "GLB Float32 accessor encoding does not match its semantic.");
            }
            const uint32_t count = accessor.at("count").get<uint32_t>();
            const auto &view = document.at("bufferViews").at(
                accessor.at("bufferView").get<uint32_t>());
            const uint64_t baseOffset =
                uint64_t(view.value("byteOffset", 0u)) +
                uint64_t(accessor.value("byteOffset", 0u));
            const uint64_t packedStride =
                uint64_t(expectedComponents) * sizeof(float);
            const uint64_t stride =
                view.value("byteStride", uint32_t(packedStride));
            if (stride < packedStride ||
                (count != 0u &&
                 baseOffset + uint64_t(count - 1u) * stride + packedStride >
                     binaryChunk.size()))
            {
                throw std::runtime_error(
                    "GLB Float32 accessor exceeds the binary chunk.");
            }
            eastl::vector<float> result;
            result.resize(size_t(count) * expectedComponents);
            for (uint32_t element = 0u; element < count; ++element)
            {
                std::memcpy(
                    result.data() + size_t(element) * expectedComponents,
                    binaryChunk.data() +
                        baseOffset + uint64_t(element) * stride,
                    static_cast<size_t>(packedStride));
            }
            return result;
        }

        /** Reads one Uint16 or Uint32 scalar GLB index accessor. */
        eastl::vector<uint32_t> readGlbIndexAccessor(
            const nlohmann::json &document,
            const eastl::vector<uint8_t> &binaryChunk,
            uint32_t accessorIndex)
        {
            const auto &accessor = document.at("accessors").at(accessorIndex);
            const uint32_t componentType =
                accessor.at("componentType").get<uint32_t>();
            const uint32_t componentBytes =
                componentType == 5123u
                    ? 2u
                    : componentType == 5125u ? 4u : 0u;
            if (accessor.at("type").get<std::string>() != "SCALAR" ||
                accessor.contains("sparse") ||
                componentBytes == 0u)
            {
                throw std::runtime_error(
                    "GLB index accessor is not Uint16 or Uint32.");
            }
            const uint32_t count = accessor.at("count").get<uint32_t>();
            const auto &view = document.at("bufferViews").at(
                accessor.at("bufferView").get<uint32_t>());
            const uint64_t baseOffset =
                uint64_t(view.value("byteOffset", 0u)) +
                uint64_t(accessor.value("byteOffset", 0u));
            const uint64_t stride =
                view.value("byteStride", componentBytes);
            if (stride < componentBytes ||
                (count != 0u &&
                 baseOffset + uint64_t(count - 1u) * stride + componentBytes >
                     binaryChunk.size()))
            {
                throw std::runtime_error(
                    "GLB index accessor exceeds the binary chunk.");
            }
            eastl::vector<uint32_t> result;
            result.resize(count);
            for (uint32_t element = 0u; element < count; ++element)
            {
                const uint8_t *source =
                    binaryChunk.data() +
                    baseOffset + uint64_t(element) * stride;
                if (componentType == 5123u)
                {
                    uint16_t value = 0u;
                    std::memcpy(&value, source, sizeof(value));
                    result[element] = value;
                }
                else
                {
                    std::memcpy(
                        &result[element],
                        source,
                        sizeof(result[element]));
                }
            }
            return result;
        }

        /** Returns one required numeric JSON array from a BufferGeometry attribute. */
        const nlohmann::json &readGeometryAttributeArray(const nlohmann::json &data,
                                                        const char *attributeName,
                                                        bool required)
        {
            const auto attributesIterator = data.find("attributes");
            if (attributesIterator == data.end() || !attributesIterator->is_object())
            {
                if (required) throw std::runtime_error("BufferGeometry JSON has no attributes object.");
                static const nlohmann::json EmptyArray = nlohmann::json::array();
                return EmptyArray;
            }
            const auto attributeIterator = attributesIterator->find(attributeName);
            if (attributeIterator == attributesIterator->end())
            {
                if (required) throw std::runtime_error("BufferGeometry JSON lacks a required attribute.");
                static const nlohmann::json EmptyArray = nlohmann::json::array();
                return EmptyArray;
            }
            const auto arrayIterator = attributeIterator->find("array");
            if (arrayIterator == attributeIterator->end() || !arrayIterator->is_array())
            {
                throw std::runtime_error("BufferGeometry attribute has no numeric array.");
            }
            return *arrayIterator;
        }

        /** Copies one JSON number array into an EASTL float vector. */
        eastl::vector<float> copyGeometryFloatArray(const nlohmann::json &array)
        {
            eastl::vector<float> result;
            result.reserve(array.size());
            for (const nlohmann::json &value : array)
            {
                if (!value.is_number()) throw std::runtime_error("BufferGeometry attribute contains a non-number.");
                const double number = value.get<double>();
                if (!std::isfinite(number) ||
                    number < -double(std::numeric_limits<float>::max()) ||
                    number > double(std::numeric_limits<float>::max()))
                {
                    throw std::runtime_error("BufferGeometry attribute contains a non-finite float.");
                }
                result.push_back(static_cast<float>(number));
            }
            return result;
        }

        /** Copies one JSON integer array into an EASTL uint32 vector. */
        eastl::vector<uint32_t> copyGeometryIndexArray(const nlohmann::json &array)
        {
            eastl::vector<uint32_t> result;
            result.reserve(array.size());
            for (const nlohmann::json &value : array)
            {
                if (!value.is_number_unsigned() && !value.is_number_integer())
                {
                    throw std::runtime_error("BufferGeometry index contains a non-integer.");
                }
                const int64_t number = value.get<int64_t>();
                if (number < 0 || uint64_t(number) > std::numeric_limits<uint32_t>::max())
                {
                    throw std::runtime_error("BufferGeometry index exceeds uint32.");
                }
                result.push_back(static_cast<uint32_t>(number));
            }
            return result;
        }

        /** Reads one newline-terminated ASCII line without accepting embedded NUL bytes. */
        eastl::string readRadianceLine(const eastl::vector<uint8_t> &bytes, size_t &offset)
        {
            const size_t lineStart = offset;
            while (offset < bytes.size() && bytes[offset] != uint8_t('\n'))
            {
                if (bytes[offset] == 0u) throw std::runtime_error("Radiance header contains a NUL byte.");
                ++offset;
            }
            if (offset == bytes.size()) throw std::runtime_error("Radiance header line is unterminated.");
            eastl::string line(reinterpret_cast<const char *>(bytes.data() + lineStart), offset - lineStart);
            ++offset;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }

        /** Converts one RGBE quadruplet into a linear RGBA texel. */
        void appendRadianceTexel(eastl::vector<float> &rgba, const uint8_t *rgbe)
        {
            if (rgbe[3u] == 0u)
            {
                rgba.insert(rgba.end(), {0.0f, 0.0f, 0.0f, 1.0f});
                return;
            }
            // Three.js r185's HDRLoader uses 2^(E-128)/255 for RGBE
            // conversion.  Keep the decoder numerically aligned with that
            // reference instead of the common 2^(E-136) approximation.
            const float scale =
                std::ldexp(1.0f, int(rgbe[3u]) - 128) / 255.0f;
            rgba.push_back(float(rgbe[0u]) * scale);
            rgba.push_back(float(rgbe[1u]) * scale);
            rgba.push_back(float(rgbe[2u]) * scale);
            rgba.push_back(1.0f);
        }

        /** Decodes one modern four-channel Radiance RLE scanline. */
        eastl::vector<uint8_t> decodeRadianceScanline(const eastl::vector<uint8_t> &bytes,
                                                      size_t &offset,
                                                      uint32_t width)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error("Radiance scanline prefix is truncated.");
            }
            if (bytes[offset] != 2u || bytes[offset + 1u] != 2u ||
                (uint32_t(bytes[offset + 2u]) << 8u | uint32_t(bytes[offset + 3u])) != width)
            {
                throw std::runtime_error("Radiance image does not use the required modern RLE scanline.");
            }
            offset += 4u;
            eastl::vector<uint8_t> planar(static_cast<size_t>(width) * 4u);
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                uint32_t x = 0u;
                while (x < width)
                {
                    if (offset == bytes.size()) throw std::runtime_error("Radiance RLE run is truncated.");
                    const uint8_t code = bytes[offset++];
                    if (code > 128u)
                    {
                        const uint32_t count = uint32_t(code) - 128u;
                        if (count == 0u || count > width - x || offset == bytes.size())
                        {
                            throw std::runtime_error("Radiance repeated run is invalid.");
                        }
                        const uint8_t value = bytes[offset++];
                        for (uint32_t index = 0u; index < count; ++index)
                        {
                            planar[static_cast<size_t>(channel) * width + x++] = value;
                        }
                    }
                    else
                    {
                        const uint32_t count = uint32_t(code);
                        if (count == 0u || count > width - x ||
                            offset > bytes.size() || bytes.size() - offset < count)
                        {
                            throw std::runtime_error("Radiance literal run is invalid.");
                        }
                        for (uint32_t index = 0u; index < count; ++index)
                        {
                            planar[static_cast<size_t>(channel) * width + x++] = bytes[offset++];
                        }
                    }
                }
            }
            eastl::vector<uint8_t> interleaved(static_cast<size_t>(width) * 4u);
            for (uint32_t x = 0u; x < width; ++x)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    interleaved[static_cast<size_t>(x) * 4u + channel] =
                        planar[static_cast<size_t>(channel) * width + x];
                }
            }
            return interleaved;
        }

        /** Compares one four-byte chunk identifier without unaligned loads. */
        bool chunkIdentifierEquals(const eastl::vector<uint8_t> &bytes, size_t offset, const char *identifier)
        {
            return offset <= bytes.size() && bytes.size() - offset >= 4u &&
                   std::memcmp(bytes.data() + offset, identifier, 4u) == 0;
        }

        /** Decodes one sRGB palette channel into Three's linear working space. */
        float decodeVoxSrgb(uint8_t value)
        {
            const float encoded = float(value) / 255.0f;
            if (encoded <= 0.04045f) return encoded * 0.0773993808f;
            return std::pow(encoded * 0.9478672986f + 0.0521327014f, 2.4f);
        }

        /** Converts one VOX grid corner into the centered Three.js Y-up coordinate system. */
        eastl::array<float, 3u> convertVoxPosition(const eastl::array<int32_t, 3u> &position,
                                                  const DecodedVoxModel &model)
        {
            return {
                float(position[0u]) - float(model.sizeX) * 0.5f,
                float(position[2u]) - float(model.sizeZ) * 0.5f,
                -float(position[1u]) + float(model.sizeY) * 0.5f};
        }

        /** Returns one normalized cross product for an emitted VOX triangle. */
        eastl::array<float, 3u> calculateVoxNormal(const eastl::array<float, 3u> &p0,
                                                  const eastl::array<float, 3u> &p1,
                                                  const eastl::array<float, 3u> &p2)
        {
            const float ax = p1[0u] - p0[0u];
            const float ay = p1[1u] - p0[1u];
            const float az = p1[2u] - p0[2u];
            const float bx = p2[0u] - p0[0u];
            const float by = p2[1u] - p0[1u];
            const float bz = p2[2u] - p0[2u];
            const float nx = ay * bz - az * by;
            const float ny = az * bx - ax * bz;
            const float nz = ax * by - ay * bx;
            const float inverseLength = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            return {nx * inverseLength, ny * inverseLength, nz * inverseLength};
        }
    }

    DecodedBufferGeometry decodeThreeBufferGeometryJson(const eastl::vector<uint8_t> &bytes)
    {
        if (bytes.empty()) throw std::runtime_error("BufferGeometry JSON payload is empty.");
        const nlohmann::json document =
            nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, true, true);
        const auto dataIterator = document.find("data");
        if (dataIterator == document.end() || !dataIterator->is_object())
        {
            throw std::runtime_error("BufferGeometry JSON has no data object.");
        }
        DecodedBufferGeometry result;
        result.positions = copyGeometryFloatArray(readGeometryAttributeArray(*dataIterator, "position", true));
        result.normals = copyGeometryFloatArray(readGeometryAttributeArray(*dataIterator, "normal", false));
        result.textureCoordinates = copyGeometryFloatArray(readGeometryAttributeArray(*dataIterator, "uv", false));
        if (result.positions.empty() || result.positions.size() % 3u != 0u)
        {
            throw std::runtime_error("BufferGeometry positions are empty or not vec3-aligned.");
        }
        const size_t vertexCount = result.positions.size() / 3u;
        if (!result.normals.empty() && result.normals.size() != vertexCount * 3u)
        {
            throw std::runtime_error("BufferGeometry normal count differs from its position count.");
        }
        if (!result.textureCoordinates.empty() && result.textureCoordinates.size() != vertexCount * 2u)
        {
            throw std::runtime_error("BufferGeometry UV count differs from its position count.");
        }
        const auto indexIterator = dataIterator->find("index");
        if (indexIterator != dataIterator->end())
        {
            const auto arrayIterator = indexIterator->find("array");
            if (arrayIterator == indexIterator->end() || !arrayIterator->is_array())
            {
                throw std::runtime_error("BufferGeometry index has no array.");
            }
            result.indices = copyGeometryIndexArray(*arrayIterator);
            for (uint32_t index : result.indices)
            {
                if (index >= vertexCount) throw std::runtime_error("BufferGeometry index exceeds its vertex count.");
            }
        }
        else
        {
            result.indices.reserve(vertexCount);
            for (uint32_t index = 0u; index < vertexCount; ++index) result.indices.push_back(index);
        }
        if (result.indices.size() % 3u != 0u)
        {
            throw std::runtime_error("BufferGeometry index count is not triangle-aligned.");
        }
        const auto groupsIterator = dataIterator->find("groups");
        if (groupsIterator != dataIterator->end())
        {
            if (!groupsIterator->is_array()) throw std::runtime_error("BufferGeometry groups is not an array.");
            for (const nlohmann::json &group : *groupsIterator)
            {
                BufferGeometryGroup decodedGroup;
                decodedGroup.start = group.at("start").get<uint32_t>();
                decodedGroup.count = group.at("count").get<uint32_t>();
                decodedGroup.materialIndex = group.value("materialIndex", 0u);
                if (decodedGroup.start > result.indices.size() ||
                    decodedGroup.count > result.indices.size() - decodedGroup.start)
                {
                    throw std::runtime_error("BufferGeometry group exceeds its index array.");
                }
                result.groups.push_back(decodedGroup);
            }
        }
        return result;
    }

    DecodedGlbContainer decodeGlbContainer(const eastl::vector<uint8_t> &bytes)
    {
        if (bytes.size() < 20u || readAssetUint32(bytes, 0u) != GlbMagic ||
            readAssetUint32(bytes, 4u) != 2u || readAssetUint32(bytes, 8u) != bytes.size())
        {
            throw std::runtime_error("GLB header is invalid or incomplete.");
        }
        size_t offset = 12u;
        const uint32_t jsonLength = readAssetUint32(bytes, offset);
        const uint32_t jsonType = readAssetUint32(bytes, offset + 4u);
        offset += 8u;
        if (jsonType != GlbJsonChunk || jsonLength > bytes.size() - offset)
        {
            throw std::runtime_error("GLB JSON chunk is invalid.");
        }
        DecodedGlbContainer result;
        result.jsonText.assign(reinterpret_cast<const char *>(bytes.data() + offset), jsonLength);
        while (!result.jsonText.empty() &&
               (result.jsonText.back() == '\0' || result.jsonText.back() == ' ' ||
                result.jsonText.back() == '\n' || result.jsonText.back() == '\r' ||
                result.jsonText.back() == '\t'))
        {
            result.jsonText.pop_back();
        }
        const nlohmann::json parsedDocument =
            nlohmann::json::parse(result.jsonText.begin(), result.jsonText.end(), nullptr, true, true);
        (void)parsedDocument;
        offset += jsonLength;
        if (offset == bytes.size()) return result;
        if (bytes.size() - offset < 8u) throw std::runtime_error("GLB binary chunk header is truncated.");
        const uint32_t binaryLength = readAssetUint32(bytes, offset);
        const uint32_t binaryType = readAssetUint32(bytes, offset + 4u);
        offset += 8u;
        if (binaryType != GlbBinaryChunk || binaryLength > bytes.size() - offset ||
            offset + binaryLength != bytes.size())
        {
            throw std::runtime_error("GLB binary chunk is invalid.");
        }
        result.binaryChunk.assign(bytes.begin() + offset, bytes.end());
        return result;
    }

    DecodedGlbMesh decodeGlbMesh(
        const eastl::vector<uint8_t> &bytes,
        uint32_t meshIndex)
    {
        const DecodedGlbContainer container =
            decodeGlbContainer(bytes);
        const nlohmann::json document =
            nlohmann::json::parse(
                container.jsonText.begin(),
                container.jsonText.end(),
                nullptr,
                true,
                true);
        if (!document.contains("meshes") ||
            meshIndex >= document.at("meshes").size() ||
            document.at("meshes").at(meshIndex).at("primitives").empty())
        {
            throw std::runtime_error(
                "GLB has no requested mesh primitive.");
        }
        const auto &primitive =
            document.at("meshes").at(meshIndex).at("primitives").at(0u);
        bool unsupportedExtension = false;
        if (primitive.contains("extensions"))
        {
            for (auto extension = primitive.at("extensions").begin();
                 extension != primitive.at("extensions").end(); ++extension)
            {
                // KHR_materials_variants changes material selection only; the
                // position/normal/index payload remains an ordinary triangle list.
                if (extension.key() != "KHR_materials_variants")
                {
                    unsupportedExtension = true;
                    break;
                }
            }
        }
        if (primitive.value("mode", 4u) != 4u || unsupportedExtension)
        {
            throw std::runtime_error(
                "GLB requested primitive is not an unextended triangle list.");
        }
        const auto &attributes = primitive.at("attributes");
        DecodedGlbMesh result;
        result.positions = readGlbFloatAccessor(
            document,
            container.binaryChunk,
            attributes.at("POSITION").get<uint32_t>(),
            3u);
        if (attributes.contains("NORMAL"))
        {
            result.normals = readGlbFloatAccessor(
                document,
                container.binaryChunk,
                attributes.at("NORMAL").get<uint32_t>(),
                3u);
        }
        if (attributes.contains("TEXCOORD_0"))
        {
            result.textureCoordinates = readGlbFloatAccessor(
                document,
                container.binaryChunk,
                attributes.at("TEXCOORD_0").get<uint32_t>(),
                2u);
        }
        if (primitive.contains("indices"))
        {
            result.indices = readGlbIndexAccessor(
                document,
                container.binaryChunk,
                primitive.at("indices").get<uint32_t>());
        }
        else
        {
            const uint32_t vertexCount =
                static_cast<uint32_t>(result.positions.size() / 3u);
            result.indices.resize(vertexCount);
            for (uint32_t index = 0u; index < vertexCount; ++index)
            {
                result.indices[index] = index;
            }
        }
        const size_t vertexCount = result.positions.size() / 3u;
        if (result.positions.size() % 3u != 0u ||
            (!result.normals.empty() &&
             result.normals.size() != vertexCount * 3u) ||
            (!result.textureCoordinates.empty() &&
             result.textureCoordinates.size() != vertexCount * 2u))
        {
            throw std::runtime_error(
                "GLB requested primitive attributes have inconsistent counts.");
        }
        for (const uint32_t index : result.indices)
        {
            if (index >= vertexCount)
            {
                throw std::runtime_error(
                    "GLB requested primitive index exceeds its vertex range.");
            }
        }
        return result;
    }

    DecodedGlbMesh decodeFirstGlbMesh(
        const eastl::vector<uint8_t> &bytes)
    {
        return decodeGlbMesh(bytes, 0u);
    }

    eastl::vector<uint8_t> extractGlbBufferView(
        const eastl::vector<uint8_t> &bytes,
        uint32_t bufferViewIndex)
    {
        const DecodedGlbContainer container = decodeGlbContainer(bytes);
        const nlohmann::json document = nlohmann::json::parse(
            container.jsonText.begin(),
            container.jsonText.end(),
            nullptr,
            true,
            true);
        if (!document.contains("bufferViews") ||
            bufferViewIndex >= document.at("bufferViews").size())
        {
            throw std::runtime_error(
                "GLB buffer view index exceeds the document.");
        }
        const auto &view = document.at("bufferViews").at(bufferViewIndex);
        if (view.value("buffer", 0u) != 0u)
        {
            throw std::runtime_error(
                "GLB buffer view does not reference the binary chunk.");
        }
        const uint64_t byteOffset = view.value("byteOffset", uint64_t(0u));
        const uint64_t byteLength = view.at("byteLength").get<uint64_t>();
        if (byteOffset > container.binaryChunk.size() ||
            byteLength > container.binaryChunk.size() - byteOffset)
        {
            throw std::runtime_error(
                "GLB buffer view exceeds the binary chunk.");
        }
        return eastl::vector<uint8_t>(
            container.binaryChunk.begin() + static_cast<size_t>(byteOffset),
            container.binaryChunk.begin() +
                static_cast<size_t>(byteOffset + byteLength));
    }

    /** Decodes OBJ vertex records and triangulated faces into OBJLoader-compatible arrays. */
    DecodedObjMesh decodeObjTriangleMesh(
        const eastl::vector<uint8_t> &bytes)
    {
        if (bytes.empty() ||
            std::memchr(bytes.data(), 0, bytes.size()) != nullptr)
        {
            throw std::runtime_error(
                "OBJ payload must be nonempty ASCII text without NUL bytes.");
        }
        const std::string_view source(
            reinterpret_cast<const char *>(bytes.data()),
            bytes.size());
        eastl::vector<float> sourcePositions;
        eastl::vector<float> sourceNormals;
        eastl::vector<float> sourceTextureCoordinates;
        DecodedObjMesh mesh;

        size_t lineStart = 0u;
        while (lineStart < source.size())
        {
            size_t lineEnd = source.find('\n', lineStart);
            if (lineEnd == std::string_view::npos)
            {
                lineEnd = source.size();
            }
            std::string_view line =
                source.substr(lineStart, lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1u);
            }
            size_t cursor = 0u;
            const std::string_view record =
                readObjToken(line, cursor);
            if (record == "v" ||
                record == "vn" ||
                record == "vt")
            {
                const uint32_t componentCount =
                    record == "vt" ? 2u : 3u;
                eastl::vector<float> *destination =
                    record == "v"
                        ? &sourcePositions
                        : record == "vn"
                            ? &sourceNormals
                            : &sourceTextureCoordinates;
                for (uint32_t component = 0u;
                     component < componentCount;
                     ++component)
                {
                    const std::string_view token =
                        readObjToken(line, cursor);
                    if (token.empty())
                    {
                        throw std::runtime_error(
                            "OBJ vertex record has too few components.");
                    }
                    destination->push_back(parseObjFloat(token));
                }
            }
            else if (record == "f")
            {
                eastl::vector<ObjFaceVertex> face;
                while (true)
                {
                    const std::string_view token =
                        readObjToken(line, cursor);
                    if (token.empty())
                    {
                        break;
                    }
                    if (token.front() == '#')
                    {
                        break;
                    }
                    face.push_back(parseObjFaceVertex(token));
                }
                if (face.size() < 3u)
                {
                    throw std::runtime_error(
                        "OBJ face contains fewer than three vertices.");
                }
                for (size_t triangle = 1u;
                     triangle + 1u < face.size();
                     ++triangle)
                {
                    appendObjVertex(
                        mesh,
                        face[0u],
                        sourcePositions,
                        sourceTextureCoordinates,
                        sourceNormals);
                    appendObjVertex(
                        mesh,
                        face[triangle],
                        sourcePositions,
                        sourceTextureCoordinates,
                        sourceNormals);
                    appendObjVertex(
                        mesh,
                        face[triangle + 1u],
                        sourcePositions,
                        sourceTextureCoordinates,
                        sourceNormals);
                }
            }
            lineStart = lineEnd + 1u;
        }
        if (mesh.positions.empty() ||
            mesh.positions.size() % 9u != 0u ||
            mesh.normals.size() / 3u != mesh.positions.size() / 3u ||
            mesh.textureCoordinates.size() / 2u !=
                mesh.positions.size() / 3u)
        {
            throw std::runtime_error(
                "OBJ payload did not produce a complete triangle mesh.");
        }
        return mesh;
    }

    DecodedRadianceImage decodeRadianceRgbe(const eastl::vector<uint8_t> &bytes)
    {
        size_t offset = 0u;
        const eastl::string magic = readRadianceLine(bytes, offset);
        if (magic != "#?RADIANCE" && magic != "#?RGBE")
        {
            throw std::runtime_error("Radiance program identifier is invalid.");
        }
        bool formatSeen = false;
        while (true)
        {
            const eastl::string line = readRadianceLine(bytes, offset);
            if (line.empty()) break;
            if (line == "FORMAT=32-bit_rle_rgbe") formatSeen = true;
        }
        if (!formatSeen) throw std::runtime_error("Radiance RGBE format declaration is missing.");
        const eastl::string resolution = readRadianceLine(bytes, offset);
        char axisY = '\0';
        char axisX = '\0';
        unsigned int height = 0u;
        unsigned int width = 0u;
        if (std::sscanf(resolution.c_str(), "%cY %u %cX %u", &axisY, &height, &axisX, &width) != 4 ||
            axisY != '-' || axisX != '+' || width == 0u || height == 0u ||
            width > 0x7FFFu || uint64_t(width) * uint64_t(height) > 0x10000000ull)
        {
            throw std::runtime_error("Radiance resolution or orientation is unsupported.");
        }
        DecodedRadianceImage result;
        result.width = width;
        result.height = height;
        result.rgba.reserve(static_cast<size_t>(width) * height * 4u);
        for (uint32_t y = 0u; y < height; ++y)
        {
            const eastl::vector<uint8_t> scanline = decodeRadianceScanline(bytes, offset, width);
            for (uint32_t x = 0u; x < width; ++x)
            {
                appendRadianceTexel(result.rgba, scanline.data() + static_cast<size_t>(x) * 4u);
            }
        }
        if (offset != bytes.size()) throw std::runtime_error("Radiance payload has trailing bytes.");
        return result;
    }

    DecodedVoxModel decodeMagicaVoxel150(const eastl::vector<uint8_t> &bytes)
    {
        if (bytes.size() < 20u || !chunkIdentifierEquals(bytes, 0u, "VOX ") ||
            readAssetUint32(bytes, 4u) != 150u || !chunkIdentifierEquals(bytes, 8u, "MAIN"))
        {
            throw std::runtime_error("MagicaVoxel v150 header is invalid.");
        }
        const uint32_t mainContentBytes = readAssetUint32(bytes, 12u);
        const uint32_t mainChildrenBytes = readAssetUint32(bytes, 16u);
        if (mainContentBytes != 0u || uint64_t(mainChildrenBytes) + 20u != bytes.size())
        {
            throw std::runtime_error("MagicaVoxel MAIN chunk size is invalid.");
        }
        DecodedVoxModel result;
        size_t offset = 20u;
        bool sizeSeen = false;
        bool voxelsSeen = false;
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < 12u) throw std::runtime_error("MagicaVoxel child chunk is truncated.");
            const size_t identifierOffset = offset;
            const uint32_t contentBytes = readAssetUint32(bytes, offset + 4u);
            const uint32_t childrenBytes = readAssetUint32(bytes, offset + 8u);
            offset += 12u;
            if (uint64_t(contentBytes) + uint64_t(childrenBytes) > bytes.size() - offset)
            {
                throw std::runtime_error("MagicaVoxel child chunk exceeds MAIN.");
            }
            const size_t contentEnd = offset + contentBytes;
            if (chunkIdentifierEquals(bytes, identifierOffset, "SIZE") && !sizeSeen)
            {
                if (contentBytes != 12u) throw std::runtime_error("MagicaVoxel SIZE chunk is invalid.");
                result.sizeX = readAssetUint32(bytes, offset);
                result.sizeY = readAssetUint32(bytes, offset + 4u);
                result.sizeZ = readAssetUint32(bytes, offset + 8u);
                if (result.sizeX == 0u || result.sizeY == 0u || result.sizeZ == 0u ||
                    result.sizeX > 256u || result.sizeY > 256u || result.sizeZ > 256u)
                {
                    throw std::runtime_error("MagicaVoxel model dimensions are invalid.");
                }
                sizeSeen = true;
            }
            else if (chunkIdentifierEquals(bytes, identifierOffset, "XYZI") && !voxelsSeen)
            {
                if (!sizeSeen || contentBytes < 4u) throw std::runtime_error("MagicaVoxel XYZI precedes SIZE.");
                const uint32_t voxelCount = readAssetUint32(bytes, offset);
                if (uint64_t(voxelCount) * 4u + 4u != contentBytes)
                {
                    throw std::runtime_error("MagicaVoxel XYZI count is invalid.");
                }
                result.voxels.reserve(voxelCount);
                for (uint32_t voxelIndex = 0u; voxelIndex < voxelCount; ++voxelIndex)
                {
                    const size_t voxelOffset = offset + 4u + static_cast<size_t>(voxelIndex) * 4u;
                    VoxVoxel voxel{
                        .x = bytes[voxelOffset],
                        .y = bytes[voxelOffset + 1u],
                        .z = bytes[voxelOffset + 2u],
                        .colorIndex = bytes[voxelOffset + 3u]};
                    if (voxel.x >= result.sizeX || voxel.y >= result.sizeY || voxel.z >= result.sizeZ ||
                        voxel.colorIndex == 0u)
                    {
                        throw std::runtime_error("MagicaVoxel voxel exceeds SIZE or uses palette index zero.");
                    }
                    result.voxels.push_back(voxel);
                }
                voxelsSeen = true;
            }
            else if (chunkIdentifierEquals(bytes, identifierOffset, "RGBA"))
            {
                if (contentBytes != 1024u) throw std::runtime_error("MagicaVoxel RGBA palette is invalid.");
                result.paletteRgba.reserve(256u);
                for (uint32_t paletteIndex = 0u; paletteIndex < 256u; ++paletteIndex)
                {
                    result.paletteRgba.push_back(readAssetUint32(bytes, offset + paletteIndex * 4u));
                }
            }
            offset = contentEnd + childrenBytes;
        }
        if (!sizeSeen || !voxelsSeen) throw std::runtime_error("MagicaVoxel model lacks SIZE or XYZI.");
        return result;
    }

    DecodedVoxMesh buildVoxGreedyMesh(const DecodedVoxModel &model)
    {
        if (model.sizeX == 0u || model.sizeY == 0u || model.sizeZ == 0u ||
            model.paletteRgba.size() != 256u)
        {
            throw std::invalid_argument("VOX greedy mesher requires dimensions and a 256-entry palette.");
        }
        const size_t voxelStorageSize =
            static_cast<size_t>(model.sizeX) * model.sizeY * model.sizeZ;
        eastl::vector<uint8_t> volume(voxelStorageSize, 0u);
        for (const VoxVoxel &voxel : model.voxels)
        {
            const size_t voxelIndex =
                static_cast<size_t>(voxel.x) +
                static_cast<size_t>(voxel.y) * model.sizeX +
                static_cast<size_t>(voxel.z) * model.sizeX * model.sizeY;
            if (volume[voxelIndex] != 0u) throw std::runtime_error("VOX model contains duplicate voxels.");
            volume[voxelIndex] = voxel.colorIndex;
        }

        DecodedVoxMesh result;
        const eastl::array<uint32_t, 3u> dimensions = {
            model.sizeX, model.sizeY, model.sizeZ};
        for (uint32_t normalAxis = 0u; normalAxis < 3u; ++normalAxis)
        {
            const uint32_t uAxis = (normalAxis + 1u) % 3u;
            const uint32_t vAxis = (normalAxis + 2u) % 3u;
            const uint32_t normalExtent = dimensions[normalAxis];
            const uint32_t uExtent = dimensions[uAxis];
            const uint32_t vExtent = dimensions[vAxis];
            eastl::vector<int16_t> mask(static_cast<size_t>(uExtent) * vExtent);
            for (uint32_t slice = 0u; slice <= normalExtent; ++slice)
            {
                size_t maskIndex = 0u;
                for (uint32_t v = 0u; v < vExtent; ++v)
                {
                    for (uint32_t u = 0u; u < uExtent; ++u)
                    {
                        eastl::array<uint32_t, 3u> coordinate = {0u, 0u, 0u};
                        coordinate[normalAxis] = slice;
                        coordinate[uAxis] = u;
                        coordinate[vAxis] = v;
                        uint8_t behind = 0u;
                        uint8_t inFront = 0u;
                        if (slice > 0u)
                        {
                            eastl::array<uint32_t, 3u> behindCoordinate = coordinate;
                            --behindCoordinate[normalAxis];
                            behind = volume[
                                static_cast<size_t>(behindCoordinate[0u]) +
                                static_cast<size_t>(behindCoordinate[1u]) * model.sizeX +
                                static_cast<size_t>(behindCoordinate[2u]) * model.sizeX * model.sizeY];
                        }
                        if (slice < normalExtent)
                        {
                            inFront = volume[
                                static_cast<size_t>(coordinate[0u]) +
                                static_cast<size_t>(coordinate[1u]) * model.sizeX +
                                static_cast<size_t>(coordinate[2u]) * model.sizeX * model.sizeY];
                        }
                        mask[maskIndex++] =
                            behind > 0u && inFront == 0u
                            ? int16_t(behind)
                            : (inFront > 0u && behind == 0u
                               ? -int16_t(inFront)
                               : int16_t(0));
                    }
                }

                maskIndex = 0u;
                for (uint32_t v = 0u; v < vExtent; ++v)
                {
                    uint32_t u = 0u;
                    while (u < uExtent)
                    {
                        const int16_t signedColor = mask[maskIndex];
                        if (signedColor == 0)
                        {
                            ++u;
                            ++maskIndex;
                            continue;
                        }
                        uint32_t width = 1u;
                        while (u + width < uExtent && mask[maskIndex + width] == signedColor) ++width;
                        uint32_t height = 1u;
                        bool complete = false;
                        while (v + height < vExtent && !complete)
                        {
                            for (uint32_t widthIndex = 0u; widthIndex < width; ++widthIndex)
                            {
                                if (mask[maskIndex + widthIndex + static_cast<size_t>(height) * uExtent] != signedColor)
                                {
                                    complete = true;
                                    break;
                                }
                            }
                            if (!complete) ++height;
                        }
                        eastl::array<int32_t, 3u> position = {0, 0, 0};
                        eastl::array<int32_t, 3u> deltaU = {0, 0, 0};
                        eastl::array<int32_t, 3u> deltaV = {0, 0, 0};
                        position[normalAxis] = int32_t(slice);
                        position[uAxis] = int32_t(u);
                        position[vAxis] = int32_t(v);
                        deltaU[uAxis] = int32_t(width);
                        deltaV[vAxis] = int32_t(height);
                        const eastl::array<int32_t, 3u> positionU = {
                            position[0u] + deltaU[0u],
                            position[1u] + deltaU[1u],
                            position[2u] + deltaU[2u]};
                        const eastl::array<int32_t, 3u> positionUv = {
                            positionU[0u] + deltaV[0u],
                            positionU[1u] + deltaV[1u],
                            positionU[2u] + deltaV[2u]};
                        const eastl::array<int32_t, 3u> positionV = {
                            position[0u] + deltaV[0u],
                            position[1u] + deltaV[1u],
                            position[2u] + deltaV[2u]};
                        eastl::array<eastl::array<float, 3u>, 4u> corners = {
                            convertVoxPosition(position, model),
                            convertVoxPosition(positionU, model),
                            convertVoxPosition(positionUv, model),
                            convertVoxPosition(positionV, model)};
                        if (signedColor < 0)
                        {
                            const auto corner1 = corners[1u];
                            corners[1u] = corners[3u];
                            corners[3u] = corner1;
                        }
                        const auto normal = calculateVoxNormal(corners[0u], corners[1u], corners[2u]);
                        const uint32_t paletteIndex = uint32_t(std::abs(int(signedColor))) - 1u;
                        const uint32_t rgba = model.paletteRgba[paletteIndex];
                        const float colorR = decodeVoxSrgb(uint8_t(rgba));
                        const float colorG = decodeVoxSrgb(uint8_t(rgba >> 8u));
                        const float colorB = decodeVoxSrgb(uint8_t(rgba >> 16u));
                        const uint32_t baseVertex = static_cast<uint32_t>(result.vertices.size());
                        for (const auto &corner : corners)
                        {
                            result.vertices.push_back({
                                .positionX = corner[0u],
                                .positionY = corner[1u],
                                .positionZ = corner[2u],
                                .normalX = normal[0u],
                                .normalY = normal[1u],
                                .normalZ = normal[2u],
                                .colorR = colorR,
                                .colorG = colorG,
                                .colorB = colorB});
                        }
                        result.indices.insert(result.indices.end(), {
                            baseVertex, baseVertex + 1u, baseVertex + 2u,
                            baseVertex, baseVertex + 2u, baseVertex + 3u});
                        ++result.quadCount;
                        for (uint32_t heightIndex = 0u; heightIndex < height; ++heightIndex)
                        {
                            for (uint32_t widthIndex = 0u; widthIndex < width; ++widthIndex)
                            {
                                mask[maskIndex + widthIndex +
                                     static_cast<size_t>(heightIndex) * uExtent] = 0;
                            }
                        }
                        u += width;
                        maskIndex += width;
                    }
                }
            }
        }
        return result;
    }
}
