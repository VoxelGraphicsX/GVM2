#include "PvrImageDecoder.hpp"

#include "ThirdParty/PowerVr/PvrTextureDecompressor.hpp"

#include <EASTL/algorithm.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t PvrV3Magic = 0x03525650u;
        constexpr uint32_t PvrV2Magic = 0x21525650u;

        /** Reads one bounded little-endian unsigned word. */
        uint32_t readPvrWord(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
                throw std::runtime_error("PVR payload contains a truncated uint32 field.");
            return uint32_t(bytes[offset]) |
                uint32_t(bytes[offset + 1u]) << 8u |
                uint32_t(bytes[offset + 2u]) << 16u |
                uint32_t(bytes[offset + 3u]) << 24u;
        }

        /** Reads one complete PVR asset without path or network fallbacks. */
        eastl::vector<uint8_t> readPvrBytes(const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open PVR asset: " + assetPath.string());
            const std::streamsize size = input.tellg();
            if (size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max())
                throw std::runtime_error("PVR asset has an invalid byte size.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input) throw std::runtime_error("Could not read the complete PVR asset.");
            return bytes;
        }

        /** Converts one decoded bottom-origin PVRTC face to top-down RGBA storage. */
        void flipPvrImageVertically(RgbaImageData &image)
        {
            const size_t rowBytes = size_t(image.width) * 4u;
            eastl::vector<uint8_t> row(rowBytes);
            for (uint32_t y = 0u; y < image.height / 2u; ++y)
            {
                uint8_t *top = image.pixels.data() + size_t(y) * rowBytes;
                uint8_t *bottom = image.pixels.data() +
                    size_t(image.height - 1u - y) * rowBytes;
                eastl::copy(top, top + rowBytes, row.begin());
                eastl::copy(bottom, bottom + rowBytes, top);
                eastl::copy(row.begin(), row.end(), bottom);
            }
        }

        /** Decodes one bounded PVRTC1 surface using the format-owner reference path. */
        RgbaImageData decodePvrSurface(
            const uint8_t *payload,
            size_t byteCount,
            uint32_t width,
            uint32_t height,
            uint32_t bitsPerPixel)
        {
            const bool twoBpp = bitsPerPixel == 2u;
            const uint32_t encodedWidth = eastl::max(width, twoBpp ? 16u : 8u);
            const uint32_t encodedHeight = eastl::max(height, 8u);
            const size_t expectedBytes = size_t(encodedWidth) * encodedHeight /
                (twoBpp ? 4u : 2u);
            if (byteCount != expectedBytes)
                throw std::runtime_error("PVR mip surface byte count is inconsistent with PVRTC1 layout.");
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            pvr::PVRTDecompressPVRTC(
                payload, twoBpp ? 1u : 0u, width, height, image.pixels.data());
            flipPvrImageVertically(image);
            return image;
        }
    } // namespace

    PvrRgba8Texture decodePvrRgba8(const std::filesystem::path &assetPath)
    {
        const eastl::vector<uint8_t> bytes = readPvrBytes(assetPath);
        if (bytes.size() < 52u) throw std::runtime_error("PVR asset is smaller than its header.");
        PvrRgba8Texture texture;
        size_t cursor = 0u;
        if (readPvrWord(bytes, 0u) == PvrV3Magic)
        {
            const uint32_t pixelFormat = readPvrWord(bytes, 8u);
            if (pixelFormat > 3u) throw std::runtime_error("PVR v3 asset is not PVRTC1 RGB/RGBA.");
            texture.bitsPerPixel = pixelFormat < 2u ? 2u : 4u;
            texture.hasAlpha = (pixelFormat & 1u) != 0u;
            texture.height = readPvrWord(bytes, 24u);
            texture.width = readPvrWord(bytes, 28u);
            texture.faceCount = readPvrWord(bytes, 40u);
            texture.mipCount = readPvrWord(bytes, 44u);
            cursor = 52u + readPvrWord(bytes, 48u);
        }
        else if (readPvrWord(bytes, 44u) == PvrV2Magic)
        {
            const uint32_t flags = readPvrWord(bytes, 16u) & 0xffu;
            if (flags != 24u && flags != 25u)
                throw std::runtime_error("PVR v2 asset is not PVRTC1 2bpp/4bpp.");
            texture.bitsPerPixel = flags == 24u ? 2u : 4u;
            texture.hasAlpha = readPvrWord(bytes, 40u) != 0u;
            texture.height = readPvrWord(bytes, 4u);
            texture.width = readPvrWord(bytes, 8u);
            texture.mipCount = readPvrWord(bytes, 12u) + 1u;
            texture.faceCount = readPvrWord(bytes, 48u);
            cursor = readPvrWord(bytes, 0u);
        }
        else
        {
            throw std::runtime_error("Asset is neither a PVR v2 nor v3 texture.");
        }
        if (texture.width == 0u || texture.height == 0u ||
            texture.mipCount == 0u || (texture.faceCount != 1u && texture.faceCount != 6u))
            throw std::runtime_error("PVR texture declares an unsupported surface layout.");
        texture.faceMipImages.resize(size_t(texture.faceCount) * texture.mipCount);
        for (uint32_t mip = 0u; mip < texture.mipCount; ++mip)
        {
            const uint32_t width = eastl::max(1u, texture.width >> mip);
            const uint32_t height = eastl::max(1u, texture.height >> mip);
            const uint32_t encodedWidth = eastl::max(
                width, texture.bitsPerPixel == 2u ? 16u : 8u);
            const uint32_t encodedHeight = eastl::max(height, 8u);
            const size_t surfaceBytes = size_t(encodedWidth) * encodedHeight /
                (texture.bitsPerPixel == 2u ? 4u : 2u);
            for (uint32_t face = 0u; face < texture.faceCount; ++face)
            {
                if (cursor > bytes.size() || surfaceBytes > bytes.size() - cursor)
                    throw std::runtime_error("PVR mip surface escapes the asset payload.");
                texture.faceMipImages[size_t(face) * texture.mipCount + mip] =
                    decodePvrSurface(
                        bytes.data() + cursor, surfaceBytes, width, height,
                        texture.bitsPerPixel);
                cursor += surfaceBytes;
            }
        }
        if (cursor > bytes.size()) throw std::runtime_error("PVR decoder overran the asset payload.");
        return texture;
    }
} // namespace GVM::ThreeSamples
