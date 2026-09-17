#include "Ktx1ImageDecoder.hpp"

#include "GifImageDecoder.hpp"
#include "ThirdParty/PowerVr/PvrTextureDecompressor.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <fstream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr eastl::array<uint8_t, 12u> Ktx1Identifier = {
            0xabu, 0x4bu, 0x54u, 0x58u, 0x20u, 0x31u,
            0x31u, 0xbbu, 0x0du, 0x0au, 0x1au, 0x0au};
        constexpr uint32_t Ktx1LittleEndian = 0x04030201u;
        constexpr uint32_t GlCompressedRgbS3tcDxt1 = 0x83f0u;
        constexpr uint32_t GlCompressedRgbaS3tcDxt5 = 0x83f3u;
        constexpr uint32_t GlCompressedRgbPvrtc4 = 0x8c00u;
        constexpr uint32_t GlCompressedRgbPvrtc2 = 0x8c01u;
        constexpr uint32_t GlCompressedRgbaPvrtc4 = 0x8c02u;
        constexpr uint32_t GlCompressedRgbaPvrtc2 = 0x8c03u;
        constexpr uint32_t GlEtc1Rgb8 = 0x8d64u;
        constexpr uint32_t GlCompressedRgRgtc2 = 0x8dbdu;
        constexpr uint32_t GlCompressedRg11Eac = 0x9272u;
        constexpr uint32_t GlRgba = 0x1908u;

        /** Reads one little-endian unsigned word from a bounded KTX1 payload. */
        uint32_t readLe32(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error("KTX1 payload contains a truncated uint32 field.");
            }
            return uint32_t(bytes[offset]) |
                (uint32_t(bytes[offset + 1u]) << 8u) |
                (uint32_t(bytes[offset + 2u]) << 16u) |
                (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Reads one complete texture container without implicit path fallback. */
        eastl::vector<uint8_t> readKtx1Bytes(const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open KTX1 asset: " + assetPath.string());
            }
            const std::streamsize size = input.tellg();
            if (size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error("KTX1 asset has an invalid byte size.");
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input) throw std::runtime_error("Could not read the complete KTX1 asset.");
            return bytes;
        }

        /** Expands one RGB565 endpoint into exact eight-bit channels. */
        eastl::array<uint8_t, 4u> expandRgb565(uint16_t value)
        {
            const uint32_t red = (value >> 11u) & 31u;
            const uint32_t green = (value >> 5u) & 63u;
            const uint32_t blue = value & 31u;
            return {
                static_cast<uint8_t>((red << 3u) | (red >> 2u)),
                static_cast<uint8_t>((green << 2u) | (green >> 4u)),
                static_cast<uint8_t>((blue << 3u) | (blue >> 2u)),
                255u};
        }

        /** Writes one clipped RGBA texel into a decoded mip image. */
        void writeTexel(
            RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            const eastl::array<uint8_t, 4u> &color)
        {
            if (x >= image.width || y >= image.height) return;
            const size_t offset = (size_t(y) * image.width + x) * 4u;
            eastl::copy(color.begin(), color.end(), image.pixels.begin() + offset);
        }

        /** Decodes BC1 color blocks, optionally forcing the BC3 four-color rule. */
        RgbaImageData decodeBc1(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height,
            bool forceFourColors)
        {
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 8u)
            {
                throw std::runtime_error("KTX1 BC1 mip has an invalid block byte count.");
            }
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks + (size_t(blockY) * blocksX + blockX) * 8u;
                    const uint16_t endpoint0 = uint16_t(block[0u]) | uint16_t(block[1u]) << 8u;
                    const uint16_t endpoint1 = uint16_t(block[2u]) | uint16_t(block[3u]) << 8u;
                    eastl::array<eastl::array<uint8_t, 4u>, 4u> colors = {};
                    colors[0u] = expandRgb565(endpoint0);
                    colors[1u] = expandRgb565(endpoint1);
                    if (endpoint0 > endpoint1 || forceFourColors)
                    {
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            colors[2u][channel] = static_cast<uint8_t>(
                                (2u * colors[0u][channel] + colors[1u][channel] + 1u) / 3u);
                            colors[3u][channel] = static_cast<uint8_t>(
                                (colors[0u][channel] + 2u * colors[1u][channel] + 1u) / 3u);
                        }
                        colors[2u][3u] = colors[3u][3u] = 255u;
                    }
                    else
                    {
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            colors[2u][channel] = static_cast<uint8_t>(
                                (uint32_t(colors[0u][channel]) + colors[1u][channel]) / 2u);
                        }
                        colors[2u][3u] = 255u;
                        colors[3u] = {0u, 0u, 0u, 255u};
                    }
                    const uint32_t indices = uint32_t(block[4u]) |
                        uint32_t(block[5u]) << 8u |
                        uint32_t(block[6u]) << 16u |
                        uint32_t(block[7u]) << 24u;
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t selector = (indices >> (2u * (4u * y + x))) & 3u;
                            writeTexel(image, blockX * 4u + x, blockY * 4u + y, colors[selector]);
                        }
                    }
                }
            }
            return image;
        }

        /** Decodes BC3 alpha and color blocks into straight RGBA8 texels. */
        RgbaImageData decodeBc3(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height)
        {
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 16u)
            {
                throw std::runtime_error("KTX1 BC3 mip has an invalid block byte count.");
            }
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks + (size_t(blockY) * blocksX + blockX) * 16u;
                    eastl::array<uint8_t, 8u> alpha = {block[0u], block[1u]};
                    if (alpha[0u] > alpha[1u])
                    {
                        for (uint32_t i = 1u; i <= 6u; ++i)
                            alpha[i + 1u] = static_cast<uint8_t>(((7u - i) * alpha[0u] + i * alpha[1u] + 3u) / 7u);
                    }
                    else
                    {
                        for (uint32_t i = 1u; i <= 4u; ++i)
                            alpha[i + 1u] = static_cast<uint8_t>(((5u - i) * alpha[0u] + i * alpha[1u] + 2u) / 5u);
                        alpha[6u] = 0u;
                        alpha[7u] = 255u;
                    }
                    uint64_t alphaIndices = 0u;
                    for (uint32_t byte = 0u; byte < 6u; ++byte)
                        alphaIndices |= uint64_t(block[2u + byte]) << (8u * byte);
                    const RgbaImageData colorBlock = decodeBc1(block + 8u, 8u, 4u, 4u, true);
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t local = 4u * y + x;
                            const size_t source = size_t(local) * 4u;
                            eastl::array<uint8_t, 4u> color = {
                                colorBlock.pixels[source], colorBlock.pixels[source + 1u],
                                colorBlock.pixels[source + 2u],
                                alpha[(alphaIndices >> (3u * local)) & 7u]};
                            writeTexel(image, blockX * 4u + x, blockY * 4u + y, color);
                        }
                    }
                }
            }
            return image;
        }

        /** Decodes one BC4 endpoint block into sixteen unsigned channel bytes. */
        eastl::array<uint8_t, 16u> decodeBc4Channel(const uint8_t *block)
        {
            eastl::array<uint8_t, 8u> values = {block[0u], block[1u]};
            if (values[0u] > values[1u])
            {
                for (uint32_t index = 1u; index <= 6u; ++index)
                {
                    values[index + 1u] = static_cast<uint8_t>(
                        ((7u - index) * values[0u] +
                         index * values[1u] + 3u) /
                        7u);
                }
            }
            else
            {
                for (uint32_t index = 1u; index <= 4u; ++index)
                {
                    values[index + 1u] = static_cast<uint8_t>(
                        ((5u - index) * values[0u] +
                         index * values[1u] + 2u) /
                        5u);
                }
                values[6u] = 0u;
                values[7u] = 255u;
            }
            uint64_t selectors = 0u;
            for (uint32_t byte = 0u; byte < 6u; ++byte)
                selectors |= uint64_t(block[2u + byte]) << (8u * byte);
            eastl::array<uint8_t, 16u> channel = {};
            for (uint32_t texel = 0u; texel < 16u; ++texel)
                channel[texel] = values[(selectors >> (3u * texel)) & 7u];
            return channel;
        }

        /** Decodes BC5/RGTC2 blocks to the unsigned RG sampling contract. */
        RgbaImageData decodeBc5(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height)
        {
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 16u)
                throw std::runtime_error("KTX1 BC5 mip has an invalid block byte count.");
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks +
                        (size_t(blockY) * blocksX + blockX) * 16u;
                    const eastl::array<uint8_t, 16u> red =
                        decodeBc4Channel(block);
                    const eastl::array<uint8_t, 16u> green =
                        decodeBc4Channel(block + 8u);
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t texel = y * 4u + x;
                            writeTexel(
                                image,
                                blockX * 4u + x,
                                blockY * 4u + y,
                                {red[texel], green[texel], 0u, 255u});
                        }
                    }
                }
            }
            return image;
        }

        /** Decodes one unsigned EAC11 block into sixteen normalized bytes. */
        eastl::array<uint8_t, 16u> decodeEac11Channel(const uint8_t *block)
        {
            static constexpr int32_t Modifiers[16u][8u] = {
                {-3,-6,-9,-15,2,5,8,14}, {-3,-7,-10,-13,2,6,9,12},
                {-2,-5,-8,-13,1,4,7,12}, {-2,-4,-6,-13,1,3,5,12},
                {-3,-6,-8,-12,2,5,7,11}, {-3,-7,-9,-11,2,6,8,10},
                {-4,-7,-8,-11,3,6,7,10}, {-3,-5,-8,-11,2,4,7,10},
                {-2,-6,-8,-10,1,5,7,9}, {-2,-5,-8,-10,1,4,7,9},
                {-2,-4,-8,-10,1,3,7,9}, {-2,-5,-7,-10,1,4,6,9},
                {-3,-4,-7,-10,2,3,6,9}, {-1,-2,-3,-10,0,1,2,9},
                {-4,-6,-8,-9,3,5,7,8}, {-3,-5,-7,-9,2,4,6,8}};
            const int32_t base = int32_t(block[0u]) * 8 + 4;
            const int32_t multiplier = int32_t(block[1u] >> 4u);
            const uint32_t table = block[1u] & 15u;
            uint64_t selectors = 0u;
            for (uint32_t byte = 0u; byte < 6u; ++byte)
                selectors = (selectors << 8u) | block[2u + byte];
            eastl::array<uint8_t, 16u> channel = {};
            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const uint32_t texel = x * 4u + y;
                    const uint32_t selector = static_cast<uint32_t>(
                        (selectors >> (45u - 3u * texel)) & 7u);
                    const int32_t value = eastl::clamp(
                        base + Modifiers[table][selector] * multiplier * 8,
                        0,
                        2047);
                    channel[y * 4u + x] = static_cast<uint8_t>(
                        (value * 255 + 1023) / 2047);
                }
            }
            return channel;
        }

        /** Decodes unsigned EAC RG11 blocks to the WebGL two-channel contract. */
        RgbaImageData decodeEacRg11(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height)
        {
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 16u)
                throw std::runtime_error("KTX1 EAC RG11 mip has an invalid block byte count.");
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks +
                        (size_t(blockY) * blocksX + blockX) * 16u;
                    const eastl::array<uint8_t, 16u> red =
                        decodeEac11Channel(block);
                    const eastl::array<uint8_t, 16u> green =
                        decodeEac11Channel(block + 8u);
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t texel = y * 4u + x;
                            writeTexel(
                                image,
                                blockX * 4u + x,
                                blockY * 4u + y,
                                {red[texel], green[texel], 0u, 255u});
                        }
                    }
                }
            }
            return image;
        }

        /** Decodes one PVRTC1 mip with the format owner's reference algorithm. */
        RgbaImageData decodePvrtc1(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height,
            bool twoBitsPerPixel)
        {
            const uint32_t encodedWidth = eastl::max(
                width, twoBitsPerPixel ? 16u : 8u);
            const uint32_t encodedHeight = eastl::max(height, 8u);
            const size_t expectedBytes =
                size_t(encodedWidth) * encodedHeight /
                (twoBitsPerPixel ? 4u : 2u);
            if (byteCount != expectedBytes)
            {
                throw std::runtime_error(
                    "KTX1 PVRTC1 mip has an invalid block byte count.");
            }
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            pvr::PVRTDecompressPVRTC(
                blocks,
                twoBitsPerPixel ? 1u : 0u,
                width,
                height,
                image.pixels.data());
            return image;
        }

        /** Sign-extends one ETC1 three-bit differential channel. */
        int32_t signExtendEtc3(uint32_t value)
        {
            return (value & 4u) != 0u ? int32_t(value) - 8 : int32_t(value);
        }

        /** Expands one ETC1 base channel to eight bits. */
        uint8_t expandEtcChannel(uint32_t value, bool fiveBits)
        {
            return fiveBits
                ? static_cast<uint8_t>((value << 3u) | (value >> 2u))
                : static_cast<uint8_t>((value << 4u) | value);
        }

        /** Decodes ETC1 individual and differential blocks to opaque RGBA8. */
        RgbaImageData decodeEtc1(
            const uint8_t *blocks,
            size_t byteCount,
            uint32_t width,
            uint32_t height)
        {
            static constexpr int32_t Modifiers[8u][4u] = {
                {2, 8, -2, -8}, {5, 17, -5, -17}, {9, 29, -9, -29},
                {13, 42, -13, -42}, {18, 60, -18, -60},
                {24, 80, -24, -80}, {33, 106, -33, -106},
                {47, 183, -47, -183}};
            const uint32_t blocksX = eastl::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = eastl::max(1u, (height + 3u) / 4u);
            if (byteCount != size_t(blocksX) * blocksY * 8u)
                throw std::runtime_error("KTX1 ETC1 mip has an invalid block byte count.");
            RgbaImageData image{width, height, {}};
            image.pixels.resize(size_t(width) * height * 4u);
            for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
            {
                for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
                {
                    const uint8_t *block = blocks + (size_t(blockY) * blocksX + blockX) * 8u;
                    const uint32_t high = uint32_t(block[0u]) << 24u |
                        uint32_t(block[1u]) << 16u | uint32_t(block[2u]) << 8u | block[3u];
                    const uint32_t low = uint32_t(block[4u]) << 24u |
                        uint32_t(block[5u]) << 16u | uint32_t(block[6u]) << 8u | block[7u];
                    const bool differential = (high & 2u) != 0u;
                    const bool flip = (high & 1u) != 0u;
                    int32_t red[2u] = {}, green[2u] = {}, blue[2u] = {};
                    if (differential)
                    {
                        red[0u] = int32_t((high >> 27u) & 31u);
                        green[0u] = int32_t((high >> 19u) & 31u);
                        blue[0u] = int32_t((high >> 11u) & 31u);
                        red[1u] = red[0u] + signExtendEtc3((high >> 24u) & 7u);
                        green[1u] = green[0u] + signExtendEtc3((high >> 16u) & 7u);
                        blue[1u] = blue[0u] + signExtendEtc3((high >> 8u) & 7u);
                    }
                    else
                    {
                        red[0u] = int32_t((high >> 28u) & 15u);
                        red[1u] = int32_t((high >> 24u) & 15u);
                        green[0u] = int32_t((high >> 20u) & 15u);
                        green[1u] = int32_t((high >> 16u) & 15u);
                        blue[0u] = int32_t((high >> 12u) & 15u);
                        blue[1u] = int32_t((high >> 8u) & 15u);
                    }
                    const uint32_t table[2u] = {(high >> 5u) & 7u, (high >> 2u) & 7u};
                    for (uint32_t y = 0u; y < 4u; ++y)
                    {
                        for (uint32_t x = 0u; x < 4u; ++x)
                        {
                            const uint32_t subblock = flip ? (y >= 2u) : (x >= 2u);
                            const uint32_t bit = x * 4u + y;
                            const uint32_t selector = ((low >> (bit + 15u)) & 2u) |
                                ((low >> bit) & 1u);
                            const int32_t modifier = Modifiers[table[subblock]][selector];
                            eastl::array<uint8_t, 4u> color = {
                                static_cast<uint8_t>(eastl::clamp<int32_t>(
                                    int32_t(expandEtcChannel(uint32_t(red[subblock]), differential)) + modifier, 0, 255)),
                                static_cast<uint8_t>(eastl::clamp<int32_t>(
                                    int32_t(expandEtcChannel(uint32_t(green[subblock]), differential)) + modifier, 0, 255)),
                                static_cast<uint8_t>(eastl::clamp<int32_t>(
                                    int32_t(expandEtcChannel(uint32_t(blue[subblock]), differential)) + modifier, 0, 255)),
                                255u};
                            writeTexel(image, blockX * 4u + x, blockY * 4u + y, color);
                        }
                    }
                }
            }
            return image;
        }

        /** Converts bottom-origin compressed block rows to top-down RGBA storage. */
        void flipKtxImageVertically(RgbaImageData &image)
        {
            const size_t rowBytes = size_t(image.width) * 4u;
            eastl::vector<uint8_t> temporaryRow(rowBytes);
            for (uint32_t y = 0u; y < image.height / 2u; ++y)
            {
                uint8_t *top = image.pixels.data() + size_t(y) * rowBytes;
                uint8_t *bottom = image.pixels.data() +
                    size_t(image.height - 1u - y) * rowBytes;
                eastl::copy(top, top + rowBytes, temporaryRow.begin());
                eastl::copy(bottom, bottom + rowBytes, top);
                eastl::copy(temporaryRow.begin(), temporaryRow.end(), bottom);
            }
        }

        /** Converts ImageIO's associated-alpha RGBA output back to straight alpha. */
        void unpremultiplyKtxImage(RgbaImageData &image)
        {
            for (size_t offset = 0u; offset < image.pixels.size(); offset += 4u)
            {
                const uint32_t alpha = image.pixels[offset + 3u];
                if (alpha == 0u)
                {
                    image.pixels[offset] = 0u;
                    image.pixels[offset + 1u] = 0u;
                    image.pixels[offset + 2u] = 0u;
                    continue;
                }
                if (alpha == 255u) continue;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    image.pixels[offset + channel] = static_cast<uint8_t>(
                        eastl::min(255u,
                            (uint32_t(image.pixels[offset + channel]) * 255u +
                                alpha / 2u) /
                                alpha));
                }
            }
        }

        /** Writes one little-endian unsigned word into a mutable KTX1 header. */
        void writeLe32(
            eastl::vector<uint8_t> &bytes,
            size_t offset,
            uint32_t value)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "KTX1 destination contains a truncated uint32 field.");
            }
            bytes[offset] = static_cast<uint8_t>(value);
            bytes[offset + 1u] = static_cast<uint8_t>(value >> 8u);
            bytes[offset + 2u] = static_cast<uint8_t>(value >> 16u);
            bytes[offset + 3u] = static_cast<uint8_t>(value >> 24u);
        }

        /** Repackages one authored compressed mip while retaining KTX metadata. */
        eastl::vector<uint8_t> buildSingleMipKtx1(
            const eastl::vector<uint8_t> &source,
            uint32_t keyValueBytes,
            uint32_t width,
            uint32_t height,
            const uint8_t *mipBytes,
            uint32_t mipByteCount)
        {
            const size_t metadataEnd = 64u + keyValueBytes;
            if (metadataEnd > source.size() ||
                mipByteCount > source.size() ||
                mipBytes < source.data() ||
                mipBytes > source.data() + source.size() - mipByteCount)
            {
                throw std::runtime_error(
                    "KTX1 single-mip repack received invalid source ranges.");
            }
            eastl::vector<uint8_t> encoded;
            encoded.insert(encoded.end(), source.begin(), source.begin() + metadataEnd);
            writeLe32(encoded, 36u, width);
            writeLe32(encoded, 40u, height);
            writeLe32(encoded, 56u, 1u);
            const size_t sizeOffset = encoded.size();
            encoded.resize(sizeOffset + 4u);
            writeLe32(encoded, sizeOffset, mipByteCount);
            encoded.insert(encoded.end(), mipBytes, mipBytes + mipByteCount);
            while ((encoded.size() & 3u) != 0u) encoded.push_back(0u);
            return encoded;
        }

        /** Decodes one repacked authored mip through ImageIO's path-backed KTX codec. */
        RgbaImageData decodeRepackedKtxMip(
            const eastl::vector<uint8_t> &encoded,
            const std::filesystem::path &sourcePath,
            uint32_t mipIndex)
        {
            const std::filesystem::path temporaryPath =
                std::filesystem::temp_directory_path() /
                ("gvm-three-" + sourcePath.stem().string() + "-" +
                    std::to_string(static_cast<uint64_t>(getpid())) + "-" +
                    std::to_string(mipIndex) + ".ktx");
            try
            {
                std::ofstream output(
                    temporaryPath,
                    std::ios::binary | std::ios::trunc);
                output.write(
                    reinterpret_cast<const char *>(encoded.data()),
                    static_cast<std::streamsize>(encoded.size()));
                output.close();
                if (!output)
                {
                    throw std::runtime_error(
                        "Could not materialize one authored KTX1 mip for CPU decode.");
                }
                RgbaImageData image = decodeImageIoTextureRgba8(temporaryPath);
                std::filesystem::remove(temporaryPath);
                return image;
            }
            catch (...)
            {
                std::error_code removeError;
                std::filesystem::remove(temporaryPath, removeError);
                throw;
            }
        }

    } // namespace

    RgbaImageData decodeBc1Rgba8(
        const uint8_t *blocks,
        size_t byteCount,
        uint32_t width,
        uint32_t height,
        bool forceFourColors)
    {
        return decodeBc1(
            blocks, byteCount, width, height, forceFourColors);
    }

    RgbaImageData decodeBc3Rgba8(
        const uint8_t *blocks,
        size_t byteCount,
        uint32_t width,
        uint32_t height)
    {
        return decodeBc3(blocks, byteCount, width, height);
    }

    Ktx1Rgba8Texture decodeKtx1Rgba8(
        const std::filesystem::path &assetPath)
    {
        const eastl::vector<uint8_t> bytes = readKtx1Bytes(assetPath);
        if (bytes.size() < 64u ||
            !eastl::equal(Ktx1Identifier.begin(), Ktx1Identifier.end(), bytes.begin()))
            throw std::runtime_error("Texture asset is not a complete KTX1 container.");
        if (readLe32(bytes, 12u) != Ktx1LittleEndian)
            throw std::runtime_error("Big-endian KTX1 textures are outside the r185 sample contract.");
        const uint32_t internalFormat = readLe32(bytes, 28u);
        const uint32_t baseInternalFormat = readLe32(bytes, 32u);
        uint32_t width = readLe32(bytes, 36u);
        uint32_t height = readLe32(bytes, 40u);
        const uint32_t faceCount = readLe32(bytes, 52u);
        const uint32_t mipCount = eastl::max(1u, readLe32(bytes, 56u));
        const uint32_t keyValueBytes = readLe32(bytes, 60u);
        if (width == 0u || height == 0u || faceCount != 1u)
            throw std::runtime_error("KTX1 decoder requires one non-empty 2D texture face.");
        size_t cursor = 64u + keyValueBytes;
        Ktx1Rgba8Texture texture;
        texture.internalFormat = internalFormat;
        texture.colorData = internalFormat != GlCompressedRgbPvrtc4 &&
            internalFormat != GlCompressedRgbPvrtc2 &&
            internalFormat != GlCompressedRgbaPvrtc4 &&
            internalFormat != GlCompressedRgbaPvrtc2 &&
            internalFormat != GlEtc1Rgb8 &&
            internalFormat != GlCompressedRgRgtc2 &&
            internalFormat != GlCompressedRg11Eac;
        if (internalFormat != GlCompressedRgbPvrtc4 &&
            internalFormat != GlCompressedRgbPvrtc2 &&
            internalFormat != GlCompressedRgbaPvrtc4 &&
            internalFormat != GlCompressedRgbaPvrtc2 &&
            internalFormat != GlCompressedRgbS3tcDxt1 &&
            internalFormat != GlCompressedRgbaS3tcDxt5 &&
            internalFormat != GlEtc1Rgb8 &&
            internalFormat != GlCompressedRgRgtc2 &&
            internalFormat != GlCompressedRg11Eac)
        {
            RgbaImageData baseImage = decodeImageIoTextureRgba8(assetPath);
            if (baseInternalFormat == GlRgba) unpremultiplyKtxImage(baseImage);
            texture.mipLevels = texture.colorData
                ? buildSrgbMipChain(baseImage)
                : buildUnormMipChain(baseImage);
            if (texture.mipLevels.size() < mipCount)
            {
                throw std::runtime_error(
                    "ImageIO texture decode produced too few explicit mip levels.");
            }
            texture.mipLevels.resize(mipCount);
            return texture;
        }
        texture.mipLevels.reserve(mipCount);
        for (uint32_t mip = 0u; mip < mipCount; ++mip)
        {
            const uint32_t imageSize = readLe32(bytes, cursor);
            cursor += 4u;
            if (cursor > bytes.size() || imageSize > bytes.size() - cursor)
                throw std::runtime_error("KTX1 mip payload escapes the container.");
            if (internalFormat == GlCompressedRgbPvrtc4 ||
                internalFormat == GlCompressedRgbPvrtc2 ||
                internalFormat == GlCompressedRgbaPvrtc4 ||
                internalFormat == GlCompressedRgbaPvrtc2 ||
                internalFormat == GlCompressedRgbS3tcDxt1 ||
                internalFormat == GlCompressedRgbaS3tcDxt5 ||
                internalFormat == GlEtc1Rgb8 ||
                internalFormat == GlCompressedRgRgtc2 ||
                internalFormat == GlCompressedRg11Eac)
            {
                RgbaImageData decoded;
                if (internalFormat == GlCompressedRgbPvrtc4 ||
                    internalFormat == GlCompressedRgbaPvrtc4)
                    decoded = decodePvrtc1(
                        bytes.data() + cursor, imageSize, width, height, false);
                else if (internalFormat == GlCompressedRgbPvrtc2 ||
                    internalFormat == GlCompressedRgbaPvrtc2)
                    decoded = decodePvrtc1(
                        bytes.data() + cursor, imageSize, width, height, true);
                else if (internalFormat == GlCompressedRgbS3tcDxt1)
                    decoded = decodeBc1(bytes.data() + cursor, imageSize, width, height, false);
                else if (internalFormat == GlCompressedRgbaS3tcDxt5)
                    decoded = decodeBc3(bytes.data() + cursor, imageSize, width, height);
                else if (internalFormat == GlEtc1Rgb8)
                    decoded = decodeEtc1(bytes.data() + cursor, imageSize, width, height);
                else if (internalFormat == GlCompressedRgRgtc2)
                    decoded = decodeBc5(bytes.data() + cursor, imageSize, width, height);
                else
                    decoded = decodeEacRg11(bytes.data() + cursor, imageSize, width, height);
                flipKtxImageVertically(decoded);
                texture.mipLevels.push_back(eastl::move(decoded));
            }
            else
            {
                const eastl::vector<uint8_t> encodedMip = buildSingleMipKtx1(
                    bytes,
                    keyValueBytes,
                    width,
                    height,
                    bytes.data() + cursor,
                    imageSize);
                try
                {
                    texture.mipLevels.push_back(decodeRepackedKtxMip(
                        encodedMip,
                        assetPath,
                        mip));
                }
                catch (const std::runtime_error &)
                {
                    if (texture.mipLevels.empty()) throw;
                    const eastl::vector<RgbaImageData> fallbackMips =
                        texture.colorData
                        ? buildSrgbMipChain(texture.mipLevels.back())
                        : buildUnormMipChain(texture.mipLevels.back());
                    if (fallbackMips.size() < 2u)
                    {
                        throw std::runtime_error(
                            "ImageIO rejected an authored KTX1 mip without a valid CPU fallback.");
                    }
                    texture.mipLevels.push_back(fallbackMips[1u]);
                }
            }
            cursor += imageSize;
            cursor = (cursor + 3u) & ~size_t(3u);
            width = eastl::max(1u, width / 2u);
            height = eastl::max(1u, height / 2u);
        }
        return texture;
    }
} // namespace GVM::ThreeSamples
