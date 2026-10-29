#pragma once

#include <EASTL/optional.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::RHI::Private
{
    /** One sub-allocation inside a backend-owned upload page. */
    template <class TPage>
    struct UploadSlice
    {
        TPage page;
        uint64_t offset = 0;
        uint8_t *mappedData = nullptr;
    };

    /**
     * Sequentially sub-allocates staging slices from backend-owned upload pages and recycles a page once the
     * last submission that read it has retired; idle pages above FreeByteLimit are handed back for destruction.
     * TPage is a small value type exposing `uint64_t size` and `uint8_t *mappedData`.
     * Not thread-safe; callers hold their queue lock.
     */
    template <class TPage>
    class UploadPageAllocator
    {
    public:
        static constexpr uint64_t MinimumPageSize = 4u * 1024u * 1024u;
        static constexpr uint64_t FreeByteLimit = 32u * 1024u * 1024u;

        /**
         * Returns a slice of `size` bytes at `alignment` from the first page whose cursor fits, or an empty
         * optional when no page fits. Pages that were submitted but not retired may still receive appends
         * because the new bytes are disjoint from the ones the GPU is reading.
         */
        eastl::optional<UploadSlice<TPage>> tryAllocate(uint64_t size, uint64_t alignment)
        {
            for (PageState &state : mPages)
            {
                const uint64_t offset = alignUp(state.bytesUsed, alignment);
                if (offset > state.page.size || size > state.page.size - offset)
                {
                    continue;
                }
                state.bytesUsed = offset + size;
                state.pending = true;
                return UploadSlice<TPage>{state.page, offset, state.page.mappedData + offset};
            }
            return eastl::nullopt;
        }

        /** Returns the page capacity a request of `size` bytes needs: max(MinimumPageSize, size aligned up to 256). */
        static uint64_t pageSizeFor(uint64_t size)
        {
            const uint64_t alignedSize = alignUp(size, 256u);
            return alignedSize > MinimumPageSize ? alignedSize : MinimumPageSize;
        }

        /** Takes ownership of a freshly created, never-written page. */
        void addPage(TPage page)
        {
            PageState state = {};
            state.page = eastl::move(page);
            state.idleIndex = mNextIdleIndex++;
            mPages.push_back(eastl::move(state));
        }

        /** Stamps every page written since the last submit with the serial of the submission that carries its writes. */
        void markSubmitted(uint64_t serial)
        {
            for (PageState &state : mPages)
            {
                if (!state.pending)
                {
                    continue;
                }
                state.pending = false;
                state.lastSerial = serial;
            }
        }

        /**
         * Resets pages whose last submission has retired, then evicts idle pages, oldest-idle first, while
         * idle bytes exceed FreeByteLimit, and returns the evicted pages for the backend to destroy.
         */
        eastl::vector<TPage> reclaim(uint64_t retiredSerial)
        {
            uint64_t idleBytes = 0;
            for (PageState &state : mPages)
            {
                if (!state.pending && state.lastSerial != 0u && state.lastSerial <= retiredSerial && state.bytesUsed != 0u)
                {
                    state.bytesUsed = 0;
                    state.lastSerial = 0;
                    state.idleIndex = mNextIdleIndex++;
                }
                if (state.bytesUsed == 0u)
                {
                    idleBytes += state.page.size;
                }
            }

            eastl::vector<TPage> evicted;
            while (idleBytes > FreeByteLimit)
            {
                uint64_t oldestIndex = mPages.size();
                for (uint64_t index = 0; index < mPages.size(); ++index)
                {
                    if (mPages[index].bytesUsed != 0u)
                    {
                        continue;
                    }
                    if (oldestIndex == mPages.size() || mPages[index].idleIndex < mPages[oldestIndex].idleIndex)
                    {
                        oldestIndex = index;
                    }
                }
                if (oldestIndex == mPages.size())
                {
                    break;
                }
                idleBytes -= mPages[oldestIndex].page.size;
                evicted.push_back(eastl::move(mPages[oldestIndex].page));
                mPages.erase(mPages.begin() + oldestIndex);
            }
            return evicted;
        }

        /** Returns every page so the backend can destroy it while tearing down the queue. */
        eastl::vector<TPage> releaseAll()
        {
            eastl::vector<TPage> released;
            released.reserve(mPages.size());
            for (PageState &state : mPages)
            {
                released.push_back(eastl::move(state.page));
            }
            mPages.clear();
            return released;
        }

    private:
        /** Per-page cursor and retirement bookkeeping kept alongside the backend-owned page object. */
        struct PageState
        {
            TPage page = {};
            uint64_t bytesUsed = 0;
            uint64_t lastSerial = 0;
            uint64_t idleIndex = 0;
            bool pending = false;
        };

        /** Aligns an allocation cursor up without overflowing past the page size checks. */
        static uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }

        eastl::vector<PageState> mPages;
        uint64_t mNextIdleIndex = 0;
    };
} // namespace GVM::RHI::Private
