#pragma once
#include "GraphPass.hpp"
#include "GraphDevice.hpp"
#include "PassSchedule.hpp"
#include "PassWeightProfiler.hpp"
#include <EASTL/atomic.h>
#include <EASTL/optional.h>
#include <array>
#include <exception>
#include <mutex>

namespace GVM::Core::Private
{
    class TaskRuntimeState;
    /// Records one expanded GPU launch group on the injected executor and submits bounded ordered batches.
    class GpuGraphExecution final : public eastl::enable_shared_from_this<GpuGraphExecution>
    {
    public:
        /// Takes ownership of immutable pass captures before any recording callback is scheduled.
        GpuGraphExecution(TaskRuntimeState &runtime, eastl::vector<RenderNodeFragment> fragments,
            eastl::function<void(uint32_t)> progress,
            eastl::function<void(std::exception_ptr)> completion);
        /// Releases any retained per-queue load counters on destruction.
        ~GpuGraphExecution();
        /// Starts a bounded number of recording lanes on the runtime's existing executor.
        void start() noexcept;
        /// Stops unstarted recordings and submissions while retaining already accepted GPU work.
        void cancel() noexcept;
    private:
        class BatchCompletionCallback;
        class QueueBudgetCallback;

        /// Describes one deferred shared-queue encoder budget wait to register outside mMutex.
        struct QueueWaitRequest
        {
            uint32_t queue = 0u;
            uint32_t needed = 0u;
            bool speculative = false;
        };

        /// Computes the shared-queue encoder reservation required by one batch including submission overhead.
        uint32_t batchEncoderReservation(uint32_t batchIndex) const noexcept;
        /// Claims the next pass from any queue whose per-queue lookahead window and shared encoder budget are open under mMutex.
        bool tryClaimPassLocked(uint32_t &outPassIndex) noexcept;
        /// Checks whether at least one unclaimed pass has an open per-queue lookahead window and shared encoder budget under mMutex.
        bool hasOpenWindowPassLocked() const noexcept;
        /// Wakes recording lanes when passes are claimable or registers shared-queue budget waiters when blocked across executions.
        void ensureRecordingProgress() noexcept;
        /// Resumes recording admission after a shared physical queue releases encoder budget.
        void onQueueBudgetAvailable(uint32_t queueIndex) noexcept;
        /// Releases unsubmitted encoders, budget reservations, and waiters once recording and submission activity have stopped.
        void releaseAbandonedReservations() noexcept;
        /// Schedules additional recording lanes on the runtime executor when a queue window opens.
        void scheduleRecordingLanes(uint32_t count) noexcept;
        /// Records independent closed command encoders without waiting on GPU dependencies.
        void record() noexcept;
        /// Submits batches whose passes and predecessors have finished outside mMutex.
        void submitReadyBatches() noexcept;
        /// Handles completion of one tracked batch from GraphDevice::awaitSubmission.
        void onBatchCompleted(uint32_t batch) noexcept;
        /// Fires the launch group completion callback once all recordings, submits, and GPU completions drain.
        void tryFinish() noexcept;
        /// Stores the first failure and prevents additional command recording or submission.
        void fail(std::exception_ptr failure) noexcept;
        TaskRuntimeState &mRuntime;
        eastl::shared_ptr<GraphDevice> mDevice;
        PassProfileSession mProfileSession;
        eastl::vector<GraphPass> mPasses;
        eastl::vector<RHI::CommandEncoder> mEncoders;
        eastl::vector<PassBatch> mBatches;
        eastl::vector<uint32_t> mPassQueues;
        eastl::vector<uint32_t> mPassToBatch;
        eastl::vector<uint32_t> mBatchNextPass;
        eastl::vector<uint32_t> mBatchQueueOrdinal;
        std::array<eastl::vector<uint32_t>, 3> mQueueBatches;
        std::array<uint32_t, 3> mQueueSubmittedCursor{};
        std::array<uint32_t, 3> mQueueUnsubmittedPasses{};
        std::array<uint32_t, 3> mQueueMaxUnsubmittedPasses{192u, 192u, 192u};
        eastl::vector<uint32_t> mBatchRemainingPasses;
        eastl::vector<uint32_t> mBatchPendingDeps;
        eastl::vector<eastl::vector<uint32_t>> mBatchDependents;
        eastl::vector<bool> mBatchHasCompletionNode;
        eastl::vector<bool> mBatchCompletionTracked;
        eastl::vector<bool> mBatchAwaitAttempted;
        eastl::vector<bool> mBatchQueued;
        eastl::vector<bool> mBatchSubmitted;
        eastl::vector<bool> mBatchLoadRetained;
        eastl::vector<uint32_t> mBatchReservedEncoders;
        std::array<bool, 3> mWaitingOnQueueBudget{false, false, false};
        std::array<bool, 3> mWaitingOnQueueSpeculative{false, false, false};
        eastl::vector<uint32_t> mReadyBatches;
        eastl::vector<eastl::optional<RHI::Submission>> mSubmissions;
        eastl::function<void(uint32_t)> mProgress;
        eastl::function<void(std::exception_ptr)> mCompletion;
        std::mutex mMutex;
        std::exception_ptr mFailure;
        eastl::atomic<bool> mStopping{false};
        uint32_t mEmptyQueueMask = 0;
        uint32_t mLanes = 1;
        uint32_t mRecording = 1;
        uint32_t mInFlightSubmits = 0;
        uint32_t mPendingCompletions = 0;
        uint32_t mSubmittedBatches = 0;
        uint32_t mFirstUnclaimedBatch = 0;
        uint32_t mClaimedPasses = 0;
        bool mCompletionCalled = false;
    };
}
