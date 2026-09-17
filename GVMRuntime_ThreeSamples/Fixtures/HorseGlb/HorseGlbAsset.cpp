#include "HorseGlbAsset.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *HorseGlbSha256 =
            "bebaa4a60ba373317e25bf20f049f26ad0f5c86d4731ab67d46eb8c93c920947";
        constexpr uint32_t HorseVertexCount = 796u;
        constexpr uint32_t HorseIndexCount = 2952u;
        constexpr uint32_t HorseTargetCount = 15u;
        constexpr uint32_t HorseAnimationKeyCount = 16u;
        constexpr uint32_t HorseJsonOffset = 20u;
        constexpr uint32_t HorsePositionOffset = 0u;
        constexpr uint32_t HorseColorOffset = 9552u;
        constexpr uint32_t HorseFirstMorphOffset = 25472u;
        constexpr uint32_t HorseMorphByteCount = 9552u;
        constexpr uint32_t HorseIndexOffset = 168752u;
        constexpr uint32_t HorseAnimationTimeOffset = 174656u;
        constexpr uint32_t HorseAnimationWeightOffset = 174720u;

        /** Reads one complete bounded binary asset. */
        eastl::vector<uint8_t> readHorseBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open the pinned Horse GLB.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("Pinned Horse GLB has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete Horse GLB.");
            }
            return bytes;
        }

        /** Calculates one lowercase SHA-256 string for a bounded asset. */
        eastl::string calculateHorseSha256(const eastl::vector<uint8_t> &bytes)
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
        Value readHorseScalar(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset + sizeof(Value) > bytes.size())
            {
                throw std::runtime_error("Horse GLB scalar access is out of bounds.");
            }
            Value value = {};
            std::memcpy(&value, bytes.data() + offset, sizeof(Value));
            return value;
        }
    } // namespace

    HorseGlbAsset loadHorseGlbAsset(const std::filesystem::path &path)
    {
        const eastl::vector<uint8_t> bytes = readHorseBytes(path);
        HorseGlbAsset asset;
        asset.sha256 = calculateHorseSha256(bytes);
        if (asset.sha256 != HorseGlbSha256 ||
            readHorseScalar<uint32_t>(bytes, 0u) != 0x46546c67u ||
            readHorseScalar<uint32_t>(bytes, 4u) != 2u)
        {
            throw std::runtime_error("Horse GLB differs from the r185 asset lock.");
        }

        const uint32_t jsonByteCount = readHorseScalar<uint32_t>(bytes, 12u);
        const size_t binStart = HorseJsonOffset + size_t(jsonByteCount) + 8u;
        const size_t requiredBinBytes =
            HorseAnimationWeightOffset +
            size_t(HorseAnimationKeyCount) * HorseTargetCount * sizeof(float);
        if (binStart + requiredBinBytes > bytes.size())
        {
            throw std::runtime_error("Horse GLB BIN layout is incomplete.");
        }

        asset.vertices.resize(HorseVertexCount);
        for (uint32_t vertex = 0u; vertex < HorseVertexCount; ++vertex)
        {
            const size_t position =
                binStart + HorsePositionOffset + size_t(vertex) * 12u;
            const size_t color =
                binStart + HorseColorOffset + size_t(vertex) * 12u;
            asset.vertices[vertex] = {
                glm::vec4(readHorseScalar<float>(bytes, position),
                          readHorseScalar<float>(bytes, position + 4u),
                          readHorseScalar<float>(bytes, position + 8u), 1.0f),
                glm::vec4(readHorseScalar<float>(bytes, color),
                          readHorseScalar<float>(bytes, color + 4u),
                          readHorseScalar<float>(bytes, color + 8u), 1.0f),
            };
        }

        asset.morphTargetCount = HorseTargetCount;
        asset.morphPositions.resize(HorseVertexCount * HorseTargetCount);
        for (uint32_t target = 0u; target < HorseTargetCount; ++target)
        {
            for (uint32_t vertex = 0u; vertex < HorseVertexCount; ++vertex)
            {
                const size_t position =
                    binStart + HorseFirstMorphOffset +
                    size_t(target) * HorseMorphByteCount + size_t(vertex) * 12u;
                asset.morphPositions[target * HorseVertexCount + vertex] = glm::vec4(
                    readHorseScalar<float>(bytes, position),
                    readHorseScalar<float>(bytes, position + 4u),
                    readHorseScalar<float>(bytes, position + 8u), 0.0f);
            }
        }

        asset.indices.resize(HorseIndexCount);
        for (uint32_t index = 0u; index < HorseIndexCount; ++index)
        {
            asset.indices[index] = readHorseScalar<uint16_t>(
                bytes, binStart + HorseIndexOffset + size_t(index) * 2u);
        }

        asset.animationTimes.resize(HorseAnimationKeyCount);
        for (uint32_t key = 0u; key < HorseAnimationKeyCount; ++key)
        {
            asset.animationTimes[key] = readHorseScalar<float>(
                bytes, binStart + HorseAnimationTimeOffset + size_t(key) * 4u);
        }
        asset.animationWeights.resize(HorseAnimationKeyCount * HorseTargetCount);
        for (uint32_t value = 0u;
             value < HorseAnimationKeyCount * HorseTargetCount;
             ++value)
        {
            asset.animationWeights[value] = readHorseScalar<float>(
                bytes, binStart + HorseAnimationWeightOffset + size_t(value) * 4u);
        }
        if (asset.animationTimes.front() != 0.0f ||
            asset.animationTimes.back() != 1.5f)
        {
            throw std::runtime_error("Horse animation time range is invalid.");
        }
        return asset;
    }

    void sampleHorseMorphWeights(
        const HorseGlbAsset &asset,
        float timeSeconds,
        eastl::vector<float> &weights)
    {
        if (asset.morphTargetCount == 0u || asset.animationTimes.size() < 2u ||
            asset.animationWeights.size() !=
                asset.animationTimes.size() * asset.morphTargetCount)
        {
            throw std::invalid_argument("Horse animation arrays are inconsistent.");
        }
        const float duration = asset.animationTimes.back();
        float wrappedTime = std::fmod(timeSeconds, duration);
        if (wrappedTime < 0.0f) wrappedTime += duration;
        size_t upperKey = 1u;
        while (upperKey + 1u < asset.animationTimes.size() &&
               wrappedTime >= asset.animationTimes[upperKey])
        {
            ++upperKey;
        }
        const size_t lowerKey = upperKey - 1u;
        const float lowerTime = asset.animationTimes[lowerKey];
        const float upperTime = asset.animationTimes[upperKey];
        const float alpha = (wrappedTime - lowerTime) / (upperTime - lowerTime);
        weights.resize(asset.morphTargetCount);
        for (uint32_t target = 0u; target < asset.morphTargetCount; ++target)
        {
            const float lower = asset.animationWeights[
                lowerKey * asset.morphTargetCount + target];
            const float upper = asset.animationWeights[
                upperKey * asset.morphTargetCount + target];
            weights[target] = lower + (upper - lower) * alpha;
        }
    }
} // namespace GVM::ThreeSamples
