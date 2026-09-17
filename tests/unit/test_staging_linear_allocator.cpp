#include <gtest/gtest.h>

#include "GStagingLinearAllocator.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace
{
    TEST(StagingLinearAllocatorTests, AppendsDataWithConfiguredAlignment)
    {
        GVM::Core::StagingLinearAllocator allocator;

        const std::array<uint32_t, 2> first = {11u, 22u};
        const std::array<uint32_t, 2> second = {33u, 44u};

        const auto firstOffset = allocator.append(first.data(), first.size(), 16);
        const auto secondOffset = allocator.append(second.data(), second.size(), 16);

        EXPECT_EQ(firstOffset, 0u);
        EXPECT_EQ(secondOffset, 16u);
        ASSERT_EQ(allocator.size(), 24u);

        const auto *bytes = reinterpret_cast<const uint32_t *>(allocator.data());
        EXPECT_EQ(bytes[0], 11u);
        EXPECT_EQ(bytes[1], 22u);

        const auto *alignedSecond = reinterpret_cast<const uint32_t *>(allocator.data() + secondOffset);
        EXPECT_EQ(alignedSecond[0], 33u);
        EXPECT_EQ(alignedSecond[1], 44u);
    }

    TEST(StagingLinearAllocatorTests, ResetClearsLogicalContents)
    {
        GVM::Core::StagingLinearAllocator allocator;
        const std::array<uint8_t, 4> bytes = {1u, 2u, 3u, 4u};

        allocator.append(bytes.data(), bytes.size(), 4);
        ASSERT_FALSE(allocator.empty());

        allocator.reset();

        EXPECT_TRUE(allocator.empty());
        EXPECT_EQ(allocator.size(), 0u);
    }

    TEST(StagingLinearAllocatorTests, AppendRawRejectsNullDataForNonZeroByteCount)
    {
        GVM::Core::StagingLinearAllocator allocator;

        EXPECT_THROW(static_cast<void>(allocator.appendRaw(nullptr, 8, 8)), std::runtime_error);
    }

    TEST(StagingLinearAllocatorTests, AppendRawWithZeroBytesIsANoOp)
    {
        GVM::Core::StagingLinearAllocator allocator;
        const std::array<uint8_t, 4> bytes = {9u, 8u, 7u, 6u};

        allocator.append(bytes.data(), bytes.size(), 4);
        const auto sizeBeforeNoOp = allocator.size();

        EXPECT_EQ(allocator.appendRaw(bytes.data(), 0, 4), sizeBeforeNoOp);
        EXPECT_EQ(allocator.size(), sizeBeforeNoOp);
    }
} // namespace
