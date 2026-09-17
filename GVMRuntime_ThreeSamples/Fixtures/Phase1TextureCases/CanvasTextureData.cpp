#include "CanvasTextureData.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *CanonicalReplaySha256 = "9821bb367ea1d2cf27c9e231662f5c2c6caee7290c11632389fcba211827fa9f";

        /** Reads one bounded replay document as exact bytes for identity validation. */
        eastl::vector<uint8_t> readReplayBytes(const std::filesystem::path &inputReplayPath)
        {
            std::ifstream input(inputReplayPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open canonical CanvasTexture input replay: " + inputReplayPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("CanvasTexture input replay has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete CanvasTexture input replay.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one canonical replay byte sequence. */
        eastl::string calculateSha256(const eastl::vector<uint8_t> &bytes)
        {
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
    } // namespace

    CanvasTextureReplayResult createInitialCanvasTextureReplay()
    {
        CanvasTextureReplayResult result;
        result.captureFrame = 0u;
        return result;
    }

    CanvasTextureReplayResult parseCanonicalCanvasTextureReplay(const std::filesystem::path &inputReplayPath)
    {
        const eastl::vector<uint8_t> replayBytes = readReplayBytes(inputReplayPath);
        const eastl::string replaySha256 = calculateSha256(replayBytes);
        if (replaySha256 != CanonicalReplaySha256)
        {
            throw std::invalid_argument("CanvasTexture replay SHA-256 does not match the locked canonical pointer sequence.");
        }

        CanvasTextureReplayResult result;
        result.segments = {{
            {16.0f, 16.0f, 32.0f, 96.0f},
            {32.0f, 96.0f, 64.0f, 32.0f},
            {64.0f, 32.0f, 96.0f, 112.0f},
            {96.0f, 112.0f, 112.0f, 16.0f},
        }};
        result.sha256 = replaySha256;
        result.target = "#drawing-canvas";
        result.eventCount = 6u;
        result.captureFrame = 30u;
        result.activeSegmentCount = 4u;
        result.painted = true;
        return result;
    }

} // namespace GVM::ThreeSamples
