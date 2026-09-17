#include <gtest/gtest.h>

#include <GProgressiveData.hpp>

#include <array>
#include <thread>
#include <vector>

namespace
{
    TEST(ProgressiveDataTests, CreatesAndResizesWithConfiguredIncrement)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "Numbers", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 4}, 7);

        ASSERT_EQ(data.getLength(), 4u);
        EXPECT_EQ(data.read(0), 7);

        data.write(5, 11);

        EXPECT_EQ(data.read(5), 11);
        EXPECT_EQ(data.getLength(), 8u);
        EXPECT_EQ(data.read(0), 7);
    }

    TEST(ProgressiveDataTests, FillRangeUpdatesElementsInPlace)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "FillRange", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 8}, 0);

        data.fillRange(3, 2, 3);

        EXPECT_EQ(data.read(1), 0);
        EXPECT_EQ(data.read(2), 3);
        EXPECT_EQ(data.read(3), 3);
        EXPECT_EQ(data.read(4), 3);
        EXPECT_EQ(data.read(5), 0);
    }

    TEST(ProgressiveDataTests, ClearResetsLengthAndByteSize)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "Clearable", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 4}, 1);
        data.write(6, 9);

        ASSERT_GT(data.getLength(), 0u);
        ASSERT_GT(data.getByteSize(), 0u);

        data.clear();

        EXPECT_EQ(data.getLength(), 0u);
        EXPECT_EQ(data.getByteSize(), 0u);
    }

    TEST(ProgressiveDataTests, WriteRangeResizesAndPreservesPreviouslyWrittenElements)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "ResizePreserves", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 2}, 0);

        data.write(0, 3);
        data.write(1, 5);

        const std::array<int, 3> values = {11, 13, 17};
        data.writeRange(6, values.data(), values.size());

        ASSERT_EQ(data.getLength(), 10u);
        EXPECT_EQ(data.read(0), 3);
        EXPECT_EQ(data.read(1), 5);
        EXPECT_EQ(data.read(6), 11);
        EXPECT_EQ(data.read(7), 13);
        EXPECT_EQ(data.read(8), 17);
    }

    TEST(ProgressiveDataTests, CopyRangeToProducesStableSnapshot)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "Snapshot", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 4}, 1);

        const std::array<int, 4> values = {9, 8, 7, 6};
        data.writeRange(2, values.data(), values.size());

        std::array<int, 4> snapshot = {};
        data.copyRangeTo(2, snapshot.data(), snapshot.size());

        data.write(2, 42);

        EXPECT_EQ(snapshot[0], 9);
        EXPECT_EQ(snapshot[1], 8);
        EXPECT_EQ(snapshot[2], 7);
        EXPECT_EQ(snapshot[3], 6);
        EXPECT_EQ(data.read(2), 42);
    }

    TEST(ProgressiveDataTests, FillRangeThrowsWhenTargetRangeIsOutOfBounds)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "Bounds", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 4}, 0);

        EXPECT_THROW(data.fillRange(1, 3, 4), std::out_of_range);
    }

    TEST(ProgressiveDataTests, ReadThrowsForMissingIndex)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "ReadBounds", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 2}, 0);

        EXPECT_THROW(data.read(4), std::out_of_range);
    }

    TEST(ProgressiveDataTests, ConcurrentWritesToDistinctIndicesRemainConsistent)
    {
        GVM::Core::ProgressiveData<int> data;
        data.create({.dataName = "Concurrent", .dataElementStorageSize = sizeof(int), .dataElementIncreamentCount = 8}, -1);

        constexpr int kThreadCount = 4;
        constexpr int kValuesPerThread = 32;
        std::vector<std::thread> workers;
        workers.reserve(kThreadCount);

        for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
        {
            workers.emplace_back([&, threadIndex] {
                const int baseIndex = threadIndex * kValuesPerThread;
                for (int valueIndex = 0; valueIndex < kValuesPerThread; ++valueIndex)
                {
                    data.write(baseIndex + valueIndex, baseIndex + valueIndex);
                }
            });
        }

        for (auto &worker : workers)
        {
            worker.join();
        }

        for (int index = 0; index < kThreadCount * kValuesPerThread; ++index)
        {
            EXPECT_EQ(data.read(index), index);
        }
    }
} // namespace
