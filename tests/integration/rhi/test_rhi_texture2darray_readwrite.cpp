#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 3u;
    constexpr uint32_t kLayerCount = 3u;
    constexpr uint64_t kLayerByteSize = static_cast<uint64_t>(kWidth) * kHeight * sizeof(uint16_t);

    /** Returns a short lowercase name for diagnostics emitted by the texture-array runtime test. */
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

    /** Converts a finite positive float to IEEE-754 binary16 bits using round-to-nearest behavior. */
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

    /** Converts IEEE-754 binary16 bits to float for readable assertion messages. */
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

    /** Returns the exact half-float bit pattern expected from the DSL compute pass. */
    uint16_t expectedHalfBits(uint32_t x, uint32_t y, uint32_t layer)
    {
        const uint32_t texelOrdinal = y * kWidth + x;
        const float value = 0.125f + static_cast<float>(layer) * 0.25f + static_cast<float>(texelOrdinal) * 0.0009765625f;
        return floatToHalfBits(value);
    }

    /** Formats a half-float payload as hex bits plus decoded float for assertion output. */
    std::string formatHalfBits(uint16_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::setw(4) << std::setfill('0') << value << " (" << std::dec << halfBitsToFloat(value) << ")";
        return stream.str();
    }

    /**
     * Owns a headless RHI device for the RWTexture2DArray DSL runtime validation.
     * The same generated shader source is run through the backend selected by the Node test runner.
     */
    class RhiTexture2DArrayReadWriteTest : public ::testing::Test
    {
    protected:
        /** Creates the test instance and skips unsupported backends before each test body runs. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "Texture2DArray runtime validation requires Metal or Vulkan backend, got " << backendName(backend);
            }

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        /** Releases the RHI device and instance after each test body finishes. */
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

    TEST_F(RhiTexture2DArrayReadWriteTest, DispatchesRWTexture2DArrayWritesAndReadsBackLayers)
    {
        auto outputTexture = device->createTexture({
            .label = "Texture2DArrayReadWriteOutput",
            .usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::R16Float,
            .mipLevelCount = 1u,
            .arrayLayerCount = kLayerCount,
        });
        ASSERT_FALSE(outputTexture.isNull());

        GVM::RHI::TextureViewDescriptor outputViewDescriptor = {};
        outputViewDescriptor.label = "Texture2DArrayReadWriteOutputView";
        outputViewDescriptor.format = GVM::RHI::TextureFormat::R16Float;
        outputViewDescriptor.dimension = GVM::RHI::TextureViewDimension::e2DArray;
        outputViewDescriptor.baseMipLevel = 0u;
        outputViewDescriptor.mipLevelCount = 1u;
        outputViewDescriptor.baseArrayLayer = 0u;
        outputViewDescriptor.arrayLayerCount = kLayerCount;

        auto outputView = outputTexture->createView(outputViewDescriptor);
        ASSERT_FALSE(outputView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<Texture2DArrayReadWriteTest::Texture2DArrayReadWriteBindGroup>(outputView);
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto pass = deviceProxy->createComputeClass<Texture2DArrayReadWriteTest::Texture2DArrayReadWritePass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(pass));

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->computePass("Texture2DArrayReadWritePass", pass->run(kWidth, kHeight, kLayerCount))->submit();

        for (uint32_t layer = 0u; layer < kLayerCount; ++layer)
        {
            std::vector<uint16_t> layerData(kWidth * kHeight, 0u);
            queue->readTexture(outputTexture, layerData.data(), kLayerByteSize, 0u, layer)->submit();

            for (uint32_t y = 0u; y < kHeight; ++y)
            {
                for (uint32_t x = 0u; x < kWidth; ++x)
                {
                    const uint32_t index = y * kWidth + x;
                    const uint16_t expected = expectedHalfBits(x, y, layer);
                    const uint16_t actual = layerData[index];
                    EXPECT_EQ(actual, expected) << "layer=" << layer << " texel=(" << x << "," << y << ") expected="
                                                << formatHalfBits(expected) << " actual=" << formatHalfBits(actual);
                }
            }
        }

        pass = nullptr;
        bindGroup = nullptr;
        device->freeTexture(outputTexture);
    }
} // namespace
