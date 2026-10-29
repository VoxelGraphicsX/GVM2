#include <gtest/gtest.h>

#include <ArgumentPagePool.hpp>

#include <EASTL/vector.h>
#include <stdexcept>

namespace
{
    using GVM::RHI::Private::ArgumentPagePool;

    /** Stands in for a native parameter page; only the size field is consumed by the pool. */
    struct FakePage
    {
        uint64_t size = 0;
    };

    /** Implements the page-pool hooks without a GPU so accounting and wait behavior can be unit tested. */
    class FakeArgumentPagePool : public ArgumentPagePool<FakePage>
    {
    public:
        /** Destroys pooled free pages through the fake hook while the hook target still exists. */
        ~FakeArgumentPagePool() override { destroyFreePages(); }

        /** Controls what the fake wait hook reports to acquire. */
        bool waitResult = true;
        /** Pages released with submitted accounting the next time the wait hook runs. */
        eastl::vector<FakePage> pendingPages;

    protected:
        /** Fakes a native page creation by returning a sized placeholder. */
        FakePage createPage(uint64_t size) override { return {size}; }
        /** Fakes native page destruction by clearing the placeholder. */
        void destroyPage(FakePage &page) override { page = {}; }
        /** Releases any pending pages as completed work, then reports the configured wait result. */
        bool waitForSubmittedWork() override
        {
            if (!pendingPages.empty())
            {
                release(eastl::span<FakePage>(pendingPages.data(), pendingPages.size()), true);
                pendingPages.clear();
            }
            return waitResult;
        }
    };

    /** Rejects acquire when accounting still counts in-flight pages but no submitted work remains. */
    TEST(ArgumentPagePool, ThrowsLogicErrorWhenInFlightPagesHaveNoPendingWork)
    {
        FakeArgumentPagePool pool;
        pool.setLimit(FakeArgumentPagePool::StandardPageSize);
        FakePage first = pool.acquire(1);
        pool.markSubmitted(first.size);

        pool.waitResult = false;
        EXPECT_THROW(pool.acquire(1), std::logic_error);

        eastl::vector<FakePage> pages{first};
        pool.release(eastl::span<FakePage>(pages.data(), pages.size()), true);
    }

    /** Lets acquire reuse a page after the wait hook reports that submitted work released it. */
    TEST(ArgumentPagePool, AcquireSucceedsWhenSubmittedWorkReleasesPages)
    {
        FakeArgumentPagePool pool;
        pool.setLimit(FakeArgumentPagePool::StandardPageSize);
        FakePage first = pool.acquire(1);
        pool.markSubmitted(first.size);
        pool.pendingPages.push_back(first);

        FakePage second = pool.acquire(1);
        EXPECT_EQ(second.size, FakeArgumentPagePool::StandardPageSize);
        EXPECT_EQ(pool.statistics().pageAllocationCount, 1u);

        eastl::vector<FakePage> pages{second};
        pool.release(eastl::span<FakePage>(pages.data(), pages.size()), false);
    }
}
