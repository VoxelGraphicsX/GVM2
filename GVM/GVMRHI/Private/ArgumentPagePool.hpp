#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <EASTL/atomic.h>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace GVM::RHI::Private
{
    /**
     * Pools reusable 64 KiB Args parameter pages for one device and owns all Args memory accounting.
     * TPage must expose a `uint64_t size` member. Backend hooks createPage, destroyPage and
     * waitForSubmittedWork are always called without the pool mutex held.
     */
    template <class TPage>
    class ArgumentPagePool : public RefCountedObject
    {
    public:
        /** Standard pooled page size; larger requests receive dedicated pages that are destroyed after use. */
        static constexpr uint64_t StandardPageSize = 64u * 1024u;
        /** Maximum pooled free bytes kept for reuse; surplus standard pages are destroyed on release. */
        static constexpr uint64_t FreeByteLimit = 1024u * 1024u;

        /** Adds CPU snapshot bytes; snapshots are counted for diagnostics and never limited. */
        void addSnapshotBytes(uint64_t bytes) { mSnapshotBytes.fetch_add(bytes, eastl::memory_order_relaxed); }
        /** Removes CPU snapshot bytes when a snapshot is released. */
        void removeSnapshotBytes(uint64_t bytes) { mSnapshotBytes.fetch_sub(bytes, eastl::memory_order_relaxed); }

        /**
         * Returns a parameter page of at least minBytes by reusing a pooled page, allocating within the
         * limit, recycling pooled pages, or waiting for the oldest submitted work. Throws
         * ArgumentMemoryExhausted only when no submitted work can still release pages, and
         * std::logic_error when accounting reports in-flight pages without pending GPU work.
         */
        TPage acquire(uint64_t minBytes)
        {
            const uint64_t size = eastl::max<uint64_t>(StandardPageSize, (minBytes + 255u) & ~uint64_t(255u));
            for (;;)
            {
                Step step = Step::Throw;
                TPage page{};
                {
                    std::unique_lock<std::mutex> lock(mMutex);
                    if (size == StandardPageSize && !mFreePages.empty())
                    {
                        page = eastl::move(mFreePages.back());
                        mFreePages.pop_back();
                        mFreeBytes -= StandardPageSize;
                        mRecordingBytes += StandardPageSize;
                        return page;
                    }
                    const uint64_t limit = mLimit.load(eastl::memory_order_relaxed);
                    if (size <= limit && mRecordingBytes + mInFlightBytes + mFreeBytes + size <= limit)
                    {
                        mRecordingBytes += size;
                        step = Step::Create;
                    }
                    else if (size > limit)
                    {
                        step = Step::Throw;
                    }
                    else if (!mFreePages.empty())
                    {
                        step = Step::Recycle;
                        page = eastl::move(mFreePages.back());
                        mFreePages.pop_back();
                        mFreeBytes -= page.size;
                    }
                    else if (mInFlightBytes != 0)
                    {
                        step = Step::Wait;
                    }
                }
                switch (step)
                {
                    case Step::Create:
                    {
                        try
                        {
                            page = createPage(size);
                        }
                        catch (...)
                        {
                            std::lock_guard<std::mutex> lock(mMutex);
                            mRecordingBytes -= size;
                            throw;
                        }
                        mAllocationCount.fetch_add(1, eastl::memory_order_relaxed);
                        return page;
                    }
                    case Step::Recycle:
                        destroyPage(page);
                        continue;
                    case Step::Wait:
                        if (!waitForSubmittedWork())
                        {
                            std::lock_guard<std::mutex> lock(mMutex);
                            if (mInFlightBytes != 0)
                            {
                                throw std::logic_error(
                                    "Args page accounting reports in-flight pages without pending GPU work.");
                            }
                        }
                        continue;
                    case Step::Throw:
                    default:
                        throw ArgumentMemoryExhausted(
                            "Args parameter pages exceed the argument memory budget; submit or discard recorded work, or raise the budget.");
                }
            }
        }

        /** Moves page bytes owned by one storage from recording to in-flight when its encoder reaches the GPU. */
        void markSubmitted(uint64_t bytes)
        {
            if (bytes == 0)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(mMutex);
            mRecordingBytes -= bytes;
            mInFlightBytes += bytes;
        }

        /**
         * Returns a storage's pages; submitted work decrements in-flight bytes and everything else
         * decrements recording bytes. Standard-size pages are pooled while free bytes stay within
         * FreeByteLimit; oversized pages and surplus pages are destroyed.
         */
        void release(eastl::span<TPage> pages, bool submitted)
        {
            eastl::vector<TPage> retired;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                uint64_t bytes = 0;
                for (const TPage &page : pages)
                {
                    bytes += page.size;
                }
                if (submitted)
                {
                    mInFlightBytes -= bytes;
                }
                else
                {
                    mRecordingBytes -= bytes;
                }
                for (TPage &page : pages)
                {
                    if (page.size == StandardPageSize && mFreeBytes + StandardPageSize <= FreeByteLimit)
                    {
                        mFreeBytes += StandardPageSize;
                        mFreePages.push_back(eastl::move(page));
                    }
                    else
                    {
                        retired.push_back(eastl::move(page));
                    }
                }
            }
            for (TPage &page : retired)
            {
                destroyPage(page);
            }
            mCondition.notify_all();
        }

        /** Destroys every pooled free page; call before native device teardown. */
        void destroyFreePages()
        {
            eastl::vector<TPage> retired;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                retired = eastl::move(mFreePages);
                mFreePages.clear();
                mFreeBytes = 0;
            }
            for (TPage &page : retired)
            {
                destroyPage(page);
            }
        }

        /** Changes the page-byte limit; nonzero; applies to later page allocations and may change at any time. */
        void setLimit(uint64_t bytes)
        {
            if (bytes == 0)
            {
                throw std::invalid_argument("Argument memory budgets must be nonzero.");
            }
            mLimit.store(bytes, eastl::memory_order_relaxed);
        }

        /** Returns a point-in-time copy of all Args memory counters for diagnostics. */
        ArgumentMemoryStatistics statistics() const
        {
            ArgumentMemoryStatistics result;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                result.pageBytesRecording = mRecordingBytes;
                result.pageBytesInFlight = mInFlightBytes;
                result.pageBytesFree = mFreeBytes;
            }
            result.snapshotBytes = mSnapshotBytes.load(eastl::memory_order_relaxed);
            result.pageByteLimit = mLimit.load(eastl::memory_order_relaxed);
            result.pageAllocationCount = mAllocationCount.load(eastl::memory_order_relaxed);
            return result;
        }

    protected:
        /** Creates one native parameter page of the given size; called without the pool mutex held. */
        virtual TPage createPage(uint64_t size) = 0;
        /** Destroys one native parameter page; called without the pool mutex held. */
        virtual void destroyPage(TPage &page) = 0;
        /**
         * Waits until submitted work can release pages and returns whether any submitted work was
         * pending to wait for; called without the pool mutex held.
         */
        virtual bool waitForSubmittedWork() = 0;

        /** Blocks until in-flight page bytes decrease; used by backends whose submissions complete asynchronously. */
        void waitForInFlightDecrease()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            const uint64_t observed = mInFlightBytes;
            mCondition.wait(lock, [this, observed]
            {
                return mInFlightBytes < observed || mInFlightBytes == 0;
            });
        }

    private:
        /** Selects the next acquire action after the locked state inspection. */
        enum class Step
        {
            Create,
            Recycle,
            Wait,
            Throw
        };

        eastl::vector<TPage> mFreePages;
        mutable std::mutex mMutex;
        std::condition_variable mCondition;
        eastl::atomic<uint64_t> mSnapshotBytes{0};
        uint64_t mRecordingBytes = 0;
        uint64_t mInFlightBytes = 0;
        uint64_t mFreeBytes = 0;
        eastl::atomic<uint64_t> mLimit{32u * 1024u * 1024u};
        eastl::atomic<uint64_t> mAllocationCount{0};
    };
}
