#include "DdsImageDecoder.hpp"

#include "GifImageDecoder.hpp"
#include "Ktx1ImageDecoder.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t DdsMagic = 0x20534444u;
        constexpr uint32_t DdpfFourCc = 0x4u;
        constexpr uint32_t DdpfRgb = 0x40u;
        constexpr uint32_t DdsCaps2CubeMap = 0x200u;
        constexpr uint32_t FourCcDxt1 = 0x31545844u;
        constexpr uint32_t FourCcDxt3 = 0x33545844u;
        constexpr uint32_t FourCcDxt5 = 0x35545844u;
        constexpr uint32_t FourCcDx10 = 0x30315844u;
        constexpr uint32_t DxgiBc6hUnsignedFloat = 95u;
        constexpr uint32_t DxgiBc6hSignedFloat = 96u;

        /** Reads one little-endian word from a validated DDS byte range. */
        uint32_t readDdsLe32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "DDS payload contains a truncated uint32 field.");
            }
            return uint32_t(bytes[offset]) |
                uint32_t(bytes[offset + 1u]) << 8u |
                uint32_t(bytes[offset + 2u]) << 16u |
                uint32_t(bytes[offset + 3u]) << 24u;
        }

        /** Reads one complete DDS asset without implicit path fallback. */
        eastl::vector<uint8_t> readDdsBytes(
            const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open DDS asset: " + assetPath.string());
            }
            const std::streamsize byteCount = input.tellg();
            if (byteCount < 0 ||
                static_cast<uint64_t>(byteCount) >
                    std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error("DDS asset has an invalid byte size.");
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete DDS asset.");
            }
            return bytes;
        }

        /** Writes one clipped texel into a decoded DDS surface. */
        void writeDdsTexel(
            RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            const eastl::array<uint8_t, 4u> &color)
        {
            if (x >= image.width || y >= image.height) return;
            const size_t target =
                (size_t(y) * image.width + x) * 4u;
            eastl::copy(
                color.begin(), color.end(),
                image.pixels.begin() + target);
        }

        /** Decodes explicit four-bit alpha plus BC1 four-color blocks. */
        RgbaImageData decodeDdsBc2(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height)
        {
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 16u)
            {
                throw std::runtime_error(
                    "DDS BC2 surface has an invalid block byte count.");
            }
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks +
                        (size_t(blockY) * blocksX + blockX) * 16u;
                    const RgbaImageData colors = decodeBc1Rgba8(
                        block + 8u, 8u, 4u, 4u, true);
                    uint64_t alphaSelectors = 0u;
                    for (uint32_t byte = 0u; byte < 8u; ++byte)
                    {
                        alphaSelectors |=
                            uint64_t(block[byte]) << (8u * byte);
                    }
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t local = y * 4u + x;
                            const size_t source = size_t(local) * 4u;
                            writeDdsTexel(
                                image, blockX * 4u + x,
                                blockY * 4u + y,
                                {colors.pixels[source],
                                 colors.pixels[source + 1u],
                                 colors.pixels[source + 2u],
                                 static_cast<uint8_t>(
                                     ((alphaSelectors >> (4u * local)) &
                                      15u) * 17u)});
                        }
                    }
                }
            }
            return image;
        }

        /** Expands one masked integer channel to an eight-bit normalized value. */
        uint8_t decodeDdsMaskedChannel(
            uint32_t packed,
            uint32_t mask,
            uint8_t fallback)
        {
            if (mask == 0u) return fallback;
            uint32_t shift = 0u;
            while (((mask >> shift) & 1u) == 0u) ++shift;
            const uint32_t maximum = mask >> shift;
            const uint32_t value = (packed & mask) >> shift;
            return static_cast<uint8_t>(
                (value * 255u + maximum / 2u) / maximum);
        }

        /** Decodes one bounded 24- or 32-bit RGB DDS surface. */
        RgbaImageData decodeDdsRgb(
            const uint8_t *source,
            size_t byteCount,
            uint32_t width,
            uint32_t height,
            uint32_t bitCount,
            uint32_t redMask,
            uint32_t greenMask,
            uint32_t blueMask,
            uint32_t alphaMask)
        {
            const uint32_t bytesPerPixel = bitCount / 8u;
            if ((bitCount != 24u && bitCount != 32u) ||
                byteCount != size_t(width) * height * bytesPerPixel)
            {
                throw std::runtime_error(
                    "DDS RGB surface has an unsupported storage layout.");
            }
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (size_t texel = 0u;
                 texel < size_t(width) * height;
                 ++texel)
            {
                uint32_t packed = 0u;
                for (uint32_t byte = 0u; byte < bytesPerPixel; ++byte)
                {
                    packed |= uint32_t(
                        source[texel * bytesPerPixel + byte]) <<
                        (8u * byte);
                }
                image.pixels[texel * 4u] =
                    decodeDdsMaskedChannel(packed, redMask, 0u);
                image.pixels[texel * 4u + 1u] =
                    decodeDdsMaskedChannel(packed, greenMask, 0u);
                image.pixels[texel * 4u + 2u] =
                    decodeDdsMaskedChannel(packed, blueMask, 0u);
                image.pixels[texel * 4u + 3u] =
                    decodeDdsMaskedChannel(packed, alphaMask, 255u);
            }
            return image;
        }

        /** Returns the exact encoded byte count of one DDS mip surface. */
        size_t ddsSurfaceByteCount(
            uint32_t fourCc,
            uint32_t pixelFlags,
            uint32_t bitCount,
            uint32_t width,
            uint32_t height)
        {
            if ((pixelFlags & DdpfFourCc) != 0u)
            {
                const size_t blockCount =
                    size_t(eastl::max(1u, (width + 3u) / 4u)) *
                    eastl::max(1u, (height + 3u) / 4u);
                if (fourCc == FourCcDxt1) return blockCount * 8u;
                if (fourCc == FourCcDxt3 || fourCc == FourCcDxt5)
                    return blockCount * 16u;
            }
            if ((pixelFlags & DdpfRgb) != 0u &&
                (bitCount == 24u || bitCount == 32u))
            {
                return size_t(width) * height * (bitCount / 8u);
            }
            throw std::runtime_error(
                "DDS surface uses an unsupported encoded format.");
        }

        /** Converts DDS encoded row order to the backend upload convention. */
        void flipDdsRows(RgbaImageData &image)
        {
            const size_t rowBytes = size_t(image.width) * 4u;
            for (uint32_t y = 0u; y < image.height / 2u; ++y)
            {
                const size_t upper = size_t(y) * rowBytes;
                const size_t lower =
                    size_t(image.height - 1u - y) * rowBytes;
                for (size_t byte = 0u; byte < rowBytes; ++byte)
                {
                    eastl::swap(
                        image.pixels[upper + byte],
                        image.pixels[lower + byte]);
                }
            }
        }
    } // namespace

    DdsRgba8Texture decodeDdsRgba8(
        const std::filesystem::path &assetPath)
    {
        const eastl::vector<uint8_t> bytes = readDdsBytes(assetPath);
        if (bytes.size() < 128u || readDdsLe32(bytes, 0u) != DdsMagic ||
            readDdsLe32(bytes, 4u) != 124u ||
            readDdsLe32(bytes, 76u) != 32u)
        {
            throw std::runtime_error(
                "DDS asset does not contain a complete legacy header.");
        }
        DdsRgba8Texture texture;
        texture.height = readDdsLe32(bytes, 12u);
        texture.width = readDdsLe32(bytes, 16u);
        texture.authoredMipCount = eastl::max(1u, readDdsLe32(bytes, 28u));
        const uint32_t pixelFlags = readDdsLe32(bytes, 80u);
        texture.fourCc = readDdsLe32(bytes, 84u);
        const uint32_t bitCount = readDdsLe32(bytes, 88u);
        const uint32_t redMask = readDdsLe32(bytes, 92u);
        const uint32_t greenMask = readDdsLe32(bytes, 96u);
        const uint32_t blueMask = readDdsLe32(bytes, 100u);
        const uint32_t alphaMask = readDdsLe32(bytes, 104u);
        texture.cube =
            (readDdsLe32(bytes, 112u) & DdsCaps2CubeMap) != 0u;
        size_t cursor = 128u;
        if (texture.fourCc == FourCcDx10)
        {
            if (bytes.size() < 148u)
            {
                throw std::runtime_error("DDS DX10 header is truncated.");
            }
            texture.dxgiFormat = readDdsLe32(bytes, 128u);
            cursor = 148u;
            texture.floatingPoint =
                texture.dxgiFormat == DxgiBc6hUnsignedFloat ||
                texture.dxgiFormat == DxgiBc6hSignedFloat;
            if (!texture.floatingPoint || texture.cube)
            {
                throw std::runtime_error(
                    "DDS DX10 format is outside the frozen r185 contract.");
            }
            RgbaImageData base = decodeImageIoTextureRgba8(assetPath);
            if (base.width != texture.width || base.height != texture.height)
            {
                throw std::runtime_error(
                    "ImageIO DDS dimensions differ from the DX10 header.");
            }
            DdsRgba8Face face;
            face.mipLevels = buildUnormMipChain(base);
            face.mipLevels.resize(eastl::min(
                texture.authoredMipCount,
                static_cast<uint32_t>(face.mipLevels.size())));
            texture.faces.push_back(eastl::move(face));
            return texture;
        }

        const uint32_t faceCount = texture.cube ? 6u : 1u;
        texture.faces.resize(faceCount);
        for (uint32_t face = 0u; face < faceCount; ++face)
        {
            uint32_t width = texture.width;
            uint32_t height = texture.height;
            DdsRgba8Face &decodedFace = texture.faces[face];
            decodedFace.mipLevels.reserve(texture.authoredMipCount);
            for (uint32_t mip = 0u;
                 mip < texture.authoredMipCount;
                 ++mip)
            {
                const size_t byteCount = ddsSurfaceByteCount(
                    texture.fourCc, pixelFlags, bitCount, width, height);
                if (cursor > bytes.size() ||
                    byteCount > bytes.size() - cursor)
                {
                    throw std::runtime_error(
                        "DDS mip surface escapes the container.");
                }
                RgbaImageData image;
                if (texture.fourCc == FourCcDxt1)
                {
                    image = decodeBc1Rgba8(
                        bytes.data() + cursor, byteCount,
                        width, height, false);
                }
                else if (texture.fourCc == FourCcDxt3)
                {
                    image = decodeDdsBc2(
                        bytes.data() + cursor, byteCount,
                        width, height);
                }
                else if (texture.fourCc == FourCcDxt5)
                {
                    image = decodeBc3Rgba8(
                        bytes.data() + cursor, byteCount,
                        width, height);
                }
                else
                {
                    image = decodeDdsRgb(
                        bytes.data() + cursor, byteCount,
                        width, height, bitCount,
                        redMask, greenMask, blueMask, alphaMask);
                }
                if (!texture.cube) flipDdsRows(image);
                decodedFace.mipLevels.push_back(eastl::move(image));
                cursor += byteCount;
                width = eastl::max(1u, width / 2u);
                height = eastl::max(1u, height / 2u);
            }
        }
        return texture;
    }
} // namespace GVM::ThreeSamples
