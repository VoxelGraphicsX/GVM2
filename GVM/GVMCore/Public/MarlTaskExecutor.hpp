#pragma once

#include "ITaskExecutor.hpp"
#include <marl/event.h>
#include <marl/scheduler.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>

namespace GVM::Core
{
    namespace Detail
    {
        template <typename SchedulerT>
        concept HasMarlCurrentScheduler = requires {
            { SchedulerT::get() } -> std::convertible_to<SchedulerT *>;
        };

        template <typename SchedulerT>
        concept HasMarlCurrentFiber = requires {
            SchedulerT::Fiber::current();
        };

        /// Returns the marl::Scheduler currently bound to the calling OS thread, or nullptr if unbound.
        template <typename SchedulerT>
        SchedulerT *boundScheduler() noexcept
        {
            if constexpr (HasMarlCurrentScheduler<SchedulerT>)
            {
                return SchedulerT::get();
            }
            else
            {
                return nullptr;
            }
        }

        /// Returns the opaque active marl fiber pointer when running on a marl fiber, or nullptr otherwise.
        template <typename SchedulerT>
        const void *currentExecutionFiber() noexcept
        {
            if constexpr (HasMarlCurrentFiber<SchedulerT>)
            {
                return static_cast<const void *>(SchedulerT::Fiber::current());
            }
            else
            {
                return nullptr;
            }
        }

        /// Returns true when ANY marl::Scheduler is bound to the calling OS thread, so blocking must yield the active fiber.
        template <typename SchedulerT>
        bool hasBoundScheduler() noexcept
        {
            return boundScheduler<SchedulerT>() != nullptr;
        }
    } // namespace Detail

    /// Optional header-only adapter allowing callers who provide marl to run GVM TaskRuntime on a marl::Scheduler.
    /// GVMCore itself does not depend on marl; include this header only in targets that link marl (or link GVM::MarlExecutor).
    class MarlTaskExecutor final : public ITaskExecutor
    {
    public:
        /// Binds to an existing marl::Scheduler whose lifetime must outlive this executor (and all TaskRuntime clients).
        explicit MarlTaskExecutor(
            marl::Scheduler &scheduler,
            uint32_t reportedConcurrency = 0,
            marl::Task::Flags taskFlags = marl::Task::Flags::None) noexcept
            : mScheduler(scheduler),
              mConcurrency(reportedConcurrency != 0 ? reportedConcurrency : defaultConcurrency(scheduler)),
              mTaskFlags(taskFlags)
        {
        }

        /// Stops accepting new tasks, drains accepted tasks, and releases pooled fiber waiters.
        ~MarlTaskExecutor() override
        {
            shutdown();
            std::lock_guard waiterLock(mWaitersMutex);
            while (mFreeWaiters != nullptr)
            {
                FiberWaiter *next = mFreeWaiters->next;
                delete mFreeWaiters;
                mFreeWaiters = next;
            }
        }

        MarlTaskExecutor(const MarlTaskExecutor &) = delete;
        MarlTaskExecutor &operator=(const MarlTaskExecutor &) = delete;

        /// Schedules one ready ExecutorTask onto the underlying marl::Scheduler from any thread.
        ScheduleResult trySchedule(ExecutorTask task) noexcept override
        {
            if (!task)
            {
                return ScheduleResult::Unavailable;
            }
            if (mStopping.load(std::memory_order_acquire))
            {
                return ScheduleResult::Stopping;
            }
            mInFlight.fetch_add(1, std::memory_order_acq_rel);
            if (mStopping.load(std::memory_order_acquire))
            {
                releaseInFlight();
                return ScheduleResult::Stopping;
            }
            BatchBlock *block = nullptr;
            try
            {
                block = BatchBlock::allocate(this, 1);
                BatchBlock::Slot &slot = block->slots()[0];
                slot.task = std::move(task);
                slot.inFlightOwned.store(true, std::memory_order_relaxed);
                block->retain();
                SlotHandle handle(&slot);
                mScheduler.enqueue(marl::Task(std::move(handle), effectiveTaskFlags()));
                block->release();
                return ScheduleResult::Accepted;
            }
            catch (...)
            {
                if (block != nullptr)
                {
                    BatchBlock::Slot &slot = block->slots()[0];
                    if (slot.task)
                    {
                        task = std::move(slot.task);
                    }
                    block->release();
                }
                else
                {
                    releaseInFlight();
                }
                return ScheduleResult::Unavailable;
            }
        }

        /// Enqueues a contiguous batch of ready tasks sharing one allocation and per-slot in-flight accounting.
        size_t scheduleBatch(ExecutorTask *tasks, size_t count) noexcept override
        {
            if (tasks == nullptr || count == 0)
            {
                return 0;
            }
            size_t validCount = 0;
            while (validCount < count && static_cast<bool>(tasks[validCount]))
            {
                ++validCount;
            }
            if (validCount == 0)
            {
                return 0;
            }
            if (validCount == 1)
            {
                return trySchedule(std::move(tasks[0])) == ScheduleResult::Accepted ? 1u : 0u;
            }
            if (mStopping.load(std::memory_order_acquire))
            {
                return 0;
            }
            mInFlight.fetch_add(validCount, std::memory_order_acq_rel);
            if (mStopping.load(std::memory_order_acquire))
            {
                for (size_t i = 0; i < validCount; ++i)
                {
                    releaseInFlight();
                }
                return 0;
            }
            BatchBlock *block = nullptr;
            try
            {
                block = BatchBlock::allocate(this, validCount);
            }
            catch (...)
            {
                for (size_t i = 0; i < validCount; ++i)
                {
                    releaseInFlight();
                }
                return 0;
            }
            const marl::Task::Flags flags = effectiveTaskFlags();
            size_t accepted = 0;
            for (; accepted < validCount; ++accepted)
            {
                BatchBlock::Slot &slot = block->slots()[accepted];
                slot.task = std::move(tasks[accepted]);
                slot.inFlightOwned.store(true, std::memory_order_relaxed);
                block->retain();
                SlotHandle handle(&slot);
                try
                {
                    mScheduler.enqueue(marl::Task(std::move(handle), flags));
                }
                catch (...)
                {
                    if (slot.task)
                    {
                        tasks[accepted] = std::move(slot.task);
                    }
                    slot.releaseSlotInFlight();
                    for (size_t unscheduled = accepted + 1u; unscheduled < validCount; ++unscheduled)
                    {
                        releaseInFlight();
                    }
                    break;
                }
            }
            block->release();
            return accepted;
        }

        /// Reports the effective concurrency for TaskRuntime launch group and lane sizing.
        uint32_t concurrency() const noexcept override
        {
            return mConcurrency;
        }

        /// Cooperatively suspends the active marl fiber on a pooled marl::Event without polling when bound to a marl::Scheduler.
        bool waitCondition(
            std::unique_lock<std::mutex> &lock,
            std::condition_variable &condition,
            uint64_t timeoutNanoseconds,
            WaitPredicate predicate,
            const void *context) const noexcept override
        {
            if (predicate(context))
            {
                return true;
            }
            if (timeoutNanoseconds == 0)
            {
                return false;
            }
            if (!Detail::hasBoundScheduler<marl::Scheduler>())
            {
                return ITaskExecutor::waitCondition(lock, condition, timeoutNanoseconds, predicate, context);
            }

            FiberWaiter *waiter = acquireWaiter(condition);
            if (waiter == nullptr)
            {
                return ITaskExecutor::waitCondition(lock, condition, timeoutNanoseconds, predicate, context);
            }
            WaiterReleaseGuard releaseGuard{this, waiter};

            const auto started = std::chrono::steady_clock::now();
            for (;;)
            {
                if (predicate(context))
                {
                    return true;
                }
                if (timeoutNanoseconds == std::numeric_limits<uint64_t>::max())
                {
                    lock.unlock();
                    waiter->event.wait();
                    lock.lock();
                }
                else
                {
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started).count());
                    if (elapsed >= timeoutNanoseconds)
                    {
                        return false;
                    }
                    constexpr uint64_t kMaxWaitSliceNs = 3'600'000'000'000ull; // 1 hour per slice prevents system_clock overflow.
                    const auto remaining = std::min(timeoutNanoseconds - elapsed, kMaxWaitSliceNs);
                    lock.unlock();
                    (void)waiter->event.wait_for(std::chrono::nanoseconds(remaining));
                    lock.lock();
                    if (predicate(context))
                    {
                        return true;
                    }
                    const auto afterElapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started).count());
                    if (afterElapsed >= timeoutNanoseconds)
                    {
                        return false;
                    }
                }
            }
        }

        /// Wakes both OS thread condition waiters and suspended marl fiber waiters immediately without polling.
        void notifyCondition(std::condition_variable &condition) const noexcept override
        {
            condition.notify_all();
            std::lock_guard waiterLock(mWaitersMutex);
            for (FiberWaiter *w = mWaitersHead; w != nullptr; w = w->next)
            {
                if (w->condition == &condition)
                {
                    w->event.signal();
                }
            }
        }

        /// Marks the executor stopping and drains accepted marl tasks without deadlocking bound fibers or self-callers.
        void shutdown() noexcept
        {
            {
                std::lock_guard lock(mDrainMutex);
                mStopping.store(true, std::memory_order_seq_cst);
            }
            const void *curFiber = Detail::currentExecutionFiber<marl::Scheduler>();
            const std::thread::id curThread = curFiber == nullptr ? std::this_thread::get_id() : std::thread::id{};
            size_t releasedSelfSlots = 0;
            {
                ActiveSlotBucket &bucket = activeSlotBucket(activeSlotBucketIndex(curFiber, curThread));
                std::lock_guard slotLock(bucket.mutex);
                for (ActiveSlotFrame *frame = bucket.head; frame != nullptr; frame = frame->next)
                {
                    const bool sameOwner = curFiber != nullptr
                        ? (frame->fiber == curFiber)
                        : (frame->fiber == nullptr && frame->threadId == curThread);
                    if (sameOwner &&
                        frame->executor == this &&
                        frame->slot != nullptr &&
                        frame->slot->inFlightOwned.exchange(false, std::memory_order_acq_rel))
                    {
                        ++releasedSelfSlots;
                    }
                }
            }
            for (size_t index = 0; index < releasedSelfSlots; ++index)
            {
                releaseInFlight();
            }
            std::unique_lock lock(mDrainMutex);
            const auto drained = [](const void *ctx) noexcept -> bool
            {
                return static_cast<const MarlTaskExecutor *>(ctx)->mInFlight.load(std::memory_order_seq_cst) == 0;
            };
            (void)waitCondition(lock, mDrained, std::numeric_limits<uint64_t>::max(), drained, this);
        }

    private:
        struct FiberWaiter
        {
            const std::condition_variable *condition = nullptr;
            marl::Event event{marl::Event::Mode::Auto};
            FiberWaiter *prev = nullptr;
            FiberWaiter *next = nullptr;
        };

        /// Returns a pooled FiberWaiter to the free list when waitCondition exits.
        struct WaiterReleaseGuard
        {
            const MarlTaskExecutor *self = nullptr;
            FiberWaiter *node = nullptr;
            ~WaiterReleaseGuard()
            {
                self->releaseWaiter(node);
            }
        };

        FiberWaiter *acquireWaiter(const std::condition_variable &condition) const noexcept
        {
            std::lock_guard waiterLock(mWaitersMutex);
            FiberWaiter *node = mFreeWaiters;
            if (node != nullptr)
            {
                mFreeWaiters = node->next;
                node->event.clear();
            }
            else
            {
                node = new (std::nothrow) FiberWaiter{};
                if (node == nullptr)
                {
                    return nullptr;
                }
            }
            node->condition = &condition;
            node->prev = nullptr;
            node->next = mWaitersHead;
            if (mWaitersHead != nullptr)
            {
                mWaitersHead->prev = node;
            }
            mWaitersHead = node;
            return node;
        }

        void releaseWaiter(FiberWaiter *node) const noexcept
        {
            std::lock_guard waiterLock(mWaitersMutex);
            if (node->prev != nullptr)
            {
                node->prev->next = node->next;
            }
            else
            {
                mWaitersHead = node->next;
            }
            if (node->next != nullptr)
            {
                node->next->prev = node->prev;
            }
            node->condition = nullptr;
            node->prev = nullptr;
            node->next = mFreeWaiters;
            mFreeWaiters = node;
        }

        struct alignas(std::max_align_t) BatchBlock
        {
            struct Slot
            {
                BatchBlock *block = nullptr;
                ExecutorTask task{};
                std::atomic<bool> inFlightOwned{false};

                /// Releases this slot's single in-flight token at most once.
                void releaseSlotInFlight() noexcept
                {
                    if (inFlightOwned.exchange(false, std::memory_order_acq_rel) &&
                        block != nullptr &&
                        block->executor != nullptr)
                    {
                        block->executor->releaseInFlight();
                    }
                }
            };

            MarlTaskExecutor *executor = nullptr;
            size_t slotCount = 0;
            std::atomic<size_t> refs{1};

            Slot *slots() noexcept
            {
                return reinterpret_cast<Slot *>(this + 1);
            }

            static BatchBlock *allocate(MarlTaskExecutor *exec, size_t count)
            {
                static_assert(alignof(BatchBlock) >= alignof(Slot));
                static_assert(sizeof(BatchBlock) % alignof(Slot) == 0);
                if (count > (std::numeric_limits<size_t>::max() - sizeof(BatchBlock)) / sizeof(Slot))
                {
                    throw std::bad_alloc();
                }
                const size_t totalBytes = sizeof(BatchBlock) + count * sizeof(Slot);
                void *raw = ::operator new(totalBytes, std::align_val_t{alignof(BatchBlock)});
                auto *block = ::new (raw) BatchBlock{exec, count, 1};
                Slot *items = block->slots();
                for (size_t i = 0; i < count; ++i)
                {
                    ::new (static_cast<void *>(&items[i])) Slot{block, ExecutorTask{}, false};
                }
                return block;
            }

            void retain() noexcept
            {
                refs.fetch_add(1, std::memory_order_relaxed);
            }

            void release() noexcept
            {
                if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    Slot *items = slots();
                    for (size_t i = 0; i < slotCount; ++i)
                    {
                        items[i].releaseSlotInFlight();
                        items[i].~Slot();
                    }
                    this->~BatchBlock();
                    ::operator delete(static_cast<void *>(this), std::align_val_t{alignof(BatchBlock)});
                }
            }
        };

        static constexpr size_t kActiveSlotBucketCount = 16u;

        /// Records one live executing Slot keyed by marl fiber identity (or OS thread identity when unbound).
        struct ActiveSlotFrame
        {
            BatchBlock::Slot *slot = nullptr;
            const MarlTaskExecutor *executor = nullptr;
            const void *fiber = nullptr;
            std::thread::id threadId{};
            size_t bucketIndex = 0;
            ActiveSlotFrame *prev = nullptr;
            ActiveSlotFrame *next = nullptr;
        };

        /// Guards one shard of the process-wide active-slot doubly-linked list independent of executor object lifetime.
        struct ActiveSlotBucket
        {
            std::mutex mutex;
            ActiveSlotFrame *head = nullptr;
        };

        /// Computes the shard index for a marl fiber pointer or fallback OS thread identifier.
        static size_t activeSlotBucketIndex(const void *fiber, std::thread::id threadId) noexcept
        {
            const size_t rawHash = fiber != nullptr
                ? static_cast<size_t>(reinterpret_cast<uintptr_t>(fiber) >> 4u)
                : std::hash<std::thread::id>{}(threadId);
            return rawHash & (kActiveSlotBucketCount - 1u);
        }

        /// Returns one static active-slot shard that outlives individual MarlTaskExecutor instances.
        static ActiveSlotBucket &activeSlotBucket(size_t index) noexcept
        {
            static ActiveSlotBucket sBuckets[kActiveSlotBucketCount];
            return sBuckets[index];
        }

        /// Registers an executing Slot for its marl fiber and unlinks the exact frame on scope exit regardless of suspension or worker migration.
        class ActiveSlotScope final
        {
        public:
            /// Links the executing slot frame into its fiber's static bucket before invoking task.execute().
            explicit ActiveSlotScope(BatchBlock::Slot *slot) noexcept
            {
                const void *fiber = Detail::currentExecutionFiber<marl::Scheduler>();
                const std::thread::id threadId = fiber == nullptr ? std::this_thread::get_id() : std::thread::id{};
                const size_t bucketIndex = activeSlotBucketIndex(fiber, threadId);
                mFrame.slot = slot;
                mFrame.executor = (slot != nullptr && slot->block != nullptr) ? slot->block->executor : nullptr;
                mFrame.fiber = fiber;
                mFrame.threadId = threadId;
                mFrame.bucketIndex = bucketIndex;

                ActiveSlotBucket &bucket = activeSlotBucket(bucketIndex);
                std::lock_guard lock(bucket.mutex);
                mFrame.prev = nullptr;
                mFrame.next = bucket.head;
                if (bucket.head != nullptr)
                {
                    bucket.head->prev = &mFrame;
                }
                bucket.head = &mFrame;
            }

            /// Unlinks this exact frame from its static bucket before the fiber stack frame unwinds.
            ~ActiveSlotScope()
            {
                ActiveSlotBucket &bucket = activeSlotBucket(mFrame.bucketIndex);
                std::lock_guard lock(bucket.mutex);
                if (mFrame.prev != nullptr)
                {
                    mFrame.prev->next = mFrame.next;
                }
                else
                {
                    bucket.head = mFrame.next;
                }
                if (mFrame.next != nullptr)
                {
                    mFrame.next->prev = mFrame.prev;
                }
                mFrame.prev = nullptr;
                mFrame.next = nullptr;
            }

            ActiveSlotScope(const ActiveSlotScope &) = delete;
            ActiveSlotScope &operator=(const ActiveSlotScope &) = delete;

        private:
            ActiveSlotFrame mFrame;
        };

        class SlotHandle final
        {
        public:
            SlotHandle() noexcept = default;
            explicit SlotHandle(BatchBlock::Slot *slot) noexcept : mSlot(slot) {}
            SlotHandle(const SlotHandle &other) noexcept : mSlot(other.mSlot)
            {
                if (mSlot != nullptr)
                {
                    mSlot->block->retain();
                }
            }
            SlotHandle(SlotHandle &&other) noexcept : mSlot(other.mSlot)
            {
                other.mSlot = nullptr;
            }
            SlotHandle &operator=(const SlotHandle &other) noexcept
            {
                if (this != &other)
                {
                    if (other.mSlot != nullptr)
                    {
                        other.mSlot->block->retain();
                    }
                    if (mSlot != nullptr)
                    {
                        mSlot->block->release();
                    }
                    mSlot = other.mSlot;
                }
                return *this;
            }
            SlotHandle &operator=(SlotHandle &&other) noexcept
            {
                if (this != &other)
                {
                    if (mSlot != nullptr)
                    {
                        mSlot->block->release();
                    }
                    mSlot = other.mSlot;
                    other.mSlot = nullptr;
                }
                return *this;
            }
            ~SlotHandle()
            {
                if (mSlot != nullptr)
                {
                    mSlot->block->release();
                }
            }
            void operator()() const noexcept
            {
                if (mSlot != nullptr && mSlot->task)
                {
                    {
                        ActiveSlotScope activeScope(mSlot);
                        mSlot->task.execute();
                    }
                    mSlot->releaseSlotInFlight();
                }
            }

        private:
            BatchBlock::Slot *mSlot = nullptr;
        };

        static uint32_t defaultConcurrency(const marl::Scheduler &scheduler) noexcept
        {
            const int count = scheduler.config().workerThread.count;
            return count > 0 ? static_cast<uint32_t>(count) : 1u;
        }

        marl::Task::Flags effectiveTaskFlags() const noexcept
        {
            if ((static_cast<uint32_t>(mTaskFlags) & static_cast<uint32_t>(marl::Task::Flags::SameThread)) != 0u &&
                Detail::boundScheduler<marl::Scheduler>() != &mScheduler)
            {
                return marl::Task::Flags::None;
            }
            return mTaskFlags;
        }

        void releaseInFlight() noexcept
        {
            uint64_t current = mInFlight.load(std::memory_order_acquire);
            while (current > 1u)
            {
                if (mInFlight.compare_exchange_weak(
                        current, current - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    return;
                }
            }
            std::lock_guard lock(mDrainMutex);
            if (mInFlight.fetch_sub(1u, std::memory_order_seq_cst) == 1u &&
                mStopping.load(std::memory_order_relaxed))
            {
                notifyCondition(mDrained);
            }
        }

        marl::Scheduler &mScheduler;
        uint32_t mConcurrency;
        marl::Task::Flags mTaskFlags;
        std::atomic<bool> mStopping{false};
        std::atomic<uint64_t> mInFlight{0};
        mutable std::mutex mWaitersMutex;
        mutable FiberWaiter *mWaitersHead = nullptr;
        mutable FiberWaiter *mFreeWaiters = nullptr;
        std::mutex mDrainMutex;
        std::condition_variable mDrained;
    };
} // namespace GVM::Core
