#include <gtest/gtest.h>

#include <UploadPageAllocator.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    /** Minimal fake page satisfying the UploadPageAllocator contract for CPU-only tests. */
    struct FakePage
    {
        uint64_t size = 0;
        uint8_t *mappedData = nullptr;
        uint64_t id = 0;
        eastl::vector<uint8_t> bytes;
    };

    /** Builds a fake page with a distinct identity and a CPU-backed mapped range. */
    FakePage makeFakePage(uint64_t size, uint64_t id)
    {
        FakePage page = {};
        page.size = size;
        page.id = id;
        page.bytes.resize(size);
        page.mappedData = page.bytes.data();
        return page;
    }

    /** Finds the first page id in a returned page list. */
    bool containsPageId(const eastl::vector<FakePage> &pages, uint64_t id)
    {
        for (const FakePage &page : pages)
        {
            if (page.id == id)
            {
                return true;
            }
        }
        return false;
    }

    TEST(UploadPageAllocatorTests, DoesNotReusePagesBeforeRetirement)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t pageSize = GVM::RHI::Private::UploadPageAllocator<FakePage>::MinimumPageSize;
        allocator.addPage(makeFakePage(pageSize, 1u));

        const auto first = allocator.tryAllocate(pageSize, 256u);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->offset, 0u);

        EXPECT_FALSE(allocator.tryAllocate(1u, 256u).has_value());

        allocator.markSubmitted(1u);
        allocator.reclaim(0u);
        EXPECT_FALSE(allocator.tryAllocate(1u, 256u).has_value());
    }

    TEST(UploadPageAllocatorTests, ReusesPagesAfterRetirement)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t pageSize = GVM::RHI::Private::UploadPageAllocator<FakePage>::MinimumPageSize;
        allocator.addPage(makeFakePage(pageSize, 1u));

        ASSERT_TRUE(allocator.tryAllocate(pageSize, 256u).has_value());
        allocator.markSubmitted(1u);

        const eastl::vector<FakePage> evicted = allocator.reclaim(1u);
        EXPECT_TRUE(evicted.empty());

        const auto second = allocator.tryAllocate(64u, 256u);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->offset, 0u);
        EXPECT_EQ(second->page.id, 1u);
    }

    TEST(UploadPageAllocatorTests, AllowsAppendingToSubmittedButNotRetiredPages)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t pageSize = GVM::RHI::Private::UploadPageAllocator<FakePage>::MinimumPageSize;
        allocator.addPage(makeFakePage(pageSize, 1u));

        const auto first = allocator.tryAllocate(1024u, 256u);
        ASSERT_TRUE(first.has_value());
        allocator.markSubmitted(1u);

        const auto second = allocator.tryAllocate(512u, 256u);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->page.id, 1u);
        EXPECT_EQ(second->offset, 1024u);

        allocator.markSubmitted(2u);
        EXPECT_TRUE(allocator.reclaim(1u).empty());
        EXPECT_FALSE(allocator.tryAllocate(pageSize, 256u).has_value());

        EXPECT_TRUE(allocator.reclaim(2u).empty());
        const auto third = allocator.tryAllocate(64u, 256u);
        ASSERT_TRUE(third.has_value());
        EXPECT_EQ(third->offset, 0u);
    }

    TEST(UploadPageAllocatorTests, LargeRequestGetsItsOwnPage)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t minimumPageSize = GVM::RHI::Private::UploadPageAllocator<FakePage>::MinimumPageSize;

        allocator.addPage(makeFakePage(GVM::RHI::Private::UploadPageAllocator<FakePage>::pageSizeFor(minimumPageSize + 256u), 7u));

        const auto slice = allocator.tryAllocate(minimumPageSize + 256u, 256u);
        ASSERT_TRUE(slice.has_value());
        EXPECT_EQ(slice->offset, 0u);
        EXPECT_EQ(slice->page.id, 7u);
        EXPECT_EQ(slice->page.size, minimumPageSize + 256u);

        EXPECT_EQ(GVM::RHI::Private::UploadPageAllocator<FakePage>::pageSizeFor(minimumPageSize + 1u), minimumPageSize + 256u);
        EXPECT_EQ(GVM::RHI::Private::UploadPageAllocator<FakePage>::pageSizeFor(16u), minimumPageSize);
    }

    TEST(UploadPageAllocatorTests, EvictsOldestIdlePagesBeyondTheFreeByteLimit)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t freeByteLimit = GVM::RHI::Private::UploadPageAllocator<FakePage>::FreeByteLimit;
        const uint64_t pageSize = freeByteLimit / 4u;

        allocator.addPage(makeFakePage(pageSize, 1u));
        allocator.addPage(makeFakePage(pageSize, 2u));
        allocator.addPage(makeFakePage(pageSize, 3u));
        allocator.addPage(makeFakePage(pageSize, 4u));
        allocator.addPage(makeFakePage(pageSize, 5u));

        for (uint64_t index = 0; index < 5u; ++index)
        {
            ASSERT_TRUE(allocator.tryAllocate(8u, 8u).has_value());
        }
        allocator.markSubmitted(1u);

        // All slices landed in page 1 (first fit), so pages 2-5 have been idle since addPage and page 2 is
        // the oldest idle page once page 1 resets.
        const eastl::vector<FakePage> evicted = allocator.reclaim(1u);
        ASSERT_EQ(evicted.size(), 1u);
        EXPECT_EQ(evicted.front().id, 2u);
    }

    TEST(UploadPageAllocatorTests, ReleaseAllReturnsEveryPage)
    {
        GVM::RHI::Private::UploadPageAllocator<FakePage> allocator;
        const uint64_t pageSize = GVM::RHI::Private::UploadPageAllocator<FakePage>::MinimumPageSize;

        allocator.addPage(makeFakePage(pageSize, 1u));
        allocator.addPage(makeFakePage(pageSize, 2u));
        ASSERT_TRUE(allocator.tryAllocate(128u, 128u).has_value());
        allocator.markSubmitted(1u);

        eastl::vector<FakePage> released = allocator.releaseAll();
        ASSERT_EQ(released.size(), 2u);
        EXPECT_TRUE(containsPageId(released, 1u));
        EXPECT_TRUE(containsPageId(released, 2u));

        EXPECT_TRUE(allocator.releaseAll().empty());
        EXPECT_FALSE(allocator.tryAllocate(1u, 1u).has_value());
    }
} // namespace
