#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/deque.h>
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include "PassSchedule.hpp"

namespace GVM::Core::Private
{
    /// Serializes native object construction and per-queue submission for graphs sharing one borrowed device.
    class GraphDevice final
    {
    public:
        /// Reserves extra native command-buffer slots per submitted batch for query resolve, dependency waits, and completion signaling.
        static constexpr uint32_t kBatchSubmitEncoderOverhead = 4u;

        /// Stops any active completion bridge before releasing borrowed device state.
        ~GraphDevice();
        /// Acquires the shared graph-side synchronization domain; the RHI device must outlive all graph resources.
        static eastl::shared_ptr<GraphDevice> acquire(RHI::DeviceImpl &device);
        /// Borrows the native device while its graph resource lock is held.
        RHI::DeviceImpl &device() const;
        /// Returns a deduplicated physical candidate; index zero denotes the canonical graphics queue.
        RHI::Queue queue(uint32_t index = 0) const;
        /// Returns deduplicated candidates used only inside the physical pass planner.
        PassQueues candidates() const;
        /// Returns the maximum pass count a single batch may place on one deduplicated queue within its encoder budget.
        uint32_t maxBatchPassCount(uint32_t queueIndex) const noexcept;
        /// Checks whether one deduplicated queue can currently reserve count unsubmitted encoders under root or speculative admission.
        bool canReserveQueueEncoders(uint32_t queueIndex, uint32_t count, bool speculative) const noexcept;
        /// Returns whether count unsubmitted encoders can ever fit within the queue's speculative lookahead budget.
        bool canEverReserveSpeculatively(uint32_t queueIndex, uint32_t count) const noexcept;
        /// Atomically reserves count unsubmitted encoders on one deduplicated queue when within its root or speculative budget.
        bool tryReserveQueueEncoders(uint32_t queueIndex, uint32_t count, bool speculative) noexcept;
        /// Releases count previously reserved encoders on one deduplicated queue and wakes any waiting executions.
        void releaseQueueEncoders(uint32_t queueIndex, uint32_t count) noexcept;
        /// Enqueues callback if count encoders cannot currently be reserved; returns false without queuing when capacity is already open.
        bool waitForQueueEncoders(
            uint32_t queueIndex,
            uint32_t count,
            bool speculative,
            const void *owner,
            eastl::function<void()> callback);
        /// Cancels any queued encoder-budget callbacks registered under owner before execution teardown completes.
        void cancelQueueEncoderWaiters(const void *owner) noexcept;
        /// Returns active planned batch load for one deduplicated queue index.
        uint32_t queueLoad(uint32_t index) const;
        /// Increments active planned batch load for one deduplicated queue index.
        void retainQueueLoad(uint32_t index);
        /// Decrements active planned batch load for one deduplicated queue index.
        void releaseQueueLoad(uint32_t index);
        /// Locks native object creation, mapping and destruction without blocking queue submission.
        std::unique_lock<std::mutex> lockResources();
        /// Locks submission on one deduplicated physical queue lane independently of other queues and resource creation.
        std::unique_lock<std::mutex> lockQueue(uint32_t index);
        /// Routes completion via native RHI push callbacks when supported or a lazy deduplicated RHI::waitAny bridge.
        void awaitSubmission(uint32_t queueIndex, RHI::Submission submission, eastl::function<void()> callback);

    private:
        struct PendingCompletion
        {
            uint64_t serial = 0;
            RHI::Submission submission;
            eastl::function<void()> callback;
        };

        /// Represents one execution callback awaiting shared unsubmitted command-encoder capacity on a physical queue.
        struct QueueEncoderWaiter
        {
            const void *owner = nullptr;
            bool speculative = false;
            eastl::function<void()> callback;
        };

        /// Tracks shared unsubmitted command-encoder reservations and pending waiters for one deduplicated physical queue.
        struct QueueEncoderBudget
        {
            mutable std::mutex mutex;
            uint32_t capacity = 256u;
            uint32_t maxBatchPasses = 192u;
            uint32_t maxRootReservation = 196u;
            uint32_t reserved = 0u;
            eastl::deque<QueueEncoderWaiter> waiters;
        };

        /// Evaluates whether count unsubmitted encoders fit within budget under root or speculative admission while budget.mutex is held.
        static bool canReserveEncoderBudgetLocked(const QueueEncoderBudget &budget, uint32_t count, bool speculative) noexcept;

        /// Resolves device queue preferences once before worker callbacks are published.
        explicit GraphDevice(RHI::DeviceImpl &device);
        bool hasPendingCompletionsLocked() const noexcept;
        void runCompletionBridge() noexcept;

        RHI::DeviceImpl &mDevice;
        eastl::vector<RHI::Queue> mQueues;
        PassQueues mCandidates;
        std::mutex mResourceMutex;
        std::array<std::mutex, 3> mQueueMutexes;
        std::array<std::atomic<uint32_t>, 3> mQueueLoads{};
        std::array<QueueEncoderBudget, 3> mEncoderBudgets{};

        std::mutex mBridgeMutex;
        std::condition_variable mBridgeCv;
        RHI::HostTimeline mHostTimeline;
        std::array<eastl::deque<PendingCompletion>, 3> mPendingCompletions;
        std::array<uint64_t, 3> mWaitedMinSerial{
            std::numeric_limits<uint64_t>::max(),
            std::numeric_limits<uint64_t>::max(),
            std::numeric_limits<uint64_t>::max()};
        std::thread mBridgeThread;
        bool mBridgeStarted = false;
        bool mBridgeInKernelWait = false;
        bool mBridgeStopping = false;
    };
}
