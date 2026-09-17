#pragma once

#include "AppTextureCapture.hpp"

#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace GaussianSplattingCapture
{
    /**
     * Writes an optional error message and returns false for readback validation helpers.
     *
     * Use this to keep failure paths explicit without introducing per-function callbacks.
     */
    inline bool failFloatTextureStatsRead(const std::string &message, std::string *errorMessage)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = message;
        }
        return false;
    }

    /**
     * Stores aggregate statistics for one CPU-read floating-point texture image.
     *
     * Use this for diagnostics-only captures where exact image bytes are unnecessary but render-depth behavior must be validated.
     */
    struct FloatTextureStats
    {
        std::uint64_t pixelCount = 0u;
        std::uint64_t finiteCount = 0u;
        std::uint64_t zeroCount = 0u;
        std::uint64_t oneCount = 0u;
        std::uint64_t negativeCount = 0u;
        std::uint64_t greaterThanOneCount = 0u;
        std::uint64_t nonClearReverseZCount = 0u;
        std::uint64_t greaterThan001Count = 0u;
        std::uint64_t greaterThan01Count = 0u;
        std::uint64_t greaterThan05Count = 0u;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        double averageValue = 0.0;
    };

    /**
     * Builds deterministic aggregate statistics for a contiguous array of float values.
     *
     * Non-finite values are counted in pixelCount but excluded from min, max, average, and threshold counters.
     */
    inline FloatTextureStats buildFloatTextureStats(const float *values, std::uint64_t pixelCount)
    {
        FloatTextureStats stats = {};
        stats.pixelCount = pixelCount;

        float minValue = std::numeric_limits<float>::infinity();
        float maxValue = -std::numeric_limits<float>::infinity();
        double sum = 0.0;
        for (std::uint64_t pixelIndex = 0u; pixelIndex < pixelCount; ++pixelIndex)
        {
            const float value = values[pixelIndex];
            if (!std::isfinite(value))
            {
                continue;
            }

            stats.finiteCount += 1u;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
            sum += double(value);
            if (std::abs(value) <= 1.0e-7f)
            {
                stats.zeroCount += 1u;
            }
            if (std::abs(value - 1.0f) <= 1.0e-6f)
            {
                stats.oneCount += 1u;
            }
            if (value < 0.0f)
            {
                stats.negativeCount += 1u;
            }
            if (value > 1.0f)
            {
                stats.greaterThanOneCount += 1u;
            }
            if (value > 1.0e-6f)
            {
                stats.nonClearReverseZCount += 1u;
            }
            if (value > 0.001f)
            {
                stats.greaterThan001Count += 1u;
            }
            if (value > 0.01f)
            {
                stats.greaterThan01Count += 1u;
            }
            if (value > 0.5f)
            {
                stats.greaterThan05Count += 1u;
            }
        }

        if (stats.finiteCount > 0u)
        {
            stats.minValue = minValue;
            stats.maxValue = maxValue;
            stats.averageValue = sum / double(stats.finiteCount);
        }
        return stats;
    }

    /**
     * Reads all Depth32Float values into a CPU vector for capture-time diagnostics.
     *
     * The texture must be readable by the main queue and is sampled through the depth-only aspect. This helper is
     * intended for validation captures, not for per-frame runtime work.
     */
    inline bool readDepth32FloatTextureValues(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        std::vector<float> *values,
        std::string *errorMessage = nullptr)
    {
        if (device == nullptr)
        {
            return failFloatTextureStatsRead("device is null", errorMessage);
        }

        if (texture.isNull())
        {
            return failFloatTextureStatsRead("texture handle is null", errorMessage);
        }

        if (values == nullptr)
        {
            return failFloatTextureStatsRead("values pointer is null", errorMessage);
        }

        if (width == 0u || height == 0u)
        {
            return failFloatTextureStatsRead("texture extent is zero", errorMessage);
        }

        if (texture->getFormat() != GVM::RHI::TextureFormat::Depth32Float)
        {
            return failFloatTextureStatsRead("texture is not Depth32Float", errorMessage);
        }

        const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
        const uint64_t byteCount = pixelCount * sizeof(float);
        if (byteCount > uint64_t(std::numeric_limits<size_t>::max()))
        {
            return failFloatTextureStatsRead("texture is too large for host readback buffer", errorMessage);
        }

        GVM::RHI::Queue queue = device->getMainQueue();
        if (queue == nullptr)
        {
            return failFloatTextureStatsRead("main queue is null", errorMessage);
        }

        values->assign(size_t(pixelCount), 0.0f);
        const GVM::RHI::ImageCopyTexture source = {
            .texture = texture,
            .mipLevel = 0u,
            .origin = {0u, 0u, 0u},
            .aspect = GVM::RHI::TextureAspect::DepthOnly,
        };
        const GVM::RHI::Extent3D extent = {
            .width = width,
            .height = height,
            .depth = 1u,
        };

        try
        {
            queue->readTexture(source, values->data(), byteCount, {}, extent);
            eastl::vector<GVM::RHI::CommandEncoder> encoders;
            queue->submit(encoders);
        }
        catch (const std::exception &error)
        {
            return failFloatTextureStatsRead(error.what(), errorMessage);
        }

        return true;
    }

    /**
     * Reads a Depth32Float texture and computes CPU-side diagnostics statistics.
     *
     * The texture must be readable by the main queue and is sampled through the depth-only aspect.
     */
    inline bool readDepth32FloatTextureStats(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        FloatTextureStats *stats,
        std::string *errorMessage = nullptr)
    {
        if (stats == nullptr)
        {
            return failFloatTextureStatsRead("stats pointer is null", errorMessage);
        }

        std::vector<float> values;
        if (!readDepth32FloatTextureValues(device, texture, width, height, &values, errorMessage))
        {
            return false;
        }

        *stats = buildFloatTextureStats(values.data(), uint64_t(values.size()));
        return true;
    }

    /**
     * Reads one mip level of an R16Float texture and computes CPU-side diagnostics statistics.
     *
     * Width and height must describe the mip level requested by mipLevel, not necessarily the base texture size.
     */
    inline bool readR16FloatTextureMipStats(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevel,
        FloatTextureStats *stats,
        std::string *errorMessage = nullptr)
    {
        if (device == nullptr)
        {
            return failFloatTextureStatsRead("device is null", errorMessage);
        }

        if (texture.isNull())
        {
            return failFloatTextureStatsRead("texture handle is null", errorMessage);
        }

        if (stats == nullptr)
        {
            return failFloatTextureStatsRead("stats pointer is null", errorMessage);
        }

        if (width == 0u || height == 0u)
        {
            return failFloatTextureStatsRead("texture extent is zero", errorMessage);
        }

        if (texture->getFormat() != GVM::RHI::TextureFormat::R16Float)
        {
            return failFloatTextureStatsRead("texture is not R16Float", errorMessage);
        }

        const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
        const uint64_t byteCount = pixelCount * sizeof(std::uint16_t);
        if (byteCount > uint64_t(std::numeric_limits<size_t>::max()))
        {
            return failFloatTextureStatsRead("texture is too large for host readback buffer", errorMessage);
        }

        GVM::RHI::Queue queue = device->getMainQueue();
        if (queue == nullptr)
        {
            return failFloatTextureStatsRead("main queue is null", errorMessage);
        }

        std::vector<std::uint16_t> halfValues((size_t(pixelCount)));
        const GVM::RHI::ImageCopyTexture source = {
            .texture = texture,
            .mipLevel = mipLevel,
            .origin = {0u, 0u, 0u},
            .aspect = GVM::RHI::TextureAspect::All,
        };
        const GVM::RHI::Extent3D extent = {
            .width = width,
            .height = height,
            .depth = 1u,
        };

        try
        {
            queue->readTexture(source, halfValues.data(), byteCount, {}, extent);
            eastl::vector<GVM::RHI::CommandEncoder> encoders;
            queue->submit(encoders);
        }
        catch (const std::exception &error)
        {
            return failFloatTextureStatsRead(error.what(), errorMessage);
        }

        std::vector<float> values((size_t(pixelCount)));
        for (std::uint64_t pixelIndex = 0u; pixelIndex < pixelCount; ++pixelIndex)
        {
            values[size_t(pixelIndex)] = decodeFloat16(halfValues[size_t(pixelIndex)]);
        }

        *stats = buildFloatTextureStats(values.data(), pixelCount);
        return true;
    }

    /**
     * Reads the base level of an R16Float texture and computes CPU-side diagnostics statistics.
     *
     * This is the base-mip convenience wrapper used by 3DGS HiZ diagnostics.
     */
    inline bool readR16FloatTextureStats(
        GVM::RHI::Device device,
        GVM::RHI::Texture texture,
        uint32_t width,
        uint32_t height,
        FloatTextureStats *stats,
        std::string *errorMessage = nullptr)
    {
        return readR16FloatTextureMipStats(device, texture, width, height, 0u, stats, errorMessage);
    }

    /**
     * Writes one FloatTextureStats block into a key-value diagnostics stream.
     *
     * The output key shape is stable and is intended for strict refactor comparisons.
     */
    inline void writeFloatTextureStatsDiagnostics(
        std::ostream &diagnosticsStream,
        const std::string &prefix,
        bool readbackSucceeded,
        const FloatTextureStats &stats,
        const std::string &errorMessage)
    {
        diagnosticsStream << prefix << ".readbackSucceeded=" << (readbackSucceeded ? 1 : 0) << "\n";
        if (!readbackSucceeded)
        {
            diagnosticsStream << prefix << ".error=" << errorMessage << "\n";
            return;
        }

        diagnosticsStream << prefix << ".pixelCount=" << stats.pixelCount << "\n";
        diagnosticsStream << prefix << ".finiteCount=" << stats.finiteCount << "\n";
        diagnosticsStream << prefix << ".min=" << stats.minValue << "\n";
        diagnosticsStream << prefix << ".max=" << stats.maxValue << "\n";
        diagnosticsStream << prefix << ".avg=" << stats.averageValue << "\n";
        diagnosticsStream << prefix << ".zeroCount=" << stats.zeroCount << "\n";
        diagnosticsStream << prefix << ".oneCount=" << stats.oneCount << "\n";
        diagnosticsStream << prefix << ".negativeCount=" << stats.negativeCount << "\n";
        diagnosticsStream << prefix << ".greaterThanOneCount=" << stats.greaterThanOneCount << "\n";
        diagnosticsStream << prefix << ".nonClearReverseZCount=" << stats.nonClearReverseZCount << "\n";
        diagnosticsStream << prefix << ".greaterThan001Count=" << stats.greaterThan001Count << "\n";
        diagnosticsStream << prefix << ".greaterThan01Count=" << stats.greaterThan01Count << "\n";
        diagnosticsStream << prefix << ".greaterThan05Count=" << stats.greaterThan05Count << "\n";
    }
} // namespace GaussianSplattingCapture
