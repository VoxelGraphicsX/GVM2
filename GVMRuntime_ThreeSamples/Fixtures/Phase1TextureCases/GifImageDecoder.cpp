#include "GifImageDecoder.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <compression.h>

#if defined(GVM_THREE_USE_LIBJPEG_TURBO)
#include <jpeglib.h>
#endif

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <cmath>
#if defined(GVM_THREE_USE_LIBJPEG_TURBO)
#include <csetjmp>
#include <cstdlib>
#endif
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t RgbaChannelCount = 4u;

        /** Stores the r185 gain-map fields required for HDR reconstruction. */
        struct UltraHdrGainMapMetadata final
        {
            double gainMapMin = 0.0;
            double gainMapMax = 1.0;
            double gamma = 1.0;
            double offsetSdr = 0.0;
            double offsetHdr = 0.0;
            double hdrCapacityMin = 0.0;
            double hdrCapacityMax = 1.0;
            bool baseRenditionIsHdr = false;
        };

#if defined(GVM_THREE_USE_LIBJPEG_TURBO)
        /** Owns libjpeg's bounded error jump and any in-flight JPEG output. */
        struct ChromeJpegErrorState final
        {
            jpeg_error_mgr base;
            std::jmp_buf jumpTarget;
            uint8_t *pixels = nullptr;
        };

        /** Converts one libjpeg fatal error into a bounded decoder jump. */
        void handleChromeJpegError(j_common_ptr decoder)
        {
            auto *state = reinterpret_cast<ChromeJpegErrorState *>(
                decoder->err);
            std::free(state->pixels);
            state->pixels = nullptr;
            std::longjmp(state->jumpTarget, 1);
        }

        /** Decodes one JPEG payload through Chrome-compatible libjpeg-turbo. */
        RgbaImageData decodeChromeJpegWithLibjpeg(
            const uint8_t *bytes,
            size_t byteCount)
        {
            jpeg_decompress_struct decoder = {};
            ChromeJpegErrorState errorState = {};
            decoder.err = jpeg_std_error(&errorState.base);
            errorState.base.error_exit = handleChromeJpegError;
            if (setjmp(errorState.jumpTarget) != 0)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error(
                    "libjpeg-turbo could not decode an UltraHDR JPEG layer.");
            }
            jpeg_create_decompress(&decoder);
            jpeg_mem_src(
                &decoder,
                bytes,
                static_cast<unsigned long>(byteCount));
            if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error(
                    "UltraHDR contains an invalid JPEG layer header.");
            }
#if defined(JCS_EXTENSIONS)
            decoder.out_color_space = JCS_EXT_RGBA;
#else
#error "Chrome-compatible UltraHDR decode requires libjpeg-turbo RGBA output."
#endif
            decoder.dct_method = JDCT_ISLOW;
            decoder.do_fancy_upsampling = TRUE;
            jpeg_start_decompress(&decoder);
            if (decoder.output_width == 0u ||
                decoder.output_height == 0u ||
                decoder.output_components != RgbaChannelCount)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error(
                    "UltraHDR JPEG layer has an unsupported output shape.");
            }
            const size_t rowBytes =
                size_t(decoder.output_width) * RgbaChannelCount;
            const size_t outputByteCount =
                rowBytes * size_t(decoder.output_height);
            errorState.pixels = static_cast<uint8_t *>(
                std::malloc(outputByteCount));
            if (errorState.pixels == nullptr)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::bad_alloc();
            }
            while (decoder.output_scanline < decoder.output_height)
            {
                JSAMPROW row = errorState.pixels +
                    size_t(decoder.output_scanline) * rowBytes;
                jpeg_read_scanlines(&decoder, &row, 1u);
            }
            const uint32_t width = decoder.output_width;
            const uint32_t height = decoder.output_height;
            jpeg_finish_decompress(&decoder);
            jpeg_destroy_decompress(&decoder);
            RgbaImageData image;
            image.width = width;
            image.height = height;
            image.pixels.resize(outputByteCount);
            for (uint32_t y = 0u; y < height; ++y)
            {
                const size_t sourceOffset =
                    size_t(height - 1u - y) * rowBytes;
                const size_t targetOffset = size_t(y) * rowBytes;
                eastl::copy_n(
                    errorState.pixels + sourceOffset,
                    rowBytes,
                    image.pixels.data() + targetOffset);
            }
            std::free(errorState.pixels);
            errorState.pixels = nullptr;
            return image;
        }
#endif

        /** Decodes one ImageIO source into a tightly packed top-down sRGB RGBA8 image. */
        RgbaImageData decodeImageSourceRgba8(
            CGImageSourceRef source,
            const std::string &assetDescription);

        /** Decodes one bounded UltraHDR JPEG byte range through ImageIO. */
        RgbaImageData decodeUltraHdrJpegBytes(
            const uint8_t *bytes,
            size_t byteCount)
        {
            if (bytes == nullptr || byteCount == 0u)
            {
                throw std::runtime_error(
                    "UltraHDR contains an empty JPEG image.");
            }
#if defined(GVM_THREE_USE_LIBJPEG_TURBO)
            return decodeChromeJpegWithLibjpeg(bytes, byteCount);
#else
            const CFDataRef encodedData = CFDataCreate(
                kCFAllocatorDefault,
                bytes,
                static_cast<CFIndex>(byteCount));
            if (encodedData == nullptr)
            {
                throw std::runtime_error(
                    "Could not create ImageIO data for an UltraHDR JPEG.");
            }
            const CGImageSourceRef source =
                CGImageSourceCreateWithData(encodedData, nullptr);
            CFRelease(encodedData);
            if (source == nullptr)
            {
                throw std::runtime_error(
                    "Could not open an UltraHDR JPEG byte range.");
            }
            RgbaImageData image = decodeImageSourceRgba8(
                source,
                "UltraHDR JPEG image");
            CFRelease(source);
            return image;
#endif
        }

        /** Reads one quoted numeric XMP attribute from an UltraHDR container. */
        double readUltraHdrXmpNumber(
            const std::string &container,
            const char *name,
            double fallback)
        {
            const std::string prefix =
                std::string("hdrgm:") + name + "=\"";
            const size_t begin = container.find(prefix);
            if (begin == std::string::npos)
            {
                return fallback;
            }
            const size_t valueBegin = begin + prefix.size();
            const size_t valueEnd = container.find('"', valueBegin);
            if (valueEnd == std::string::npos)
            {
                throw std::runtime_error(
                    "UltraHDR contains a truncated XMP gain-map field.");
            }
            return std::stod(container.substr(
                valueBegin,
                valueEnd - valueBegin));
        }

        /** Parses the legacy r185 XMP gain-map contract from one container. */
        UltraHdrGainMapMetadata parseUltraHdrGainMapMetadata(
            const eastl::vector<uint8_t> &bytes)
        {
            const std::string container(
                reinterpret_cast<const char *>(bytes.data()),
                bytes.size());
            UltraHdrGainMapMetadata metadata;
            metadata.gainMapMin =
                readUltraHdrXmpNumber(container, "GainMapMin", 0.0);
            metadata.gainMapMax =
                readUltraHdrXmpNumber(container, "GainMapMax", 1.0);
            metadata.gamma =
                readUltraHdrXmpNumber(container, "Gamma", 1.0);
            metadata.offsetSdr =
                readUltraHdrXmpNumber(container, "OffsetSDR", 0.0) * 64.0;
            metadata.offsetHdr =
                readUltraHdrXmpNumber(container, "OffsetHDR", 0.0) * 64.0;
            metadata.hdrCapacityMin =
                readUltraHdrXmpNumber(container, "HDRCapacityMin", 0.0);
            metadata.hdrCapacityMax =
                readUltraHdrXmpNumber(container, "HDRCapacityMax", 1.0);
            const std::string hdrPrefix =
                "hdrgm:BaseRenditionIsHDR=\"";
            const size_t hdrBegin = container.find(hdrPrefix);
            metadata.baseRenditionIsHdr =
                hdrBegin != std::string::npos &&
                container.compare(
                    hdrBegin + hdrPrefix.size(),
                    4u,
                    "True") == 0;
            return metadata;
        }

        /** Returns the r185 Canvas bilinear gain-map channel at one SDR pixel. */
        double sampleUltraHdrGainMap(
            const RgbaImageData &gainMap,
            uint32_t targetWidth,
            uint32_t targetHeight,
            uint32_t x,
            uint32_t y,
            uint32_t channel)
        {
            const double sourceX =
                (double(x) + 0.5) * double(gainMap.width) /
                    double(targetWidth) -
                0.5;
            const double sourceY =
                (double(y) + 0.5) * double(gainMap.height) /
                    double(targetHeight) -
                0.5;
            const int32_t x0 = static_cast<int32_t>(std::floor(sourceX));
            const int32_t y0 = static_cast<int32_t>(std::floor(sourceY));
            const uint32_t sampleX0 = static_cast<uint32_t>(eastl::clamp(
                x0, 0, static_cast<int32_t>(gainMap.width - 1u)));
            const uint32_t sampleY0 = static_cast<uint32_t>(eastl::clamp(
                y0, 0, static_cast<int32_t>(gainMap.height - 1u)));
            const uint32_t sampleX1 = static_cast<uint32_t>(eastl::clamp(
                x0 + 1, 0, static_cast<int32_t>(gainMap.width - 1u)));
            const uint32_t sampleY1 = static_cast<uint32_t>(eastl::clamp(
                y0 + 1, 0, static_cast<int32_t>(gainMap.height - 1u)));
            const double fractionX = sourceX - std::floor(sourceX);
            const double fractionY = sourceY - std::floor(sourceY);
            const size_t upperLeftOffset =
                (static_cast<size_t>(sampleY0) * gainMap.width + sampleX0) *
                    RgbaChannelCount +
                channel;
            const size_t upperRightOffset =
                (static_cast<size_t>(sampleY0) * gainMap.width + sampleX1) *
                    RgbaChannelCount +
                channel;
            const size_t lowerLeftOffset =
                (static_cast<size_t>(sampleY1) * gainMap.width + sampleX0) *
                    RgbaChannelCount +
                channel;
            const size_t lowerRightOffset =
                (static_cast<size_t>(sampleY1) * gainMap.width + sampleX1) *
                    RgbaChannelCount +
                channel;
            const double upper =
                double(gainMap.pixels[upperLeftOffset]) *
                    (1.0 - fractionX) +
                double(gainMap.pixels[upperRightOffset]) *
                    fractionX;
            const double lower =
                double(gainMap.pixels[lowerLeftOffset]) *
                    (1.0 - fractionX) +
                double(gainMap.pixels[lowerRightOffset]) *
                    fractionX;
            /* Keep the browser Canvas interpolation in floating point.  The
               UltraHDR loader consumes the interpolated gain value directly;
               rounding here quantizes the recovery before the logarithmic
               boost and produces visible bands in transmission backgrounds. */
            return upper * (1.0 - fractionY) + lower * fractionY;
        }

        /** Reproduces r185's byte-domain sRGB lookup for one HDR channel. */
        double ultraHdrSrgbToLinear(double value)
        {
            if (value < 10.31475)
            {
                return value * 0.000303527;
            }
            const double lookupValue = value < 1024.0
                ? std::floor(value)
                : value;
            return std::pow(
                lookupValue * 0.003717127 + 0.0521327014,
                2.4);
        }

        /** Reads one network-order uint32 from a validated byte range. */
        uint32_t readPngBigEndianUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "PNG contains a truncated uint32 field.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Returns the PNG Paeth predictor for one filtered channel byte. */
        uint8_t predictPngPaeth(
            uint8_t left,
            uint8_t above,
            uint8_t upperLeft)
        {
            const int leftValue = int(left);
            const int aboveValue = int(above);
            const int upperLeftValue = int(upperLeft);
            const int prediction =
                leftValue + aboveValue - upperLeftValue;
            const int leftDistance =
                std::abs(prediction - leftValue);
            const int aboveDistance =
                std::abs(prediction - aboveValue);
            const int upperLeftDistance =
                std::abs(prediction - upperLeftValue);
            if (leftDistance <= aboveDistance &&
                leftDistance <= upperLeftDistance)
            {
                return left;
            }
            if (aboveDistance <= upperLeftDistance)
            {
                return above;
            }
            return upperLeft;
        }

        /** Reads one complete file into deterministic byte storage. */
        eastl::vector<uint8_t> readImageBytes(
            const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open image asset: " +
                    assetPath.string());
            }
            input.seekg(0, std::ios::end);
            const std::streamoff byteCount = input.tellg();
            input.seekg(0, std::ios::beg);
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "Image asset is empty: " +
                    assetPath.string());
            }
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read complete image asset: " +
                    assetPath.string());
            }
            return bytes;
        }

        /** Returns one tightly packed RGBA8 channel from a validated image coordinate. */
        uint8_t readChannel(
            const RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            uint32_t channel);

        /** Builds one raw 2x2 box-filtered mip for every RGBA channel. */
        RgbaImageData buildNextUnormMip(
            const RgbaImageData &source)
        {
            RgbaImageData target;
            target.width = eastl::max(source.width / 2u, 1u);
            target.height = eastl::max(source.height / 2u, 1u);
            target.pixels.resize(
                static_cast<size_t>(target.width) *
                target.height *
                RgbaChannelCount);
            for (uint32_t targetY = 0u;
                 targetY < target.height;
                 ++targetY)
            {
                for (uint32_t targetX = 0u;
                     targetX < target.width;
                     ++targetX)
                {
                    const uint32_t sourceX0 =
                        eastl::min(targetX * 2u, source.width - 1u);
                    const uint32_t sourceY0 =
                        eastl::min(targetY * 2u, source.height - 1u);
                    const uint32_t sourceX1 =
                        eastl::min(sourceX0 + 1u, source.width - 1u);
                    const uint32_t sourceY1 =
                        eastl::min(sourceY0 + 1u, source.height - 1u);
                    const uint32_t sampleX[4u] = {
                        sourceX0,
                        sourceX1,
                        sourceX0,
                        sourceX1};
                    const uint32_t sampleY[4u] = {
                        sourceY0,
                        sourceY0,
                        sourceY1,
                        sourceY1};
                    const size_t targetOffset =
                        (static_cast<size_t>(targetY) *
                             target.width +
                         targetX) *
                        RgbaChannelCount;
                    for (uint32_t channel = 0u;
                         channel < RgbaChannelCount;
                         ++channel)
                    {
                        uint32_t sum = 0u;
                        for (uint32_t sample = 0u;
                             sample < 4u;
                             ++sample)
                        {
                            sum += readChannel(
                                source,
                                sampleX[sample],
                                sampleY[sample],
                                channel);
                        }
                        target.pixels[targetOffset + channel] =
                            static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Converts one normalized sRGB channel to linear light. */
        float srgbToLinear(float value)
        {
            if (value <= 0.04045f)
            {
                return value / 12.92f;
            }
            return std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one normalized linear-light channel to an 8-bit sRGB channel. */
        uint8_t linearToSrgbByte(float value)
        {
            const float clamped = eastl::clamp(value, 0.0f, 1.0f);
            const float encoded = clamped <= 0.0031308f
                ? clamped * 12.92f
                : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(
                std::lround(eastl::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        /** Returns one tightly packed RGBA8 channel from a validated image coordinate. */
        uint8_t readChannel(
            const RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            uint32_t channel)
        {
            const size_t offset =
                (static_cast<size_t>(y) * image.width + x) * RgbaChannelCount + channel;
            return image.pixels[offset];
        }

        /** Builds one 2x2 box-filtered mip in linear light while preserving straight alpha. */
        RgbaImageData buildNextSrgbMip(const RgbaImageData &source)
        {
            RgbaImageData target;
            target.width = eastl::max(source.width / 2u, 1u);
            target.height = eastl::max(source.height / 2u, 1u);
            target.pixels.resize(
                static_cast<size_t>(target.width) * target.height * RgbaChannelCount);

            for (uint32_t targetY = 0; targetY < target.height; ++targetY)
            {
                for (uint32_t targetX = 0; targetX < target.width; ++targetX)
                {
                    const uint32_t sourceX0 = eastl::min(targetX * 2u, source.width - 1u);
                    const uint32_t sourceY0 = eastl::min(targetY * 2u, source.height - 1u);
                    const uint32_t sourceX1 = eastl::min(sourceX0 + 1u, source.width - 1u);
                    const uint32_t sourceY1 = eastl::min(sourceY0 + 1u, source.height - 1u);
                    const uint32_t sampleX[4] = {sourceX0, sourceX1, sourceX0, sourceX1};
                    const uint32_t sampleY[4] = {sourceY0, sourceY0, sourceY1, sourceY1};
                    const size_t targetOffset =
                        (static_cast<size_t>(targetY) * target.width + targetX) * RgbaChannelCount;

                    for (uint32_t channel = 0; channel < 3u; ++channel)
                    {
                        float sum = 0.0f;
                        for (uint32_t sample = 0; sample < 4u; ++sample)
                        {
                            sum += srgbToLinear(
                                float(readChannel(source, sampleX[sample], sampleY[sample], channel)) /
                                255.0f);
                        }
                        target.pixels[targetOffset + channel] = linearToSrgbByte(sum * 0.25f);
                    }

                    uint32_t alphaSum = 0u;
                    for (uint32_t sample = 0; sample < 4u; ++sample)
                    {
                        alphaSum += readChannel(source, sampleX[sample], sampleY[sample], 3u);
                    }
                    target.pixels[targetOffset + 3u] =
                        static_cast<uint8_t>((alphaSum + 2u) / 4u);
                }
            }
            return target;
        }

        /** Decodes one ImageIO source into a tightly packed top-down sRGB RGBA8 image. */
        RgbaImageData decodeImageSourceRgba8(
            CGImageSourceRef source,
            const std::string &assetDescription)
        {
            const CGImageRef decodedImage = CGImageSourceCreateImageAtIndex(source, 0u, nullptr);
            if (decodedImage == nullptr)
            {
                throw std::runtime_error(
                    "Could not decode " + assetDescription + ".");
            }

            RgbaImageData image;
            image.width = static_cast<uint32_t>(CGImageGetWidth(decodedImage));
            image.height = static_cast<uint32_t>(CGImageGetHeight(decodedImage));
            if (image.width == 0u || image.height == 0u)
            {
                CGImageRelease(decodedImage);
                throw std::runtime_error(
                    "Decoded " + assetDescription + " has an empty extent.");
            }

            const size_t byteCount =
                static_cast<size_t>(image.width) * image.height * RgbaChannelCount;
            image.pixels.resize(byteCount);

            const CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            if (colorSpace == nullptr)
            {
                CGImageRelease(decodedImage);
                throw std::runtime_error(
                    "Could not create the sRGB " + assetDescription +
                    " decode color space.");
            }
            const CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
                static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) |
                static_cast<uint32_t>(kCGBitmapByteOrder32Big));
            const CGContextRef context = CGBitmapContextCreate(
                image.pixels.data(),
                image.width,
                image.height,
                8u,
                static_cast<size_t>(image.width) * RgbaChannelCount,
                colorSpace,
                bitmapInfo);
            CGColorSpaceRelease(colorSpace);
            if (context == nullptr)
            {
                CGImageRelease(decodedImage);
                throw std::runtime_error(
                    "Could not create the RGBA8 " + assetDescription +
                    " decode context.");
            }

            CGContextSetBlendMode(context, kCGBlendModeCopy);
            CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(image.height));
            CGContextScaleCTM(context, 1.0, -1.0);
            CGContextDrawImage(
                context,
                CGRectMake(0.0, 0.0, image.width, image.height),
                decodedImage);
            CGContextRelease(context);
            CGImageRelease(decodedImage);
            return image;
        }

        /** Opens one path-backed ImageIO source and decodes its first image. */
        RgbaImageData decodeImageRgba8(
            const std::filesystem::path &assetPath,
            const char *assetKind)
        {
            const std::string pathString = assetPath.string();
            const CFURLRef assetUrl = CFURLCreateFromFileSystemRepresentation(
                kCFAllocatorDefault,
                reinterpret_cast<const UInt8 *>(pathString.data()),
                static_cast<CFIndex>(pathString.size()),
                false);
            if (assetUrl == nullptr)
            {
                throw std::runtime_error(
                    "Could not create a URL for " + std::string(assetKind) +
                    " asset: " + pathString);
            }
            const CGImageSourceRef source =
                CGImageSourceCreateWithURL(assetUrl, nullptr);
            CFRelease(assetUrl);
            if (source == nullptr)
            {
                throw std::runtime_error(
                    "Could not open explicit " + std::string(assetKind) +
                    " asset: " + pathString);
            }
            RgbaImageData image = decodeImageSourceRgba8(
                source,
                std::string(assetKind) + " asset: " + pathString);
            CFRelease(source);
            return image;
        }

        /** Converts one finite float channel into its IEEE binary16 payload. */
        uint16_t encodeImageHalf(float value)
        {
            const _Float16 halfValue = static_cast<_Float16>(value);
            uint16_t bits = 0u;
            std::memcpy(&bits, &halfValue, sizeof(bits));
            return bits;
        }
    } // namespace

    RgbaImageData decodeGifRgba8(const std::filesystem::path &assetPath)
    {
        return decodeImageRgba8(assetPath, "GIF");
    }

    RgbaImageData decodeJpegRgba8(const std::filesystem::path &assetPath)
    {
        return decodeImageRgba8(assetPath, "JPEG");
    }

    RgbaImageData decodeJpegRgba8(
        const eastl::vector<uint8_t> &encodedBytes)
    {
        if (encodedBytes.empty())
        {
            throw std::runtime_error("In-memory JPEG payload is empty.");
        }
        const CFDataRef encodedData = CFDataCreate(
            kCFAllocatorDefault,
            encodedBytes.data(),
            static_cast<CFIndex>(encodedBytes.size()));
        if (encodedData == nullptr)
        {
            throw std::runtime_error(
                "Could not create ImageIO data for an in-memory JPEG.");
        }
        const void *optionKeys[] = {kCGImageSourceTypeIdentifierHint};
        const void *optionValues[] = {CFSTR("org.khronos.ktx")};
        const CFDictionaryRef options = CFDictionaryCreate(
            kCFAllocatorDefault,
            optionKeys,
            optionValues,
            1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        const CGImageSourceRef source =
            CGImageSourceCreateWithData(encodedData, options);
        CFRelease(options);
        CFRelease(encodedData);
        if (source == nullptr)
        {
            throw std::runtime_error(
                "Could not open an in-memory JPEG payload.");
        }
        RgbaImageData image = decodeImageSourceRgba8(
            source,
            "in-memory JPEG asset");
        CFRelease(source);
        return image;
    }

    Rgba16FloatImageData decodeUltraHdrRgba16Float(
        const std::filesystem::path &assetPath)
    {
        const eastl::vector<uint8_t> bytes = readImageBytes(assetPath);
        size_t gainMapOffset = bytes.size();
        for (size_t offset = 2u; offset + 1u < bytes.size(); ++offset)
        {
            if (bytes[offset] == 0xffu && bytes[offset + 1u] == 0xd8u)
            {
                gainMapOffset = offset;
                break;
            }
        }
        if (gainMapOffset == bytes.size())
        {
            throw std::runtime_error(
                "UltraHDR does not contain a secondary gain-map JPEG.");
        }
        const RgbaImageData sdr = decodeUltraHdrJpegBytes(
            bytes.data(),
            gainMapOffset);
        const RgbaImageData gainMap = decodeUltraHdrJpegBytes(
            bytes.data() + gainMapOffset,
            bytes.size() - gainMapOffset);
        if (uint64_t(sdr.width) * gainMap.height !=
            uint64_t(sdr.height) * gainMap.width)
        {
            throw std::runtime_error(
                "UltraHDR SDR and gain-map aspect ratios differ.");
        }
        const UltraHdrGainMapMetadata metadata =
            parseUltraHdrGainMapMetadata(bytes);
        if (metadata.baseRenditionIsHdr)
        {
            throw std::runtime_error(
                "HDR-base UltraHDR images are outside the r185 loader contract.");
        }
        Rgba16FloatImageData image;
        image.width = sdr.width;
        image.height = sdr.height;
        image.pixels.resize(
            static_cast<size_t>(image.width) * image.height *
            RgbaChannelCount,
            encodeImageHalf(1.0f));
        const double maxDisplayBoost = std::pow(
            1.8,
            metadata.hdrCapacityMax * 0.5);
        const double denominator =
            metadata.hdrCapacityMax - metadata.hdrCapacityMin;
        const double unclampedWeight = denominator == 0.0
            ? 1.0
            : (std::log2(maxDisplayBoost) - metadata.hdrCapacityMin) /
                denominator;
        const double weight = eastl::clamp(unclampedWeight, 0.0, 1.0);
        const double inverseGamma = 1.0 / metadata.gamma;
        for (uint32_t y = 0u; y < image.height; ++y)
        {
            for (uint32_t x = 0u; x < image.width; ++x)
            {
                const size_t pixelOffset =
                    (static_cast<size_t>(y) * image.width + x) *
                    RgbaChannelCount;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double sdrValue = sdr.pixels[pixelOffset + channel];
                    const double gainMapValue = sampleUltraHdrGainMap(
                        gainMap,
                        image.width,
                        image.height,
                        x,
                        y,
                        channel) / 255.0;
                    const double logRecovery = metadata.gamma == 1.0
                        ? gainMapValue
                        : std::pow(gainMapValue, inverseGamma);
                    const double logBoost =
                        metadata.gainMapMin +
                        (metadata.gainMapMax - metadata.gainMapMin) *
                            logRecovery;
                    const double weightedBoost = logBoost * weight;
                    const double hdrValue =
                        (sdrValue + metadata.offsetSdr) *
                            (weightedBoost == 0.0
                                ? 1.0
                                : std::pow(2.0, weightedBoost)) -
                        metadata.offsetHdr;
                    const double linearValue = eastl::clamp(
                        ultraHdrSrgbToLinear(hdrValue),
                        0.0,
                        65504.0);
                    image.pixels[pixelOffset + channel] =
                        encodeImageHalf(static_cast<float>(linearValue));
                }
            }
        }
        return image;
    }

    /** Decodes one PNG asset through the shared deterministic ImageIO path. */
    RgbaImageData decodePngRgba8(const std::filesystem::path &assetPath)
    {
        return decodeImageRgba8(assetPath, "PNG");
    }

    RgbaImageData decodeTiffRgba8(const std::filesystem::path &assetPath)
    {
        return decodeImageRgba8(assetPath, "TIFF");
    }

    RgbaImageData decodeImageIoTextureRgba8(
        const std::filesystem::path &assetPath)
    {
        return decodeImageRgba8(assetPath, "sample-private texture container");
    }

    RgbaImageData decodeImageIoTextureRgba8(
        const eastl::vector<uint8_t> &encodedBytes,
        const char *assetDescription)
    {
        if (encodedBytes.empty() || assetDescription == nullptr)
        {
            throw std::invalid_argument(
                "In-memory texture decode requires bytes and an asset description.");
        }
        const CFDataRef encodedData = CFDataCreate(
            kCFAllocatorDefault,
            encodedBytes.data(),
            static_cast<CFIndex>(encodedBytes.size()));
        if (encodedData == nullptr)
        {
            throw std::runtime_error(
                "Could not create ImageIO data for an in-memory texture container.");
        }
        const CGImageSourceRef source =
            CGImageSourceCreateWithData(encodedData, nullptr);
        CFRelease(encodedData);
        if (source == nullptr)
        {
            throw std::runtime_error(
                "Could not open in-memory texture container: " +
                std::string(assetDescription));
        }
        RgbaImageData image = decodeImageSourceRgba8(
            source,
            std::string(assetDescription));
        CFRelease(source);
        return image;
    }

    RgbaImageData decodeStraightPngRgba8(
        const std::filesystem::path &assetPath)
    {
        constexpr eastl::array<uint8_t, 8u> PngSignature = {
            0x89u,
            0x50u,
            0x4eu,
            0x47u,
            0x0du,
            0x0au,
            0x1au,
            0x0au};
        const eastl::vector<uint8_t> pngBytes =
            readImageBytes(assetPath);
        if (pngBytes.size() < PngSignature.size() ||
            !eastl::equal(
                PngSignature.begin(),
                PngSignature.end(),
                pngBytes.begin()))
        {
            throw std::runtime_error(
                "Straight PNG decoder received an invalid signature.");
        }

        RgbaImageData image;
        eastl::vector<uint8_t> compressed;
        bool ihdrSeen = false;
        bool iendSeen = false;
        size_t offset = PngSignature.size();
        while (offset < pngBytes.size())
        {
            if (pngBytes.size() - offset < 12u)
            {
                throw std::runtime_error(
                    "Straight PNG decoder found a truncated chunk.");
            }
            const uint32_t payloadSize =
                readPngBigEndianUint32(pngBytes, offset);
            const size_t payloadOffset = offset + 8u;
            if (payloadSize >
                pngBytes.size() - payloadOffset - 4u)
            {
                throw std::runtime_error(
                    "Straight PNG decoder found a truncated payload.");
            }
            const char type0 = char(pngBytes[offset + 4u]);
            const char type1 = char(pngBytes[offset + 5u]);
            const char type2 = char(pngBytes[offset + 6u]);
            const char type3 = char(pngBytes[offset + 7u]);
            if (type0 == 'I' && type1 == 'H' &&
                type2 == 'D' && type3 == 'R')
            {
                if (ihdrSeen || payloadSize != 13u)
                {
                    throw std::runtime_error(
                        "Straight PNG decoder found an invalid IHDR.");
                }
                image.width =
                    readPngBigEndianUint32(
                        pngBytes,
                        payloadOffset);
                image.height =
                    readPngBigEndianUint32(
                        pngBytes,
                        payloadOffset + 4u);
                if (image.width == 0u ||
                    image.height == 0u ||
                    pngBytes[payloadOffset + 8u] != 8u ||
                    pngBytes[payloadOffset + 9u] != 6u ||
                    pngBytes[payloadOffset + 10u] != 0u ||
                    pngBytes[payloadOffset + 11u] != 0u ||
                    pngBytes[payloadOffset + 12u] != 0u)
                {
                    throw std::runtime_error(
                        "Straight PNG decoder requires noninterlaced RGBA8 input.");
                }
                ihdrSeen = true;
            }
            else if (
                type0 == 'I' && type1 == 'D' &&
                type2 == 'A' && type3 == 'T')
            {
                compressed.insert(
                    compressed.end(),
                    pngBytes.begin() + payloadOffset,
                    pngBytes.begin() +
                        payloadOffset +
                        payloadSize);
            }
            else if (
                type0 == 'I' && type1 == 'E' &&
                type2 == 'N' && type3 == 'D')
            {
                iendSeen = true;
                break;
            }
            offset =
                payloadOffset +
                payloadSize +
                4u;
        }
        if (!ihdrSeen || !iendSeen || compressed.size() <= 6u)
        {
            throw std::runtime_error(
                "Straight PNG decoder lacks required chunks.");
        }
        const uint32_t zlibHeader =
            uint32_t(compressed[0u]) * 256u +
            uint32_t(compressed[1u]);
        if ((compressed[0u] & 0x0fu) != 8u ||
            zlibHeader % 31u != 0u)
        {
            throw std::runtime_error(
                "Straight PNG decoder found an unsupported zlib stream.");
        }

        const size_t rowByteCount =
            static_cast<size_t>(image.width) *
            RgbaChannelCount;
        const size_t filteredByteCount =
            static_cast<size_t>(image.height) *
            (rowByteCount + 1u);
        eastl::vector<uint8_t> filtered(filteredByteCount);
        const size_t decodedSize = compression_decode_buffer(
            filtered.data(),
            filtered.size(),
            compressed.data() + 2u,
            compressed.size() - 6u,
            nullptr,
            COMPRESSION_ZLIB);
        if (decodedSize != filteredByteCount)
        {
            throw std::runtime_error(
                "Straight PNG decoder could not inflate the complete payload.");
        }

        image.pixels.resize(
            static_cast<size_t>(image.width) *
            image.height *
            RgbaChannelCount);
        for (uint32_t y = 0u; y < image.height; ++y)
        {
            const size_t filteredRow =
                static_cast<size_t>(y) *
                (rowByteCount + 1u);
            const uint8_t filter = filtered[filteredRow];
            if (filter > 4u)
            {
                throw std::runtime_error(
                    "Straight PNG decoder found an unsupported row filter.");
            }
            for (size_t x = 0u; x < rowByteCount; ++x)
            {
                const uint8_t source =
                    filtered[filteredRow + 1u + x];
                const size_t outputOffset =
                    static_cast<size_t>(y) *
                        rowByteCount +
                    x;
                const uint8_t left = x >= RgbaChannelCount
                    ? image.pixels[
                          outputOffset -
                          RgbaChannelCount]
                    : 0u;
                const uint8_t above = y > 0u
                    ? image.pixels[
                          outputOffset -
                          rowByteCount]
                    : 0u;
                const uint8_t upperLeft =
                    y > 0u && x >= RgbaChannelCount
                    ? image.pixels[
                          outputOffset -
                          rowByteCount -
                          RgbaChannelCount]
                    : 0u;
                uint8_t predictor = 0u;
                if (filter == 1u)
                {
                    predictor = left;
                }
                else if (filter == 2u)
                {
                    predictor = above;
                }
                else if (filter == 3u)
                {
                    predictor = uint8_t(
                        (uint32_t(left) +
                         uint32_t(above)) /
                        2u);
                }
                else if (filter == 4u)
                {
                    predictor =
                        predictPngPaeth(
                            left,
                            above,
                            upperLeft);
                }
                image.pixels[outputOffset] =
                    uint8_t(
                        uint32_t(source) +
                        uint32_t(predictor));
            }
        }
        return image;
    }

    eastl::vector<RgbaImageData> buildSrgbMipChain(const RgbaImageData &baseImage)
    {
        const size_t expectedByteCount =
            static_cast<size_t>(baseImage.width) * baseImage.height * RgbaChannelCount;
        if (baseImage.width == 0u || baseImage.height == 0u ||
            baseImage.pixels.size() != expectedByteCount)
        {
            throw std::invalid_argument("sRGB mip generation requires a non-empty tightly packed RGBA8 image.");
        }

        eastl::vector<RgbaImageData> levels;
        levels.push_back(baseImage);
        while (levels.back().width > 1u || levels.back().height > 1u)
        {
            levels.push_back(buildNextSrgbMip(levels.back()));
        }
        return levels;
    }

    eastl::vector<RgbaImageData> buildUnormMipChain(
        const RgbaImageData &baseImage)
    {
        const size_t expectedByteCount =
            static_cast<size_t>(baseImage.width) *
            baseImage.height *
            RgbaChannelCount;
        if (baseImage.width == 0u ||
            baseImage.height == 0u ||
            baseImage.pixels.size() != expectedByteCount)
        {
            throw std::invalid_argument(
                "UNorm mip generation requires a nonempty tightly packed RGBA8 image.");
        }
        eastl::vector<RgbaImageData> levels;
        levels.push_back(baseImage);
        while (
            levels.back().width > 1u ||
            levels.back().height > 1u)
        {
            levels.push_back(
                buildNextUnormMip(levels.back()));
        }
        return levels;
    }
} // namespace GVM::ThreeSamples
