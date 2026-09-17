#include "MichelleGlbAsset.hpp"

#include "GifImageDecoder.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *MichelleGlbSha256 =
            "7a87e15a99ccbc5e5877be66e1e4ecae0a581adcafa0cce1a5569f49909e968e";
        constexpr uint32_t GlbMagic = 0x46546c67u;
        constexpr uint32_t JsonChunkType = 0x4e4f534au;
        constexpr uint32_t BinChunkType = 0x004e4942u;
        constexpr uint32_t FloatComponent = 5126u;
        constexpr uint32_t UnsignedByteComponent = 5121u;
        constexpr uint32_t UnsignedShortComponent = 5123u;
        constexpr uint32_t UnsignedIntComponent = 5125u;

        /** Describes one validated accessor over the GLB BIN chunk. */
        struct MichelleAccessorView final
        {
            const uint8_t *bytes = nullptr;
            size_t byteCount = 0u;
            size_t count = 0u;
            size_t stride = 0u;
            uint32_t componentType = 0u;
            uint32_t componentCount = 0u;
            bool normalized = false;
        };

        /** Stores mutable transforms while evaluating one animation time. */
        struct MichellePoseNode final
        {
            glm::vec3 translation = glm::vec3(0.0f);
            glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scale = glm::vec3(1.0f);
        };

        /** Reads one complete bounded Michelle asset. */
        eastl::vector<uint8_t> readMichelleBytes(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open the pinned Michelle GLB.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("Pinned Michelle GLB has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete Michelle GLB.");
            }
            return bytes;
        }

        /** Calculates one lowercase SHA-256 string for a bounded asset. */
        eastl::string calculateMichelleSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char Digits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(Digits[value >> 4u]);
                result.push_back(Digits[value & 15u]);
            }
            return result;
        }

        /** Reads one little-endian scalar from a validated byte range. */
        template <typename Value>
        Value readMichelleScalar(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset + sizeof(Value) > bytes.size())
            {
                throw std::runtime_error("Michelle GLB scalar access is out of bounds.");
            }
            Value value = {};
            std::memcpy(&value, bytes.data() + offset, sizeof(Value));
            return value;
        }

        /** Returns the byte width of one supported accessor component. */
        size_t michelleComponentByteCount(uint32_t componentType)
        {
            switch (componentType)
            {
                case UnsignedByteComponent: return 1u;
                case UnsignedShortComponent: return 2u;
                case UnsignedIntComponent:
                case FloatComponent: return 4u;
                default:
                    throw std::runtime_error(
                        "Michelle GLB uses an unsupported component type.");
            }
        }

        /** Returns the scalar count for one supported accessor shape. */
        uint32_t michelleAccessorComponentCount(const std::string &type)
        {
            if (type == "SCALAR") return 1u;
            if (type == "VEC2") return 2u;
            if (type == "VEC3") return 3u;
            if (type == "VEC4") return 4u;
            if (type == "MAT4") return 16u;
            throw std::runtime_error("Michelle GLB uses an unsupported accessor shape.");
        }

        /** Resolves one accessor into the pinned BIN chunk. */
        MichelleAccessorView makeMichelleAccessorView(
            const nlohmann::json &document,
            uint32_t accessorIndex,
            const eastl::vector<uint8_t> &bytes,
            size_t binStart,
            size_t binByteCount)
        {
            const auto &accessors = document.at("accessors");
            const auto &bufferViews = document.at("bufferViews");
            if (accessorIndex >= accessors.size())
            {
                throw std::runtime_error("Michelle accessor index is invalid.");
            }
            const auto &accessor = accessors.at(accessorIndex);
            if (!accessor.contains("bufferView"))
            {
                throw std::runtime_error("Michelle sparse accessors are not expected.");
            }
            const uint32_t bufferViewIndex = accessor.at("bufferView").get<uint32_t>();
            if (bufferViewIndex >= bufferViews.size())
            {
                throw std::runtime_error("Michelle buffer-view index is invalid.");
            }
            const auto &bufferView = bufferViews.at(bufferViewIndex);
            const uint32_t componentType = accessor.at("componentType").get<uint32_t>();
            const uint32_t componentCount = michelleAccessorComponentCount(
                accessor.at("type").get<std::string>());
            const size_t packedStride =
                michelleComponentByteCount(componentType) * componentCount;
            const size_t stride = bufferView.value("byteStride", packedStride);
            const size_t count = accessor.at("count").get<size_t>();
            const size_t byteOffset =
                bufferView.value("byteOffset", size_t(0u)) +
                accessor.value("byteOffset", size_t(0u));
            const size_t requiredBytes = count == 0u
                ? 0u
                : (count - 1u) * stride + packedStride;
            if (byteOffset + requiredBytes > binByteCount ||
                binStart + byteOffset + requiredBytes > bytes.size())
            {
                throw std::runtime_error("Michelle accessor exceeds the BIN chunk.");
            }
            return {
                .bytes = bytes.data() + binStart + byteOffset,
                .byteCount = requiredBytes,
                .count = count,
                .stride = stride,
                .componentType = componentType,
                .componentCount = componentCount,
                .normalized = accessor.value("normalized", false),
            };
        }

        /** Reads one floating accessor component with normalized integer support. */
        float readMichelleFloat(
            const MichelleAccessorView &view,
            size_t element,
            uint32_t component)
        {
            if (element >= view.count || component >= view.componentCount)
            {
                throw std::runtime_error("Michelle float accessor read is out of bounds.");
            }
            const uint8_t *source = view.bytes + element * view.stride +
                component * michelleComponentByteCount(view.componentType);
            if (view.componentType == FloatComponent)
            {
                float value = 0.0f;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            if (view.componentType == UnsignedByteComponent)
            {
                return view.normalized ? float(*source) / 255.0f : float(*source);
            }
            if (view.componentType == UnsignedShortComponent)
            {
                uint16_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return view.normalized ? float(value) / 65535.0f : float(value);
            }
            throw std::runtime_error("Michelle float accessor type is unsupported.");
        }

        /** Reads one unsigned accessor component. */
        uint32_t readMichelleUnsigned(
            const MichelleAccessorView &view,
            size_t element,
            uint32_t component)
        {
            if (element >= view.count || component >= view.componentCount)
            {
                throw std::runtime_error("Michelle integer accessor read is out of bounds.");
            }
            const uint8_t *source = view.bytes + element * view.stride +
                component * michelleComponentByteCount(view.componentType);
            if (view.componentType == UnsignedByteComponent) return *source;
            if (view.componentType == UnsignedShortComponent)
            {
                uint16_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            if (view.componentType == UnsignedIntComponent)
            {
                uint32_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            throw std::runtime_error("Michelle integer accessor type is unsupported.");
        }

        /** Reads up to four floating components into one vector. */
        glm::vec4 readMichelleVector(
            const MichelleAccessorView &view,
            size_t element,
            float fourthDefault)
        {
            glm::vec4 value(0.0f, 0.0f, 0.0f, fourthDefault);
            const uint32_t count = eastl::min(view.componentCount, 4u);
            for (uint32_t component = 0u; component < count; ++component)
            {
                value[component] = readMichelleFloat(view, element, component);
            }
            return value;
        }

        /** Reads one column-major four-by-four matrix. */
        glm::mat4 readMichelleMatrix(
            const MichelleAccessorView &view,
            size_t element)
        {
            if (view.componentCount != 16u || view.componentType != FloatComponent)
            {
                throw std::runtime_error("Michelle matrix accessor is not MAT4 float.");
            }
            glm::mat4 value(1.0f);
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    value[column][row] = readMichelleFloat(
                        view, element, column * 4u + row);
                }
            }
            return value;
        }

        /** Reads one optional glTF vector or returns its deterministic default. */
        glm::vec3 readMichelleJsonVec3(
            const nlohmann::json &node,
            const char *name,
            const glm::vec3 &defaultValue)
        {
            if (!node.contains(name)) return defaultValue;
            const auto &value = node.at(name);
            return glm::vec3(value.at(0u).get<float>(),
                             value.at(1u).get<float>(),
                             value.at(2u).get<float>());
        }

        /** Reads one optional glTF quaternion or returns its identity default. */
        glm::quat readMichelleJsonQuaternion(const nlohmann::json &node)
        {
            if (!node.contains("rotation"))
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            const auto &value = node.at("rotation");
            return glm::normalize(glm::quat(
                value.at(3u).get<float>(), value.at(0u).get<float>(),
                value.at(1u).get<float>(), value.at(2u).get<float>()));
        }

        /** Composes one Three-compatible local transform. */
        glm::mat4 composeMichelleTransform(const MichellePoseNode &node)
        {
            return glm::translate(glm::mat4(1.0f), node.translation) *
                glm::toMat4(node.rotation) *
                glm::scale(glm::mat4(1.0f), node.scale);
        }

        /** Computes one world matrix after recursively resolving its parent. */
        const glm::mat4 &computeMichelleWorldMatrix(
            uint32_t nodeIndex,
            const MichelleGlbAsset &asset,
            const eastl::vector<MichellePoseNode> &pose,
            eastl::vector<glm::mat4> &worldMatrices,
            eastl::vector<uint8_t> &states)
        {
            if (nodeIndex >= asset.nodes.size())
            {
                throw std::runtime_error("Michelle pose node index is invalid.");
            }
            if (states[nodeIndex] == 2u) return worldMatrices[nodeIndex];
            if (states[nodeIndex] == 1u)
            {
                throw std::runtime_error("Michelle node hierarchy contains a cycle.");
            }
            states[nodeIndex] = 1u;
            const glm::mat4 local = composeMichelleTransform(pose[nodeIndex]);
            const int32_t parent = asset.nodes[nodeIndex].parent;
            worldMatrices[nodeIndex] = parent < 0
                ? local
                : computeMichelleWorldMatrix(
                    static_cast<uint32_t>(parent), asset, pose,
                    worldMatrices, states) * local;
            states[nodeIndex] = 2u;
            return worldMatrices[nodeIndex];
        }

        /** Finds interpolation keys on the shared repeating time grid. */
        void findMichelleAnimationKeys(
            const MichelleGlbAsset &asset,
            float timeSeconds,
            size_t &lowerKey,
            size_t &upperKey,
            float &alpha)
        {
            if (asset.animationTimes.empty() || asset.animationDuration <= 0.0f)
            {
                throw std::invalid_argument("Michelle animation time grid is empty.");
            }
            float time = std::fmod(timeSeconds, asset.animationDuration);
            if (time < 0.0f) time += asset.animationDuration;
            if (time <= asset.animationTimes.front())
            {
                lowerKey = 0u;
                upperKey = 0u;
                alpha = 0.0f;
                return;
            }
            upperKey = 1u;
            while (upperKey + 1u < asset.animationTimes.size() &&
                   time >= asset.animationTimes[upperKey])
            {
                ++upperKey;
            }
            lowerKey = upperKey - 1u;
            const float lowerTime = asset.animationTimes[lowerKey];
            const float upperTime = asset.animationTimes[upperKey];
            alpha = (time - lowerTime) / (upperTime - lowerTime);
        }
    } // namespace

    MichelleGlbAsset loadMichelleGlbAsset(
        const std::filesystem::path &path)
    {
        const eastl::vector<uint8_t> bytes = readMichelleBytes(path);
        MichelleGlbAsset asset;
        asset.sha256 = calculateMichelleSha256(bytes);
        if (asset.sha256 != MichelleGlbSha256 ||
            readMichelleScalar<uint32_t>(bytes, 0u) != GlbMagic ||
            readMichelleScalar<uint32_t>(bytes, 4u) != 2u ||
            readMichelleScalar<uint32_t>(bytes, 8u) != bytes.size())
        {
            throw std::runtime_error("Michelle GLB differs from the r185 asset lock.");
        }
        const uint32_t jsonByteCount = readMichelleScalar<uint32_t>(bytes, 12u);
        if (readMichelleScalar<uint32_t>(bytes, 16u) != JsonChunkType ||
            20u + size_t(jsonByteCount) + 8u > bytes.size())
        {
            throw std::runtime_error("Michelle GLB JSON chunk is invalid.");
        }
        const size_t binHeader = 20u + size_t(jsonByteCount);
        const uint32_t binByteCount = readMichelleScalar<uint32_t>(bytes, binHeader);
        if (readMichelleScalar<uint32_t>(bytes, binHeader + 4u) != BinChunkType ||
            binHeader + 8u + size_t(binByteCount) > bytes.size())
        {
            throw std::runtime_error("Michelle GLB BIN chunk is invalid.");
        }
        const size_t binStart = binHeader + 8u;
        const nlohmann::json document = nlohmann::json::parse(
            reinterpret_cast<const char *>(bytes.data() + 20u),
            reinterpret_cast<const char *>(bytes.data() + 20u + jsonByteCount));

        const auto &primitive = document.at("meshes").at(0u).at("primitives").at(0u);
        const auto &attributes = primitive.at("attributes");
        const MichelleAccessorView positions = makeMichelleAccessorView(
            document, attributes.at("POSITION").get<uint32_t>(),
            bytes, binStart, binByteCount);
        const MichelleAccessorView normals = makeMichelleAccessorView(
            document, attributes.at("NORMAL").get<uint32_t>(),
            bytes, binStart, binByteCount);
        const MichelleAccessorView uvs = makeMichelleAccessorView(
            document, attributes.at("TEXCOORD_0").get<uint32_t>(),
            bytes, binStart, binByteCount);
        const MichelleAccessorView joints = makeMichelleAccessorView(
            document, attributes.at("JOINTS_0").get<uint32_t>(),
            bytes, binStart, binByteCount);
        const MichelleAccessorView weights = makeMichelleAccessorView(
            document, attributes.at("WEIGHTS_0").get<uint32_t>(),
            bytes, binStart, binByteCount);
        if (positions.count != normals.count || positions.count != uvs.count ||
            positions.count != joints.count || positions.count != weights.count)
        {
            throw std::runtime_error("Michelle vertex accessor counts differ.");
        }
        asset.vertices.resize(positions.count);
        for (size_t vertex = 0u; vertex < positions.count; ++vertex)
        {
            MichelleGlbVertex value;
            value.position = readMichelleVector(positions, vertex, 1.0f);
            value.normal = readMichelleVector(normals, vertex, 0.0f);
            value.uv = readMichelleVector(uvs, vertex, 0.0f);
            for (uint32_t component = 0u; component < 4u; ++component)
            {
                value.joints[component] = readMichelleUnsigned(
                    joints, vertex, component);
                value.weights[component] = readMichelleFloat(
                    weights, vertex, component);
            }
            asset.vertices[vertex] = value;
        }

        const MichelleAccessorView indices = makeMichelleAccessorView(
            document, primitive.at("indices").get<uint32_t>(),
            bytes, binStart, binByteCount);
        asset.indices.resize(indices.count);
        for (size_t index = 0u; index < indices.count; ++index)
        {
            asset.indices[index] = readMichelleUnsigned(indices, index, 0u);
        }

        const auto &jsonNodes = document.at("nodes");
        asset.nodes.resize(jsonNodes.size());
        for (size_t nodeIndex = 0u; nodeIndex < jsonNodes.size(); ++nodeIndex)
        {
            const auto &node = jsonNodes.at(nodeIndex);
            asset.nodes[nodeIndex].translation = readMichelleJsonVec3(
                node, "translation", glm::vec3(0.0f));
            asset.nodes[nodeIndex].rotation = readMichelleJsonQuaternion(node);
            asset.nodes[nodeIndex].scale = readMichelleJsonVec3(
                node, "scale", glm::vec3(1.0f));
            if (node.value("mesh", uint32_t(-1)) == 0u)
            {
                asset.meshNodeIndex = static_cast<uint32_t>(nodeIndex);
            }
            if (node.contains("children"))
            {
                for (const auto &child : node.at("children"))
                {
                    const uint32_t childIndex = child.get<uint32_t>();
                    if (childIndex >= asset.nodes.size() ||
                        asset.nodes[childIndex].parent >= 0)
                    {
                        throw std::runtime_error(
                            "Michelle node parent relation is invalid.");
                    }
                    asset.nodes[childIndex].parent =
                        static_cast<int32_t>(nodeIndex);
                }
            }
        }

        const auto &skin = document.at("skins").at(0u);
        for (const auto &joint : skin.at("joints"))
        {
            asset.jointNodeIndices.push_back(joint.get<uint32_t>());
        }
        const MichelleAccessorView inverseBinds = makeMichelleAccessorView(
            document, skin.at("inverseBindMatrices").get<uint32_t>(),
            bytes, binStart, binByteCount);
        if (inverseBinds.count != asset.jointNodeIndices.size())
        {
            throw std::runtime_error("Michelle skin joint count is inconsistent.");
        }
        asset.inverseBindMatrices.resize(inverseBinds.count);
        for (size_t joint = 0u; joint < inverseBinds.count; ++joint)
        {
            asset.inverseBindMatrices[joint] =
                readMichelleMatrix(inverseBinds, joint);
        }

        const auto &animation = document.at("animations").at(0u);
        const auto &samplers = animation.at("samplers");
        const auto &channels = animation.at("channels");
        uint32_t sharedTimeAccessor = std::numeric_limits<uint32_t>::max();
        for (const auto &channel : channels)
        {
            const uint32_t samplerIndex = channel.at("sampler").get<uint32_t>();
            const auto &sampler = samplers.at(samplerIndex);
            if (sampler.value("interpolation", std::string("LINEAR")) != "LINEAR")
            {
                throw std::runtime_error("Michelle animation interpolation is not linear.");
            }
            const uint32_t inputAccessor = sampler.at("input").get<uint32_t>();
            if (sharedTimeAccessor == std::numeric_limits<uint32_t>::max())
            {
                sharedTimeAccessor = inputAccessor;
            }
            else if (sharedTimeAccessor != inputAccessor)
            {
                throw std::runtime_error("Michelle animation does not share one time grid.");
            }
            const MichelleAccessorView output = makeMichelleAccessorView(
                document, sampler.at("output").get<uint32_t>(),
                bytes, binStart, binByteCount);
            MichelleAnimationChannel decodedChannel;
            decodedChannel.nodeIndex =
                channel.at("target").at("node").get<uint32_t>();
            const std::string pathName =
                channel.at("target").at("path").get<std::string>();
            if (pathName == "translation")
            {
                decodedChannel.path = MichelleAnimationPath::Translation;
            }
            else if (pathName == "rotation")
            {
                decodedChannel.path = MichelleAnimationPath::Rotation;
            }
            else if (pathName == "scale")
            {
                decodedChannel.path = MichelleAnimationPath::Scale;
            }
            else
            {
                throw std::runtime_error("Michelle animation path is unsupported.");
            }
            decodedChannel.values.resize(output.count);
            for (size_t key = 0u; key < output.count; ++key)
            {
                decodedChannel.values[key] = readMichelleVector(
                    output, key,
                    decodedChannel.path == MichelleAnimationPath::Rotation
                        ? 1.0f
                        : 0.0f);
            }
            asset.animationChannels.push_back(eastl::move(decodedChannel));
        }
        const MichelleAccessorView times = makeMichelleAccessorView(
            document, sharedTimeAccessor, bytes, binStart, binByteCount);
        asset.animationTimes.resize(times.count);
        for (size_t key = 0u; key < times.count; ++key)
        {
            asset.animationTimes[key] = readMichelleFloat(times, key, 0u);
        }
        asset.animationDuration = asset.animationTimes.back();
        for (const MichelleAnimationChannel &channel : asset.animationChannels)
        {
            if (channel.values.size() != asset.animationTimes.size())
            {
                throw std::runtime_error("Michelle animation channel length differs.");
            }
        }

        const auto &images = document.at("images");
        const auto &bufferViews = document.at("bufferViews");
        asset.textures.reserve(images.size());
        for (const auto &image : images)
        {
            if (image.value("mimeType", std::string()) != "image/png")
            {
                throw std::runtime_error("Michelle embedded texture is not PNG.");
            }
            const auto &bufferView = bufferViews.at(
                image.at("bufferView").get<uint32_t>());
            const size_t offset = bufferView.value("byteOffset", size_t(0u));
            const size_t byteCount = bufferView.at("byteLength").get<size_t>();
            if (offset + byteCount > binByteCount)
            {
                throw std::runtime_error("Michelle embedded texture exceeds BIN data.");
            }
            eastl::vector<uint8_t> encoded(
                bytes.begin() + static_cast<ptrdiff_t>(binStart + offset),
                bytes.begin() + static_cast<ptrdiff_t>(binStart + offset + byteCount));
            asset.textures.push_back(decodeImageIoTextureRgba8(
                encoded, "Michelle embedded PNG"));
        }

        if (asset.vertices.size() != 16340u || asset.indices.size() != 84318u ||
            asset.nodes.size() != 67u || asset.jointNodeIndices.size() != 65u ||
            asset.animationTimes.size() != 547u ||
            asset.animationChannels.size() != 195u || asset.textures.size() != 4u ||
            asset.meshNodeIndex != 65u)
        {
            throw std::runtime_error("Michelle GLB semantic counts differ from r185.");
        }
        return asset;
    }

    void sampleMichelleSkinPalette(
        const MichelleGlbAsset &asset,
        float timeSeconds,
        eastl::vector<glm::mat4> &palette)
    {
        if (asset.nodes.empty() ||
            asset.inverseBindMatrices.size() != asset.jointNodeIndices.size() ||
            asset.meshNodeIndex >= asset.nodes.size())
        {
            throw std::invalid_argument("Michelle skin asset is inconsistent.");
        }
        size_t lowerKey = 0u;
        size_t upperKey = 0u;
        float alpha = 0.0f;
        findMichelleAnimationKeys(
            asset, timeSeconds, lowerKey, upperKey, alpha);
        eastl::vector<MichellePoseNode> pose(asset.nodes.size());
        for (size_t node = 0u; node < asset.nodes.size(); ++node)
        {
            pose[node].translation = asset.nodes[node].translation;
            pose[node].rotation = asset.nodes[node].rotation;
            pose[node].scale = asset.nodes[node].scale;
        }
        for (const MichelleAnimationChannel &channel : asset.animationChannels)
        {
            const glm::vec4 lower = channel.values[lowerKey];
            const glm::vec4 upper = channel.values[upperKey];
            if (channel.path == MichelleAnimationPath::Translation)
            {
                pose[channel.nodeIndex].translation = glm::mix(
                    glm::vec3(lower), glm::vec3(upper), alpha);
            }
            else if (channel.path == MichelleAnimationPath::Scale)
            {
                pose[channel.nodeIndex].scale = glm::mix(
                    glm::vec3(lower), glm::vec3(upper), alpha);
            }
            else
            {
                const glm::quat lowerRotation(
                    lower.w, lower.x, lower.y, lower.z);
                const glm::quat upperRotation(
                    upper.w, upper.x, upper.y, upper.z);
                pose[channel.nodeIndex].rotation = glm::normalize(glm::slerp(
                    lowerRotation, upperRotation, alpha));
            }
        }
        eastl::vector<glm::mat4> worldMatrices(
            asset.nodes.size(), glm::mat4(1.0f));
        eastl::vector<uint8_t> states(asset.nodes.size(), 0u);
        for (uint32_t node = 0u; node < asset.nodes.size(); ++node)
        {
            (void)computeMichelleWorldMatrix(
                node, asset, pose, worldMatrices, states);
        }
        const glm::mat4 meshWorld = worldMatrices[asset.meshNodeIndex];
        const glm::mat4 inverseMeshWorld = glm::inverse(meshWorld);
        palette.resize(asset.jointNodeIndices.size());
        for (size_t joint = 0u; joint < asset.jointNodeIndices.size(); ++joint)
        {
            palette[joint] = inverseMeshWorld *
                worldMatrices[asset.jointNodeIndices[joint]] *
                asset.inverseBindMatrices[joint];
        }
    }

    void sampleMichelleSkinnedVertices(
        const MichelleGlbAsset &asset,
        float timeSeconds,
        eastl::vector<glm::vec4> &positions,
        eastl::vector<glm::vec4> &normals)
    {
        eastl::vector<glm::mat4> palette;
        sampleMichelleSkinPalette(asset, timeSeconds, palette);
        positions.resize(asset.vertices.size());
        normals.resize(asset.vertices.size());
        for (size_t vertexIndex = 0u;
             vertexIndex < asset.vertices.size(); ++vertexIndex)
        {
            const MichelleGlbVertex &vertex = asset.vertices[vertexIndex];
            glm::vec4 position(0.0f);
            glm::vec4 normal(0.0f);
            for (uint32_t influence = 0u; influence < 4u; ++influence)
            {
                const uint32_t joint = vertex.joints[influence];
                if (joint >= palette.size())
                {
                    throw std::runtime_error(
                        "Michelle vertex references an invalid skin joint.");
                }
                position += palette[joint] * vertex.position *
                    vertex.weights[influence];
                normal += palette[joint] *
                    glm::vec4(glm::vec3(vertex.normal), 0.0f) *
                    vertex.weights[influence];
            }
            positions[vertexIndex] = position;
            normals[vertexIndex] = glm::vec4(
                glm::normalize(glm::vec3(normal)), 0.0f);
        }
    }
} // namespace GVM::ThreeSamples
