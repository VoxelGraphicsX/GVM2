#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 256u;
    constexpr uint32_t kHeight = 128u;
    constexpr uint32_t kTileSize = 64u;
    constexpr uint32_t kLocalSizeX = 16u;
    constexpr uint32_t kLocalSizeY = 16u;
    constexpr uint32_t kMaxMipBindings = 13u;

    const char *backendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal:
            return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan:
            return "vulkan";
        default:
            return "undefined";
        }
    }

    uint32_t ceilDiv(uint32_t value, uint32_t divisor)
    {
        return (value + divisor - 1u) / divisor;
    }

    uint32_t mipDim(uint32_t value, uint32_t mip)
    {
        return std::max(1u, value >> mip);
    }

    uint32_t calcMipCount(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1u;
        while (width > 1u || height > 1u)
        {
            width = std::max(1u, width >> 1u);
            height = std::max(1u, height >> 1u);
            ++levels;
        }
        return levels;
    }

    uint16_t floatToHalfBits(float value)
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint32_t sign = (bits >> 16u) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
        uint32_t mantissa = bits & 0x7fffffu;

        if (exponent <= 0)
        {
            if (exponent < -10)
            {
                return static_cast<uint16_t>(sign);
            }
            mantissa = (mantissa | 0x800000u) >> static_cast<uint32_t>(1 - exponent);
            return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13u));
        }
        if (exponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7c00u);
        }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) | ((mantissa + 0x1000u) >> 13u));
    }

    float halfBitsToFloat(uint16_t value)
    {
        const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16u;
        int32_t exponent = static_cast<int32_t>((static_cast<uint32_t>(value) >> 10u) & 0x1fu);
        uint32_t mantissa = static_cast<uint32_t>(value) & 0x03ffu;

        if (exponent == 0)
        {
            if (mantissa == 0u)
            {
                return std::bit_cast<float>(sign);
            }
            while ((mantissa & 0x0400u) == 0u)
            {
                mantissa <<= 1u;
                --exponent;
            }
            mantissa &= 0x03ffu;
            exponent += 1;
        }
        else if (exponent == 31)
        {
            return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13u));
        }

        const uint32_t floatExponent = static_cast<uint32_t>(exponent + (127 - 15));
        return std::bit_cast<float>(sign | (floatExponent << 23u) | (mantissa << 13u));
    }

    std::vector<float> makeDepthPattern(uint32_t width, uint32_t height)
    {
        std::vector<float> depth(width * height);
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                uint32_t bucket = ((x * 37u) ^ (y * 149u) ^ ((x + 17u) * (y + 29u))) & 1023u;
                if (x == 0u && y == 0u)
                {
                    bucket = 0u;
                }
                if (x + 1u == width && y + 1u == height)
                {
                    bucket = 1024u;
                }
                depth[y * width + x] = static_cast<float>(bucket) / 1024.0f;
            }
        }
        return depth;
    }

    uint16_t reduceHalf(uint16_t a, uint16_t b, bool reduceMax)
    {
        const float af = halfBitsToFloat(a);
        const float bf = halfBitsToFloat(b);
        return reduceMax ? (af >= bf ? a : b) : (af <= bf ? a : b);
    }

    std::vector<std::vector<uint16_t>> buildCpuReference(const std::vector<float> &depth, uint32_t width, uint32_t height, bool reduceMax)
    {
        const uint32_t mipCount = calcMipCount(width, height);
        std::vector<std::vector<uint16_t>> mips(mipCount);
        mips[0].resize(width * height);
        for (size_t i = 0u; i < depth.size(); ++i)
        {
            mips[0][i] = floatToHalfBits(depth[i]);
        }

        for (uint32_t mip = 1u; mip < mipCount; ++mip)
        {
            const uint32_t prevWidth = mipDim(width, mip - 1u);
            const uint32_t prevHeight = mipDim(height, mip - 1u);
            const uint32_t mipWidth = mipDim(width, mip);
            const uint32_t mipHeight = mipDim(height, mip);
            const uint16_t identity = floatToHalfBits(reduceMax ? 0.0f : 1.0f);
            const auto &prev = mips[mip - 1u];
            auto &current = mips[mip];
            current.assign(mipWidth * mipHeight, identity);

            for (uint32_t y = 0u; y < mipHeight; ++y)
            {
                for (uint32_t x = 0u; x < mipWidth; ++x)
                {
                    uint16_t value = identity;
                    for (uint32_t dy = 0u; dy < 2u; ++dy)
                    {
                        for (uint32_t dx = 0u; dx < 2u; ++dx)
                        {
                            const uint32_t srcX = x * 2u + dx;
                            const uint32_t srcY = y * 2u + dy;
                            if (srcX < prevWidth && srcY < prevHeight)
                            {
                                value = reduceHalf(value, prev[srcY * prevWidth + srcX], reduceMax);
                            }
                        }
                    }
                    current[y * mipWidth + x] = value;
                }
            }
        }
        return mips;
    }

    std::string halfHex(uint16_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::setw(4) << std::setfill('0') << value;
        return stream.str();
    }

    ::testing::AssertionResult mipChainsMatch(const std::vector<std::vector<uint16_t>> &gpu,
                                              const std::vector<std::vector<uint16_t>> &cpu,
                                              uint32_t width,
                                              uint32_t height,
                                              const char *modeName)
    {
        if (gpu.size() != cpu.size())
        {
            return ::testing::AssertionFailure() << modeName << " mip count mismatch: gpu=" << gpu.size() << " cpu=" << cpu.size();
        }

        for (uint32_t mip = 0u; mip < cpu.size(); ++mip)
        {
            const uint32_t mipWidth = mipDim(width, mip);
            const uint32_t mipHeight = mipDim(height, mip);
            if (gpu[mip].size() != cpu[mip].size())
            {
                return ::testing::AssertionFailure() << modeName << " mip " << mip << " size mismatch: gpu=" << gpu[mip].size()
                                                     << " cpu=" << cpu[mip].size();
            }

            for (uint32_t y = 0u; y < mipHeight; ++y)
            {
                for (uint32_t x = 0u; x < mipWidth; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * mipWidth + x;
                    if (gpu[mip][index] != cpu[mip][index])
                    {
                        return ::testing::AssertionFailure()
                               << modeName << " mismatch at mip=" << mip << " texel=(" << x << "," << y << ") expected="
                               << halfHex(cpu[mip][index]) << " (" << halfBitsToFloat(cpu[mip][index]) << ") actual="
                               << halfHex(gpu[mip][index]) << " (" << halfBitsToFloat(gpu[mip][index]) << ")";
                    }
                }
            }
        }
        return ::testing::AssertionSuccess();
    }

    class SpdHiZReadbackTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "SPD HiZ readback test requires Metal or Vulkan backend, got " << backendName(backend);
            }

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        void TearDown() override
        {
            device = nullptr;
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;
    };

    std::array<GVM::RHI::TextureView, kMaxMipBindings> createMipViews(GVM::RHI::Texture hiz,
                                                                       GVM::RHI::Texture emptyTexture,
                                                                       uint32_t mipCount)
    {
        std::array<GVM::RHI::TextureView, kMaxMipBindings> views = {};
        for (uint32_t mip = 0u; mip < kMaxMipBindings; ++mip)
        {
            if (mip < mipCount)
            {
                views[mip] = hiz->createView({
                    .label = eastl::string("SpdHiZReadbackMip") + eastl::to_string(mip),
                    .format = GVM::RHI::TextureFormat::R16Float,
                    .dimension = GVM::RHI::TextureViewDimension::e2D,
                    .baseMipLevel = mip,
                    .mipLevelCount = 1u,
                    .baseArrayLayer = 0u,
                    .arrayLayerCount = 1u,
                });
            }
            else
            {
                views[mip] = emptyTexture->createView({
                    .label = eastl::string("SpdHiZReadbackUnusedMip") + eastl::to_string(mip),
                    .format = GVM::RHI::TextureFormat::R16Float,
                    .dimension = GVM::RHI::TextureViewDimension::e2D,
                    .baseMipLevel = 0u,
                    .mipLevelCount = 1u,
                    .baseArrayLayer = 0u,
                    .arrayLayerCount = 1u,
                });
            }
        }
        return views;
    }

    std::vector<std::vector<uint16_t>> runSpdHiZ(GVM::RHI::Device device,
                                                 GVM::RHI::Texture sourceDepth,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool reduceMax)
    {
        const uint32_t mipCount = calcMipCount(width, height);
        GVM::Core::DeviceProxy deviceProxy(device);

        auto hiz = device->createTexture({
            .label = reduceMax ? "SpdHiZReadbackMax" : "SpdHiZReadbackMin",
            .usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {width, height, 1u},
            .format = GVM::RHI::TextureFormat::R16Float,
            .mipLevelCount = mipCount,
            .arrayLayerCount = 1u,
        });
        auto emptyTexture = device->createTexture({
            .label = "SpdHiZReadbackEmpty",
            .usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::TextureBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {1u, 1u, 1u},
            .format = GVM::RHI::TextureFormat::R16Float,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        auto counter = device->createBuffer({
            .label = "SpdHiZReadbackCounter",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(uint32_t),
        });
        auto paramsBuffer = device->createBuffer({
            .label = "SpdHiZReadbackParams",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(SpdHiZReadback::Params),
        });
        EXPECT_FALSE(hiz.isNull());
        EXPECT_FALSE(emptyTexture.isNull());
        EXPECT_FALSE(counter.isNull());
        EXPECT_FALSE(paramsBuffer.isNull());

        const auto mipViews = createMipViews(hiz, emptyTexture, mipCount);
        auto bindGroup = deviceProxy->createBindGroup<SpdHiZReadback::SpdHiZBindGroup>(
            sourceDepth->createView(),
            mipViews[0],
            mipViews[1],
            mipViews[2],
            mipViews[3],
            mipViews[4],
            mipViews[5],
            mipViews[6],
            mipViews[7],
            mipViews[8],
            mipViews[9],
            mipViews[10],
            mipViews[11],
            mipViews[12],
            counter,
            paramsBuffer);
        EXPECT_TRUE(static_cast<bool>(bindGroup));

        auto pass = deviceProxy->createComputeClass<SpdHiZReadback::Pass>(bindGroup);
        EXPECT_TRUE(static_cast<bool>(pass));

        const uint32_t groupsX = ceilDiv(width, kTileSize);
        const uint32_t groupsY = ceilDiv(height, kTileSize);
        SpdHiZReadback::Params params = {};
        params.sourceSize = uint2(width, height);
        params.mipCount = std::min(mipCount, 7u);
        params.reduceMode = reduceMax ? SpdHiZReadback::ReduceMax : SpdHiZReadback::ReduceMin;
        params.workgroupCount = uint2(groupsX, groupsY);
        params.totalWorkgroupCount = groupsX * groupsY;

        auto queue = deviceProxy->graphicsQueue(0);
        queue->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)), &params, sizeof(params))
            ->fillBuffer(GVM::RHI::BufferRange(counter, 0u, sizeof(uint32_t)), 0u)
            ->computePass("SpdHiZReadbackPass", pass->run(groupsX * kLocalSizeX, groupsY * kLocalSizeY, 1u))
            ->submit();

        if (mipCount > 7u)
        {
            SpdHiZReadback::Params tailParams = params;
            tailParams.mipCount = mipCount;
            tailParams.workgroupCount = uint2(1u, 1u);
            tailParams.totalWorkgroupCount = 1u;

            queue->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(tailParams)), &tailParams, sizeof(tailParams))
                ->fillBuffer(GVM::RHI::BufferRange(counter, 0u, sizeof(uint32_t)), 0u)
                ->computePass("SpdHiZReadbackTailPass", pass->run(kLocalSizeX, kLocalSizeY, 1u))
                ->submit();
        }

        std::vector<std::vector<uint16_t>> gpuMips(mipCount);
        for (uint32_t mip = 0u; mip < mipCount; ++mip)
        {
            gpuMips[mip].resize(static_cast<size_t>(mipDim(width, mip)) * mipDim(height, mip));
            queue->readTexture(hiz, gpuMips[mip].data(), gpuMips[mip].size() * sizeof(uint16_t), mip);
        }
        queue->submit();

        pass = nullptr;
        bindGroup = nullptr;
        device->freeBuffer(paramsBuffer);
        device->freeBuffer(counter);
        device->freeTexture(emptyTexture);
        device->freeTexture(hiz);
        return gpuMips;
    }

    TEST_F(SpdHiZReadbackTest, GeneratesMinAndMaxMipChainsMatchingCpuReference)
    {
        const uint32_t mipCount = calcMipCount(kWidth, kHeight);
        ASSERT_LE(mipCount, kMaxMipBindings);
        ASSERT_GT(ceilDiv(kWidth, kTileSize) * ceilDiv(kHeight, kTileSize), 1u);
        ASSERT_GT(mipCount, 7u);

        const std::vector<float> depth = makeDepthPattern(kWidth, kHeight);
        auto sourceDepth = device->createTexture({
            .label = "SpdHiZReadbackSourceDepth",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::Depth32Float,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        ASSERT_FALSE(sourceDepth.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        deviceProxy->graphicsQueue(0)->writeTexture(sourceDepth, depth.data(), depth.size() * sizeof(float))->submit();

        const auto minGpu = runSpdHiZ(device, sourceDepth, kWidth, kHeight, false);
        const auto minCpu = buildCpuReference(depth, kWidth, kHeight, false);
        EXPECT_TRUE(mipChainsMatch(minGpu, minCpu, kWidth, kHeight, "min"));

        const auto maxGpu = runSpdHiZ(device, sourceDepth, kWidth, kHeight, true);
        const auto maxCpu = buildCpuReference(depth, kWidth, kHeight, true);
        EXPECT_TRUE(mipChainsMatch(maxGpu, maxCpu, kWidth, kHeight, "max"));

        device->freeTexture(sourceDepth);
    }
} // namespace
