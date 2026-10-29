#include <gtest/gtest.h>

#include "GHelperFunction.hpp"

namespace
{
    TEST(HelperFunctionTests, WaveReadAcrossXReturnsInputUnchanged)
    {
        EXPECT_EQ(GVM::Core::Math::WaveReadAcrossX(42), 42);
        EXPECT_FLOAT_EQ(GVM::Core::Math::WaveReadAcrossX(3.5f), 3.5f);
    }

    TEST(HelperFunctionTests, WaveReadAcrossYReturnsInputUnchanged)
    {
        EXPECT_EQ(GVM::Core::Math::WaveReadAcrossY(-7), -7);
        EXPECT_DOUBLE_EQ(GVM::Core::Math::WaveReadAcrossY(9.25), 9.25);
    }
} // namespace
