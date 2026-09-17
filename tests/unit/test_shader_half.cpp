#include <gtest/gtest.h>
#include <GVMCore/Private/GMath.hpp>
#include <glm/glm.hpp>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

using GVM::Core::Math::ShaderHalf;

TEST(ShaderHalfStorage, RoundTripsEveryFiniteBinary16Representation)
{
    for (uint32_t bits = 0; bits <= 0xffffu; ++bits)
    {
        const ShaderHalf half = ShaderHalf::fromBits(static_cast<uint16_t>(bits));
        const float value = float(half);
        if ((bits & 0x7c00u) == 0x7c00u && (bits & 0x3ffu) != 0) { EXPECT_TRUE(std::isnan(value)); }
        else { EXPECT_EQ(ShaderHalf(value).bits, bits) << "representation=" << bits; }
    }
}

TEST(ShaderHalfStorage, PreservesIEEEBoundariesAndTiesToEven)
{
    EXPECT_EQ(ShaderHalf(1.0f).bits, 0x3c00u);
    EXPECT_EQ(ShaderHalf(-0.0f).bits, 0x8000u);
    EXPECT_EQ(ShaderHalf(1.0f + 1.0f / 2048.0f).bits, 0x3c00u);
    EXPECT_EQ(ShaderHalf(1.0f + 3.0f / 2048.0f).bits, 0x3c02u);
    EXPECT_EQ(ShaderHalf(65504.0f).bits, 0x7bffu);
    EXPECT_EQ(ShaderHalf(65520.0f).bits, 0x7c00u);
    EXPECT_EQ(ShaderHalf(std::ldexp(1.0f, -25)).bits, 0u);
    EXPECT_EQ(ShaderHalf(std::ldexp(3.0f, -25)).bits, 2u);
    EXPECT_EQ(float(std::numeric_limits<ShaderHalf>::denorm_min()), std::ldexp(1.0f, -24));
    EXPECT_EQ(float(std::numeric_limits<ShaderHalf>::min()), std::ldexp(1.0f, -14));
    EXPECT_EQ(float(std::numeric_limits<ShaderHalf>::epsilon()), std::ldexp(1.0f, -10));
    EXPECT_EQ(std::bit_cast<uint32_t>(float(ShaderHalf::fromBits(0x8000))), 0x80000000u);
}

TEST(ShaderHalfStorage, SupportsHostMathAndTrivialValueInitialization)
{
    static_assert(std::is_trivially_default_constructible_v<ShaderHalf>);
    static_assert(std::is_trivially_copyable_v<ShaderHalf>);
    const ShaderHalf zero{};
    const ShaderHalf four = 4.0f;
    const ShaderHalf root = std::sqrt(four);
    const ShaderHalf power = std::pow(four, ShaderHalf(0.5f));
    EXPECT_EQ(zero.bits, 0u);
    EXPECT_EQ(float(root), 2.0f);
    EXPECT_EQ(float(power), 2.0f);
}

TEST(ShaderHalfStorage, PreservesPackedVectorLayoutAndConvertsHostValues)
{
    using Half3 = glm::vec<3, ShaderHalf, glm::packed_highp>;
    static_assert(sizeof(Half3) == 6);
    static_assert(alignof(Half3) == 2);
    static_assert(std::is_default_constructible_v<Half3>);
    const glm::vec3 input(1.0f, 2.0f, 3.0f);
    const Half3 converted = GVM::Core::Math::convertShaderSwizzle<Half3>(input);
    EXPECT_EQ(float(converted.x), 1.0f);
    EXPECT_EQ(float(converted.y), 2.0f);
    EXPECT_EQ(float(converted.z), 3.0f);
    const Half3 scalar(0.5f);
    EXPECT_EQ(float(scalar.z), 0.5f);
}
