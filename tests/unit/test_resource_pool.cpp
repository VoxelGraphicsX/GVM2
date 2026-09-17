#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include <Utils/ResourcePool.hpp>

namespace
{
    class DummyResource final : public GVM::RHI::ResourceObject
    {
    public:
        explicit DummyResource(const char *label)
        {
            mLabelName = label;
        }
    };

    TEST(ResourcePoolTests, AllocatesAndResolvesHandle)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        const auto handle = pool.alloc(new DummyResource("Alpha"));
        ASSERT_NE(handle.get(), nullptr);
        EXPECT_EQ(handle->getLabelName(), "Alpha");

        pool.freeByHandle(handle);
    }

    TEST(ResourcePoolTests, RejectsNullAllocations)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        EXPECT_THROW(
            {
                [[maybe_unused]] const auto ignored = pool.alloc(nullptr);
            },
            std::invalid_argument);
    }

    TEST(ResourcePoolTests, FreedHandleBecomesStale)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        const auto handle = pool.alloc(new DummyResource("Beta"));
        ASSERT_NE(handle.get(), nullptr);

        pool.freeByHandle(handle);

        EXPECT_EQ(handle.get(), nullptr);
    }

    TEST(ResourcePoolTests, ReusesFreedSlotWithFreshGeneration)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        const auto firstHandle = pool.alloc(new DummyResource("Gamma"));
        ASSERT_NE(firstHandle.get(), nullptr);

        pool.freeByHandle(firstHandle);
        EXPECT_EQ(firstHandle.get(), nullptr);

        const auto secondHandle = pool.alloc(new DummyResource("Delta"));
        ASSERT_NE(secondHandle.get(), nullptr);
        EXPECT_EQ(secondHandle->getLabelName(), "Delta");
        EXPECT_FALSE(firstHandle == secondHandle);

        pool.freeByHandle(secondHandle);
    }

    TEST(ResourcePoolTests, HandleResetMarksItNull)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        auto handle = pool.alloc(new DummyResource("Epsilon"));
        ASSERT_FALSE(handle.isNull());

        handle.reset();

        EXPECT_TRUE(handle.isNull());
        EXPECT_EQ(handle.get(), nullptr);
    }

    TEST(ResourcePoolTests, ForeignPoolHandleDoesNotResolveOrFree)
    {
        xGE::Utils::ResourcePool<DummyResource> ownerPool;
        xGE::Utils::ResourcePool<DummyResource> foreignPool;
        ownerPool.init();
        foreignPool.init();

        const auto handle = ownerPool.alloc(new DummyResource("Foreign"));
        ASSERT_NE(handle.get(), nullptr);

        EXPECT_EQ(foreignPool.getByHandle(handle), nullptr);
        foreignPool.freeByHandle(handle);
        EXPECT_NE(handle.get(), nullptr);

        ownerPool.freeByHandle(handle);
    }

    TEST(ResourcePoolTests, GrowsBeyondInitialCapacityWithoutInvalidatingLiveHandles)
    {
        xGE::Utils::ResourcePool<DummyResource> pool;
        pool.init();

        eastl::vector<xGE::Utils::ResourceHandle<DummyResource>> handles;
        handles.reserve(192);
        for (int index = 0; index < 192; ++index)
        {
            handles.push_back(pool.alloc(new DummyResource("Growth")));
        }

        ASSERT_NE(handles.front().get(), nullptr);
        ASSERT_NE(handles.back().get(), nullptr);
        EXPECT_EQ(handles.front()->getLabelName(), "Growth");
        EXPECT_EQ(handles.back()->getLabelName(), "Growth");

        for (const auto &handle : handles)
        {
            pool.freeByHandle(handle);
        }
    }
} // namespace
