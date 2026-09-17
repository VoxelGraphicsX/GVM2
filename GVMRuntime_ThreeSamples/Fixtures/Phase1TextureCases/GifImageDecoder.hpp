#pragma once

#include "RgbaImageData.hpp"

#include <EASTL/vector.h>

#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one top-down extended-linear-sRGB image as packed IEEE binary16 RGBA pixels. */
    struct Rgba16FloatImageData final
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<uint16_t> pixels;
    };

    /** Decodes the first composited GIF frame as deterministic top-down RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodeGifRgba8(const std::filesystem::path &assetPath);

    /** Decodes one JPEG asset as deterministic top-down sRGB RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodeJpegRgba8(const std::filesystem::path &assetPath);

    /** Decodes one in-memory JPEG payload as deterministic top-down sRGB RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodeJpegRgba8(
        const eastl::vector<uint8_t> &encodedBytes);

    /** Decodes an UltraHDR JPEG and applies its gain map into extended-linear RGBA16F. */
    [[nodiscard]] Rgba16FloatImageData decodeUltraHdrRgba16Float(
        const std::filesystem::path &assetPath);

    /** Decodes one PNG asset as deterministic top-down sRGB RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodePngRgba8(const std::filesystem::path &assetPath);

    /** Decodes one TIFF asset as deterministic top-down sRGB RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodeTiffRgba8(const std::filesystem::path &assetPath);

    /** Decodes one sample-private ImageIO-compatible texture container to top-down RGBA8 pixels. */
    [[nodiscard]] RgbaImageData decodeImageIoTextureRgba8(
        const std::filesystem::path &assetPath);

    /** Decodes one in-memory sample-private ImageIO-compatible texture container. */
    [[nodiscard]] RgbaImageData decodeImageIoTextureRgba8(
        const eastl::vector<uint8_t> &encodedBytes,
        const char *assetDescription);

    /** Decodes a noninterlaced RGBA8 PNG while preserving RGB bytes under transparent pixels. */
    [[nodiscard]] RgbaImageData decodeStraightPngRgba8(
        const std::filesystem::path &assetPath);

    /** Builds a complete linear-filtered sRGB mip chain without relying on automatic GPU mip generation. */
    [[nodiscard]] eastl::vector<RgbaImageData> buildSrgbMipChain(
        const RgbaImageData &baseImage);

    /** Builds a raw UNorm box-filtered mip chain without applying an sRGB transfer. */
    [[nodiscard]] eastl::vector<RgbaImageData> buildUnormMipChain(
        const RgbaImageData &baseImage);
} // namespace GVM::ThreeSamples
