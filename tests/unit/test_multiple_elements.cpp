#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>

#include <array>

namespace
{
    TEST(MultipleElementsTests, StoresValuesFromInitializerList)
    {
        GVM::RHI::MultipleElements<int> values{1, 2, 3};

        ASSERT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[2], 3);
    }

    TEST(MultipleElementsTests, SupportsScalarAndRangeAssignment)
    {
        GVM::RHI::MultipleElements<int> values{1, 2, 3};

        values = 7;
        ASSERT_EQ(values.size(), 1u);
        EXPECT_EQ(values[0], 7);

        const std::array<int, 4> replacement = {4, 5, 6, 7};
        values = replacement;

        ASSERT_EQ(values.size(), replacement.size());
        EXPECT_EQ(values[0], 4);
        EXPECT_EQ(values[3], 7);
    }

    TEST(MultipleElementsTests, SupportsVariadicConstructionAndContiguousAccess)
    {
        GVM::RHI::MultipleElements<int> values(9, 8, 7, 6);

        ASSERT_EQ(values.size(), 4u);
        ASSERT_NE(values.data(), nullptr);
        EXPECT_EQ(values.data()[0], 9);
        EXPECT_EQ(values.data()[1], 8);
        EXPECT_EQ(values.data()[2], 7);
        EXPECT_EQ(values.data()[3], 6);
    }
} // namespace
