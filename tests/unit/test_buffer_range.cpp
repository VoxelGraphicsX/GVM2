#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>

#include <vector>

namespace
{
    class DummyBuffer final : public GVM::RHI::BufferImpl
    {
    public:
        explicit DummyBuffer(uint64_t storageSize, const char *label)
            : mStorage(storageSize, 0)
        {
            mLabelName = label;
        }

        uint64_t getStorageSize() const override
        {
            return mStorage.size();
        }

        void map() override {}

        void const *getConstMappedRange(uint64_t offset, uint64_t) const override
        {
            return mStorage.data() + offset;
        }

        void *getMappedRange(uint64_t offset, uint64_t) const override
        {
            return const_cast<unsigned char *>(mStorage.data() + offset);
        }

        void unmap() override {}

        void destroy() override {}

    private:
        std::vector<unsigned char> mStorage;
    };

    TEST(BufferRangeTests, WholeSizeClampsToBufferStorage)
    {
        GVM::RHI::BufferPool pool;
        pool.init();

        const auto handle = pool.alloc(new DummyBuffer(64, "DummyBuffer"));
        const GVM::RHI::BufferRange range(handle, 0);

        EXPECT_EQ(range.offset, 0u);
        EXPECT_EQ(range.size, 64u);

        pool.freeByHandle(handle);
    }

    TEST(BufferRangeTests, ExplicitSizeStaysUnchangedWhenInsideBounds)
    {
        GVM::RHI::BufferPool pool;
        pool.init();

        const auto handle = pool.alloc(new DummyBuffer(128, "SizedBuffer"));
        const GVM::RHI::BufferRange range(handle, 16, 32);

        EXPECT_EQ(range.offset, 16u);
        EXPECT_EQ(range.size, 32u);

        pool.freeByHandle(handle);
    }

    TEST(BufferRangeTests, ExplicitZeroSizeRemainsZero)
    {
        GVM::RHI::BufferPool pool;
        pool.init();

        const auto handle = pool.alloc(new DummyBuffer(256, "ZeroSizedBuffer"));
        const GVM::RHI::BufferRange range(handle, 48, 0);

        EXPECT_EQ(range.offset, 48u);
        EXPECT_EQ(range.size, 0u);

        pool.freeByHandle(handle);
    }

    TEST(BufferRangeTests, WholeSizeWithOffsetUsesRemainingBytes)
    {
        GVM::RHI::BufferPool pool;
        pool.init();

        const auto handle = pool.alloc(new DummyBuffer(128, "OffsetWholeSizeBuffer"));
        const GVM::RHI::BufferRange range(handle, 24, GVM::RHI::WholeSize);

        EXPECT_EQ(range.offset, 24u);
        EXPECT_EQ(range.size, 104u);

        pool.freeByHandle(handle);
    }

    TEST(TimestampQueryTests, DefaultPassDescriptorsDoNotWriteTimestamps)
    {
        const GVM::RHI::RenderPassDescriptor renderPass = {};
        const GVM::RHI::ComputePassDescriptor computePass = {};
        const GVM::RHI::BlitPassDescriptor blitPass = {};

        EXPECT_TRUE(renderPass.timestampWrites.querySet == nullptr);
        EXPECT_EQ(renderPass.timestampWrites.beginningOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);
        EXPECT_EQ(renderPass.timestampWrites.endOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);

        EXPECT_TRUE(computePass.timestampWrites.querySet == nullptr);
        EXPECT_EQ(computePass.timestampWrites.beginningOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);
        EXPECT_EQ(computePass.timestampWrites.endOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);

        EXPECT_TRUE(blitPass.timestampWrites.querySet == nullptr);
        EXPECT_EQ(blitPass.timestampWrites.beginningOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);
        EXPECT_EQ(blitPass.timestampWrites.endOfPassWriteIndex, GVM::RHI::QuerySetIndexUndefined);
    }

    TEST(TimestampQueryTests, DurationUsesTickPeriodForFullWidthCounters)
    {
        GVM::RHI::TimestampQuerySupport support = {};
        support.validBits = 64u;
        support.tickPeriodNs = 3.5;

        EXPECT_EQ(GVM::RHI::calculateTimestampDeltaRaw(support, 10u, 14u), 4u);
        EXPECT_DOUBLE_EQ(GVM::RHI::calculateTimestampDurationNs(support, 10u, 14u), 14.0);
    }

    TEST(TimestampQueryTests, DeltaHandlesValidBitWrap)
    {
        GVM::RHI::TimestampQuerySupport support = {};
        support.validBits = 8u;
        support.tickPeriodNs = 2.0;

        EXPECT_EQ(GVM::RHI::calculateTimestampDeltaRaw(support, 250u, 5u), 11u);
        EXPECT_DOUBLE_EQ(GVM::RHI::calculateTimestampDurationNs(support, 250u, 5u), 22.0);
    }
} // namespace
