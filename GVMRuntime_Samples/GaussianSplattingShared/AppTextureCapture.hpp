#pragma once

#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace GaussianSplattingCapture
{
    inline bool writeRgbPpm(
        const std::vector<std::uint8_t> &rgbPixels,
        uint32_t width,
        uint32_t height,
        const std::string &path,
        std::string *errorMessage = nullptr)
    {
        auto fail = [&](const std::string &message) {
            if (errorMessage != nullptr)
            {
                *errorMessage = message;
            }
            return false;
        };

        std::ofstream output(path, std::ios::binary);
        if (!output.is_open())
        {
            return fail("failed to open output file");
        }

        output << "P6\n" << width << " " << height << "\n255\n";
        output.write(reinterpret_cast<const char *>(rgbPixels.data()), std::streamsize(rgbPixels.size()));
        output.flush();
        if (!output.good())
        {
            return fail("failed while writing ppm payload");
        }

        return true;
    }

    inline float decodeFloat16(std::uint16_t bits)
    {
        const std::uint32_t sign = std::uint32_t(bits & 0x8000u) << 16u;
        std::uint32_t exponent = (bits >> 10u) & 0x1fu;
        std::uint32_t mantissa = bits & 0x03ffu;
        std::uint32_t floatBits = 0u;

        if (exponent == 0u)
        {
            if (mantissa == 0u)
            {
                floatBits = sign;
            }
            else
            {
                exponent = 113u;
                while ((mantissa & 0x0400u) == 0u)
                {
                    mantissa <<= 1u;
                    exponent -= 1u;
                }
                mantissa &= 0x03ffu;
                floatBits = sign | (exponent << 23u) | (mantissa << 13u);
            }
        }
        else if (exponent == 0x1fu)
        {
            floatBits = sign | 0x7f800000u | (mantissa << 13u);
        }
        else
        {
            floatBits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
        }

        float decodedValue = 0.0f;
        std::memcpy(&decodedValue, &floatBits, sizeof(decodedValue));
        return decodedValue;
    }

    inline std::uint8_t linearToGammaByte(float linearValue)
    {
        if (!std::isfinite(linearValue))
        {
            linearValue = 0.0f;
        }
        linearValue = std::max(linearValue, 0.0f);
        linearValue = std::pow(linearValue, 1.0f / 2.2f);
        linearValue = std::clamp(linearValue, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::lround(linearValue * 255.0f));
    }

    inline bool dumpRgba8TextureToPpm(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        const std::string &path,
        std::string *errorMessage = nullptr)
    {
        auto fail = [&](const std::string &message) {
            if (errorMessage != nullptr)
            {
                *errorMessage = message;
            }
            return false;
        };

        if (device == nullptr)
        {
            return fail("device is null");
        }

        if (texture.isNull())
        {
            return fail("texture handle is null");
        }

        if (width == 0u || height == 0u)
        {
            return fail("texture extent is zero");
        }

        const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
        const uint64_t rgbaByteCount = pixelCount * 4u;
        const uint64_t rgbByteCount = pixelCount * 3u;
        if (rgbaByteCount > uint64_t(std::numeric_limits<size_t>::max()) || rgbByteCount > uint64_t(std::numeric_limits<size_t>::max()))
        {
            return fail("texture is too large for host readback buffer");
        }

        GVM::RHI::Queue queue = device->getMainQueue();
        if (queue == nullptr)
        {
            return fail("main queue is null");
        }

        std::vector<std::uint8_t> rgbaPixels((size_t(rgbaByteCount)));
        const GVM::RHI::ImageCopyTexture source = {
            .texture = texture,
            .mipLevel = 0u,
            .origin = {0u, 0u, 0u},
            .aspect = GVM::RHI::TextureAspect::All,
        };
        const GVM::RHI::Extent3D extent = {
            .width = width,
            .height = height,
            .depth = 1u,
        };

        queue->readTexture(source, rgbaPixels.data(), rgbaPixels.size(), {}, extent);
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);

        const GVM::RHI::TextureFormat textureFormat = texture->getFormat();
        const bool isBgra8 =
            textureFormat == GVM::RHI::TextureFormat::BGRA8Unorm ||
            textureFormat == GVM::RHI::TextureFormat::BGRA8UnormSrgb;
        const bool isRgba8 =
            textureFormat == GVM::RHI::TextureFormat::RGBA8Unorm ||
            textureFormat == GVM::RHI::TextureFormat::RGBA8UnormSrgb ||
            isBgra8;
        if (!isRgba8)
        {
            return fail("unsupported 8-bit texture format for host capture");
        }

        std::vector<std::uint8_t> rgbPixels((size_t(rgbByteCount)));
        for (uint64_t pixelIndex = 0u; pixelIndex < pixelCount; ++pixelIndex)
        {
            const uint64_t rgbaOffset = pixelIndex * 4u;
            const uint64_t rgbOffset = pixelIndex * 3u;
            if (isBgra8)
            {
                rgbPixels[size_t(rgbOffset + 0u)] = rgbaPixels[size_t(rgbaOffset + 2u)];
                rgbPixels[size_t(rgbOffset + 1u)] = rgbaPixels[size_t(rgbaOffset + 1u)];
                rgbPixels[size_t(rgbOffset + 2u)] = rgbaPixels[size_t(rgbaOffset + 0u)];
            }
            else
            {
                rgbPixels[size_t(rgbOffset + 0u)] = rgbaPixels[size_t(rgbaOffset + 0u)];
                rgbPixels[size_t(rgbOffset + 1u)] = rgbaPixels[size_t(rgbaOffset + 1u)];
                rgbPixels[size_t(rgbOffset + 2u)] = rgbaPixels[size_t(rgbaOffset + 2u)];
            }
        }

        return writeRgbPpm(rgbPixels, width, height, path, errorMessage);
    }

    inline bool dumpRgba16FloatTextureToPpm(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        const std::string &path,
        std::string *errorMessage = nullptr)
    {
        auto fail = [&](const std::string &message) {
            if (errorMessage != nullptr)
            {
                *errorMessage = message;
            }
            return false;
        };

        if (device == nullptr)
        {
            return fail("device is null");
        }

        if (texture.isNull())
        {
            return fail("texture handle is null");
        }

        if (width == 0u || height == 0u)
        {
            return fail("texture extent is zero");
        }

        const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
        const uint64_t rgbaByteCount = pixelCount * 8u;
        const uint64_t rgbByteCount = pixelCount * 3u;
        if (rgbaByteCount > uint64_t(std::numeric_limits<size_t>::max()) || rgbByteCount > uint64_t(std::numeric_limits<size_t>::max()))
        {
            return fail("texture is too large for host readback buffer");
        }

        GVM::RHI::Queue queue = device->getMainQueue();
        if (queue == nullptr)
        {
            return fail("main queue is null");
        }

        std::vector<std::uint8_t> rgbaBytes((size_t(rgbaByteCount)));
        const GVM::RHI::ImageCopyTexture source = {
            .texture = texture,
            .mipLevel = 0u,
            .origin = {0u, 0u, 0u},
            .aspect = GVM::RHI::TextureAspect::All,
        };
        const GVM::RHI::Extent3D extent = {
            .width = width,
            .height = height,
            .depth = 1u,
        };

        queue->readTexture(source, rgbaBytes.data(), rgbaBytes.size(), {}, extent);
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);

        std::vector<std::uint8_t> rgbPixels((size_t(rgbByteCount)));
        for (uint64_t pixelIndex = 0u; pixelIndex < pixelCount; ++pixelIndex)
        {
            const uint64_t sourceOffset = pixelIndex * 8u;
            std::uint16_t redHalf = 0u;
            std::uint16_t greenHalf = 0u;
            std::uint16_t blueHalf = 0u;
            std::memcpy(&redHalf, rgbaBytes.data() + size_t(sourceOffset + 0u), sizeof(redHalf));
            std::memcpy(&greenHalf, rgbaBytes.data() + size_t(sourceOffset + 2u), sizeof(greenHalf));
            std::memcpy(&blueHalf, rgbaBytes.data() + size_t(sourceOffset + 4u), sizeof(blueHalf));

            const uint64_t rgbOffset = pixelIndex * 3u;
            rgbPixels[size_t(rgbOffset + 0u)] = linearToGammaByte(decodeFloat16(redHalf));
            rgbPixels[size_t(rgbOffset + 1u)] = linearToGammaByte(decodeFloat16(greenHalf));
            rgbPixels[size_t(rgbOffset + 2u)] = linearToGammaByte(decodeFloat16(blueHalf));
        }

        return writeRgbPpm(rgbPixels, width, height, path, errorMessage);
    }

    template <typename T>
    inline bool readBufferStruct(
        GVM::RHI::Device device,
        GVM::RHI::Buffer buffer,
        T *value,
        std::string *errorMessage = nullptr)
    {
        static_assert(std::is_trivially_copyable_v<T>, "readBufferStruct requires a trivially copyable payload.");

        auto fail = [&](const std::string &message) {
            if (errorMessage != nullptr)
            {
                *errorMessage = message;
            }
            return false;
        };

        if (device == nullptr)
        {
            return fail("device is null");
        }

        if (buffer.isNull())
        {
            return fail("buffer handle is null");
        }

        if (value == nullptr)
        {
            return fail("destination value pointer is null");
        }

        GVM::RHI::Queue queue = device->getMainQueue();
        if (queue == nullptr)
        {
            return fail("main queue is null");
        }

        queue->readBuffer(GVM::RHI::BufferRange(buffer, 0u, sizeof(T)), value, sizeof(T));
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);
        return true;
    }
} // namespace GaussianSplattingCapture
