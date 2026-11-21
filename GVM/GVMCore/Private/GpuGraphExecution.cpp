#include "GpuGraphExecution.hpp"
#include "TaskRuntimeState.hpp"
#include "PassSchedule.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/utility.h>
#include <atomic>
#include <stdexcept>

namespace GVM::Core::Private
{
    /// Retains TaskRuntimeState callback-drain lifetime until a shared-queue encoder budget waiter closure is destroyed.
    class GpuGraphExecution::QueueBudgetCallback final
    {
    public:
        /// Acquires one runtime callback-drain reference before binding the target execution and physical queue index.
        QueueBudgetCallback(
            TaskRuntimeState &runtime,
            eastl::shared_ptr<GpuGraphExecution> execution,
            uint32_t queueIndex) noexcept
            : mRuntime(&runtime)
            , mExecution(eastl::move(execution))
            , mQueueIndex(queueIndex)
        {
            mRuntime->beginCallbackDrain();
        }

        /// Transfers ownership of the active runtime callback-drain reference without incrementing the count.
        QueueBudgetCallback(QueueBudgetCallback &&other) noexcept
            : mRuntime(other.mRuntime)
            , mExecution(eastl::move(other.mExecution))
            , mQueueIndex(other.mQueueIndex)
        {
            other.mRuntime = nullptr;
        }

        /// Duplicates the callback closure while acquiring an independent runtime callback-drain reference.
        QueueBudgetCallback(const QueueBudgetCallback &other) noexcept
            : mRuntime(other.mRuntime)
            , mExecution(other.mExecution)
            , mQueueIndex(other.mQueueIndex)
        {
            if (mRuntime != nullptr)
            {
                mRuntime->beginCallbackDrain();
            }
        }

        QueueBudgetCallback &operator=(const QueueBudgetCallback &) = delete;
        QueueBudgetCallback &operator=(QueueBudgetCallback &&) = delete;

        /// Drops the GpuGraphExecution reference strictly before releasing the runtime callback-drain guard.
        ~QueueBudgetCallback()
        {
            mExecution.reset();
            if (mRuntime != nullptr)
            {
                mRuntime->endCallbackDrain();
            }
        }

        /// Wakes recording admission on the retained GPU launch group after shared queue budget is released.
        void operator()() const noexcept
        {
            if (mExecution)
            {
                mExecution->onQueueBudgetAvailable(mQueueIndex);
            }
        }

    private:
        TaskRuntimeState *mRuntime = nullptr;
        eastl::shared_ptr<GpuGraphExecution> mExecution;
        uint32_t mQueueIndex = 0u;
    };
    /// Retains TaskRuntimeState callback-drain lifetime until an asynchronous GPU batch completion closure is destroyed.
    /// Ensures the completion bridge or backend handler drops its GpuGraphExecution reference before host TaskRuntime teardown completes.
    class GpuGraphExecution::BatchCompletionCallback final
    {
    public:
        /// Acquires one runtime callback-drain reference before binding the target execution and batch index.
        BatchCompletionCallback(
            TaskRuntimeState &runtime,
            eastl::shared_ptr<GpuGraphExecution> execution,
            uint32_t batchIndex) noexcept
            : mRuntime(&runtime)
            , mExecution(eastl::move(execution))
            , mBatchIndex(batchIndex)
        {
            mRuntime->beginCallbackDrain();
        }

        /// Transfers ownership of the active runtime callback-drain reference without incrementing the count.
        BatchCompletionCallback(BatchCompletionCallback &&other) noexcept
            : mRuntime(other.mRuntime)
            , mExecution(eastl::move(other.mExecution))
            , mBatchIndex(other.mBatchIndex)
        {
            other.mRuntime = nullptr;
        }

        /// Duplicates the callback closure while acquiring an independent runtime callback-drain reference.
        BatchCompletionCallback(const BatchCompletionCallback &other) noexcept
            : mRuntime(other.mRuntime)
            , mExecution(other.mExecution)
            , mBatchIndex(other.mBatchIndex)
        {
            if (mRuntime != nullptr)
            {
                mRuntime->beginCallbackDrain();
            }
        }

        BatchCompletionCallback &operator=(const BatchCompletionCallback &) = delete;
        BatchCompletionCallback &operator=(BatchCompletionCallback &&) = delete;

        /// Drops the GpuGraphExecution reference strictly before releasing the runtime callback-drain guard.
        ~BatchCompletionCallback()
        {
            mExecution.reset();
            if (mRuntime != nullptr)
            {
                mRuntime->endCallbackDrain();
            }
        }

        /// Delivers batch completion to the retained GPU launch group.
        void operator()() const noexcept
        {
            if (mExecution)
            {
                mExecution->onBatchCompleted(mBatchIndex);
            }
        }

    private:
        TaskRuntimeState *mRuntime = nullptr;
        eastl::shared_ptr<GpuGraphExecution> mExecution;
        uint32_t mBatchIndex = 0;
    };

    GpuGraphExecution::GpuGraphExecution(TaskRuntimeState &runtime, eastl::vector<RenderNodeFragment> fragments,
        eastl::function<void(uint32_t)> progress, eastl::function<void(std::exception_ptr)> completion)
        : mRuntime(runtime), mDevice(runtime.gpuDevice()), mProgress(eastl::move(progress)), mCompletion(eastl::move(completion))
    {
        for (auto &fragment : fragments)
        {
            for (auto &pass : fragment.passes)
            {
                for (const auto &access : pass.resources.accesses)
                {
                    if (access.resource == nullptr)
                    {
                        pass.resources.complete = false;
                        break;
                    }
                }
                mPasses.push_back(eastl::move(pass));
            }
        }
        if (mPasses.size() > runtime.maximumPasses())
        {
            throw std::length_error("GPU graph exceeds the runtime pass budget.");
        }
        if (auto *profiler = mRuntime.passWeightProfiler())
        {
            profiler->applyLearnedWeights(mPasses);
        }
        PassSchedule schedule(mPasses);
        const auto queues = mDevice->candidates();
        uint32_t effectiveBatchLimit = runtime.maximumPassesPerBatch();
        for (uint32_t q = 0; q < 3u; ++q)
        {
            if (q == 0u || q == queues.compute || q == queues.transfer)
            {
                mQueueMaxUnsubmittedPasses[q] = mDevice->maxBatchPassCount(q);
                effectiveBatchLimit = eastl::min(effectiveBatchLimit, mQueueMaxUnsubmittedPasses[q]);
            }
        }
        effectiveBatchLimit = eastl::max(1u, effectiveBatchLimit);
        eastl::vector<uint32_t> initialAvailable(3, 0);
        for (uint32_t q = 0; q < 3; ++q)
        {
            initialAvailable[q] = mDevice->queueLoad(q) * kDefaultPassWeight;
        }
        mBatches = schedule.place(mPasses, effectiveBatchLimit, queues, initialAvailable);
        mBatchLoadRetained.resize(mBatches.size(), false);
        for (const auto &batch : mBatches)
        {
            mPassQueues.resize(batch.end, batch.queue);
        }
        eastl::vector<GraphPass> ordered;
        ordered.reserve(mPasses.size());
        for (uint32_t index : schedule.order())
        {
            ordered.push_back(eastl::move(mPasses[index]));
        }
        mPasses = eastl::move(ordered);
        mEncoders.resize(mPasses.size());
        mSubmissions.resize(mBatches.size());

        const size_t batchCount = mBatches.size();
        mPassToBatch.resize(mPasses.size());
        mBatchNextPass.resize(batchCount, 0);
        mBatchQueueOrdinal.resize(batchCount, 0);
        mBatchRemainingPasses.resize(batchCount, 0);
        mBatchPendingDeps.resize(batchCount, 0);
        mBatchDependents.resize(batchCount);
        mBatchHasCompletionNode.resize(batchCount, false);
        mBatchCompletionTracked.resize(batchCount, false);
        mBatchAwaitAttempted.resize(batchCount, false);
        mBatchQueued.resize(batchCount, false);
        mBatchSubmitted.resize(batchCount, false);
        mBatchReservedEncoders.resize(batchCount, 0u);
        for (uint32_t b = 0; b < batchCount; ++b)
        {
            const uint32_t begin = b == 0 ? 0 : mBatches[b - 1].end;
            const uint32_t q = mBatches[b].queue;
            mBatchNextPass[b] = begin;
            mBatchQueueOrdinal[b] = static_cast<uint32_t>(mQueueBatches[q].size());
            mQueueBatches[q].push_back(b);
            mBatchRemainingPasses[b] = mBatches[b].end - begin;
            for (uint32_t p = begin; p < mBatches[b].end; ++p)
            {
                mPassToBatch[p] = b;
                if (mPasses[p].completionNode != UINT32_MAX)
                {
                    mBatchHasCompletionNode[b] = true;
                }
            }
            auto addDep = [&](uint32_t dep)
            {
                if (dep == UINT32_MAX)
                {
                    return;
                }
                auto &dependents = mBatchDependents[dep];
                if (eastl::find(dependents.begin(), dependents.end(), b) == dependents.end())
                {
                    dependents.push_back(b);
                    ++mBatchPendingDeps[b];
                }
            };
            addDep(mBatches[b].previousOnQueue);
            for (uint32_t waitBatch : mBatches[b].waits)
            {
                addDep(waitBatch);
            }
        }
        for (uint32_t q = 0; q < 3; ++q)
        {
            if (mQueueBatches[q].empty())
            {
                mEmptyQueueMask |= (1u << q);
            }
        }
        try
        {
            if (auto *profiler = mRuntime.passWeightProfiler())
            {
                mProfileSession = profiler->beginSession(mPasses, mPassQueues);
            }
            for (size_t b = 0; b < mBatches.size(); ++b)
            {
                mDevice->retainQueueLoad(mBatches[b].queue);
                mBatchLoadRetained[b] = true;
            }
        }
        catch (...)
        {
            if (mProfileSession.active())
            {
                if (auto *profiler = mRuntime.passWeightProfiler())
                {
                    profiler->cancelSession(eastl::move(mProfileSession));
                }
            }
            for (size_t b = 0; b < mBatches.size(); ++b)
            {
                if (mBatchLoadRetained[b])
                {
                    mBatchLoadRetained[b] = false;
                    mDevice->releaseQueueLoad(mBatches[b].queue);
                }
            }
            throw;
        }
    }

    GpuGraphExecution::~GpuGraphExecution()
    {
        mEncoders.clear();
        mDevice->cancelQueueEncoderWaiters(this);
        for (size_t b = 0; b < mBatchReservedEncoders.size(); ++b)
        {
            const uint32_t reserved = eastl::exchange(mBatchReservedEncoders[b], 0u);
            if (reserved > 0u)
            {
                mDevice->releaseQueueEncoders(mBatches[b].queue, reserved);
            }
        }
        if (mProfileSession.active())
        {
            if (auto *profiler = mRuntime.passWeightProfiler())
            {
                profiler->cancelSession(eastl::move(mProfileSession));
            }
        }
        for (size_t b = 0; b < mBatches.size(); ++b)
        {
            if (b < mBatchLoadRetained.size() && mBatchLoadRetained[b])
            {
                mDevice->releaseQueueLoad(mBatches[b].queue);
            }
        }
    }

    void GpuGraphExecution::start() noexcept
    {
        uint32_t lanes = eastl::min(mRuntime.maximumInFlight(), static_cast<uint32_t>(mPasses.size()));
        for (const auto &pass : mPasses)
        {
            if (!pass.resources.complete)
            {
                lanes = 1;
                break;
            }
        }
        {
            std::lock_guard lock(mMutex);
            mLanes = lanes;
            mRecording = lanes;
        }
        if (lanes == 0)
        {
            tryFinish();
            return;
        }
        scheduleRecordingLanes(lanes);
    }

    void GpuGraphExecution::scheduleRecordingLanes(uint32_t count) noexcept
    {
        if (count == 1)
        {
            try
            {
                ExecutorTask task([self = shared_from_this()] { self->record(); });
                if (mRuntime.schedule(eastl::move(task)) == ScheduleResult::Accepted)
                {
                    return;
                }
                throw std::runtime_error("Task executor rejected GPU pass recording.");
            }
            catch (...)
            {
                fail(std::current_exception());
                {
                    std::lock_guard lock(mMutex);
                    --mRecording;
                }
                tryFinish();
            }
            return;
        }
        eastl::vector<ExecutorTask> tasks;
        tasks.reserve(count);
        try
        {
            auto self = shared_from_this();
            for (uint32_t index = 0; index < count; ++index)
            {
                tasks.emplace_back([self] { self->record(); });
            }
        }
        catch (...)
        {
            const uint32_t unbuilt = count - static_cast<uint32_t>(tasks.size());
            fail(std::current_exception());
            {
                std::lock_guard lock(mMutex);
                mRecording -= unbuilt;
            }
        }
        if (!tasks.empty())
        {
            const size_t accepted = mRuntime.scheduleBatch(tasks.data(), tasks.size());
            if (accepted < tasks.size())
            {
                const uint32_t rejected = static_cast<uint32_t>(tasks.size() - accepted);
                try
                {
                    throw std::runtime_error("Task executor rejected GPU pass recording.");
                }
                catch (...)
                {
                    fail(std::current_exception());
                }
                {
                    std::lock_guard lock(mMutex);
                    mRecording -= rejected;
                }
                tryFinish();
            }
        }
        else
        {
            tryFinish();
        }
    }

    void GpuGraphExecution::cancel() noexcept
    {
        mStopping.store(true, eastl::memory_order_release);
        tryFinish();
    }

    void GpuGraphExecution::fail(std::exception_ptr failure) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            if (!mFailure)
            {
                mFailure = failure;
            }
        }
        mStopping.store(true, eastl::memory_order_release);
    }

    uint32_t GpuGraphExecution::batchEncoderReservation(uint32_t batchIndex) const noexcept
    {
        const uint32_t begin = batchIndex == 0u ? 0u : mBatches[batchIndex - 1u].end;
        const uint32_t passCount = mBatches[batchIndex].end - begin;
        return passCount + GraphDevice::kBatchSubmitEncoderOverhead;
    }

    bool GpuGraphExecution::hasOpenWindowPassLocked() const noexcept
    {
        if (mStopping.load(eastl::memory_order_acquire) || mClaimedPasses >= mPasses.size())
        {
            return false;
        }
        const uint32_t maxLookaheadBatches = eastl::max<uint32_t>(4u, mLanes);
        uint32_t fullQueueMask = mEmptyQueueMask;
        for (uint32_t b = mFirstUnclaimedBatch; b < mBatches.size() && fullQueueMask != 0x7u; ++b)
        {
            if (mBatchNextPass[b] >= mBatches[b].end)
            {
                continue;
            }
            const uint32_t q = mBatches[b].queue;
            const uint32_t qBit = 1u << q;
            if ((fullQueueMask & qBit) != 0)
            {
                continue;
            }
            const uint32_t oldestOrd = mQueueSubmittedCursor[q];
            const uint32_t ord = mBatchQueueOrdinal[b];
            const bool withinLookahead = (ord == oldestOrd) || (ord - oldestOrd < maxLookaheadBatches);
            const bool windowOpen = withinLookahead &&
                (mBatchReservedEncoders[b] > 0u ||
                 mDevice->canReserveQueueEncoders(q, batchEncoderReservation(b), mBatchPendingDeps[b] > 0u));
            if (windowOpen)
            {
                return true;
            }
            fullQueueMask |= qBit;
        }
        return false;
    }

    bool GpuGraphExecution::tryClaimPassLocked(uint32_t &outPassIndex) noexcept
    {
        while (mFirstUnclaimedBatch < mBatches.size() &&
               mBatchNextPass[mFirstUnclaimedBatch] >= mBatches[mFirstUnclaimedBatch].end)
        {
            ++mFirstUnclaimedBatch;
        }
        if (mStopping.load(eastl::memory_order_acquire) || mFirstUnclaimedBatch >= mBatches.size())
        {
            return false;
        }
        const uint32_t maxLookaheadBatches = eastl::max<uint32_t>(4u, mLanes);
        uint32_t fullQueueMask = mEmptyQueueMask;
        for (uint32_t b = mFirstUnclaimedBatch; b < mBatches.size() && fullQueueMask != 0x7u; ++b)
        {
            if (mBatchNextPass[b] >= mBatches[b].end)
            {
                continue;
            }
            const uint32_t q = mBatches[b].queue;
            const uint32_t qBit = 1u << q;
            if ((fullQueueMask & qBit) != 0)
            {
                continue;
            }
            const uint32_t oldestOrd = mQueueSubmittedCursor[q];
            const uint32_t ord = mBatchQueueOrdinal[b];
            const bool withinLookahead = (ord == oldestOrd) || (ord - oldestOrd < maxLookaheadBatches);
            if (!withinLookahead)
            {
                fullQueueMask |= qBit;
                continue;
            }
            if (mBatchReservedEncoders[b] == 0u)
            {
                const uint32_t reservation = batchEncoderReservation(b);
                if (!mDevice->tryReserveQueueEncoders(q, reservation, mBatchPendingDeps[b] > 0u))
                {
                    fullQueueMask |= qBit;
                    continue;
                }
                mBatchReservedEncoders[b] = reservation;
            }
            outPassIndex = mBatchNextPass[b]++;
            ++mClaimedPasses;
            ++mQueueUnsubmittedPasses[q];
            return true;
        }
        return false;
    }

    void GpuGraphExecution::onQueueBudgetAvailable(uint32_t queueIndex) noexcept
    {
        if (queueIndex < mWaitingOnQueueBudget.size())
        {
            std::lock_guard lock(mMutex);
            mWaitingOnQueueBudget[queueIndex] = false;
            mWaitingOnQueueSpeculative[queueIndex] = false;
        }
        ensureRecordingProgress();
        tryFinish();
    }

    void GpuGraphExecution::ensureRecordingProgress() noexcept
    {
        for (;;)
        {
            uint32_t wakeLanes = 0u;
            eastl::fixed_vector<QueueWaitRequest, 3, false> waitRequests;
            {
                std::lock_guard lock(mMutex);
                if (mStopping.load(eastl::memory_order_acquire) || mClaimedPasses >= mPasses.size())
                {
                    return;
                }
                if (hasOpenWindowPassLocked())
                {
                    if (mRecording < mLanes)
                    {
                        const uint32_t remainingPasses = static_cast<uint32_t>(mPasses.size() - mClaimedPasses);
                        wakeLanes = eastl::min(mLanes - mRecording, remainingPasses);
                        mRecording += wakeLanes;
                    }
                }
                else
                {
                    const uint32_t maxLookaheadBatches = eastl::max<uint32_t>(4u, mLanes);
                    uint32_t inspectedQueueMask = mEmptyQueueMask;
                    for (uint32_t b = mFirstUnclaimedBatch; b < mBatches.size() && inspectedQueueMask != 0x7u; ++b)
                    {
                        if (mBatchNextPass[b] >= mBatches[b].end)
                        {
                            continue;
                        }
                        const uint32_t q = mBatches[b].queue;
                        const uint32_t qBit = 1u << q;
                        if ((inspectedQueueMask & qBit) != 0u)
                        {
                            continue;
                        }
                        inspectedQueueMask |= qBit;
                        const uint32_t oldestOrd = mQueueSubmittedCursor[q];
                        const uint32_t ord = mBatchQueueOrdinal[b];
                        const bool withinLookahead = (ord == oldestOrd) || (ord - oldestOrd < maxLookaheadBatches);
                        if (!withinLookahead || mBatchReservedEncoders[b] > 0u)
                        {
                            continue;
                        }
                        const uint32_t needed = batchEncoderReservation(b);
                        const bool speculative = mBatchPendingDeps[b] > 0u;
                        if (speculative && !mDevice->canEverReserveSpeculatively(q, needed))
                        {
                            continue;
                        }
                        if (!mWaitingOnQueueBudget[q] || (mWaitingOnQueueSpeculative[q] && !speculative))
                        {
                            mWaitingOnQueueBudget[q] = true;
                            mWaitingOnQueueSpeculative[q] = speculative;
                            waitRequests.push_back({q, needed, speculative});
                        }
                    }
                }
            }
            if (wakeLanes > 0u)
            {
                scheduleRecordingLanes(wakeLanes);
                return;
            }
            if (waitRequests.empty())
            {
                return;
            }
            bool budgetOpenedImmediately = false;
            for (const auto &request : waitRequests)
            {
                bool registered = false;
                try
                {
                    registered = mDevice->waitForQueueEncoders(
                        request.queue,
                        request.needed,
                        request.speculative,
                        this,
                        QueueBudgetCallback(mRuntime, shared_from_this(), request.queue));
                }
                catch (...)
                {
                    fail(std::current_exception());
                    {
                        std::lock_guard lock(mMutex);
                        mWaitingOnQueueBudget[request.queue] = false;
                        mWaitingOnQueueSpeculative[request.queue] = false;
                    }
                    return;
                }
                if (!registered)
                {
                    std::lock_guard lock(mMutex);
                    mWaitingOnQueueBudget[request.queue] = false;
                    mWaitingOnQueueSpeculative[request.queue] = false;
                    budgetOpenedImmediately = true;
                }
            }
            if (!budgetOpenedImmediately)
            {
                return;
            }
        }
    }

    void GpuGraphExecution::releaseAbandonedReservations() noexcept
    {
        eastl::vector<RHI::CommandEncoder> abandonedEncoders;
        std::array<uint32_t, 3> releasedPerQueue{};
        bool cancelWaiters = false;
        {
            std::lock_guard lock(mMutex);
            if (mRecording != 0u || mInFlightSubmits != 0u)
            {
                return;
            }
            if (!mStopping.load(eastl::memory_order_acquire) && mSubmittedBatches != mBatches.size())
            {
                return;
            }
            for (size_t index = 0; index < mEncoders.size(); ++index)
            {
                if (mEncoders[index] != nullptr)
                {
                    abandonedEncoders.push_back(eastl::move(mEncoders[index]));
                }
            }
            for (size_t b = 0; b < mBatchReservedEncoders.size(); ++b)
            {
                const uint32_t reserved = eastl::exchange(mBatchReservedEncoders[b], 0u);
                if (reserved > 0u)
                {
                    releasedPerQueue[mBatches[b].queue] += reserved;
                }
            }
            for (uint32_t q = 0u; q < 3u; ++q)
            {
                mWaitingOnQueueBudget[q] = false;
                mWaitingOnQueueSpeculative[q] = false;
            }
            cancelWaiters = true;
        }
        abandonedEncoders.clear();
        if (cancelWaiters)
        {
            mDevice->cancelQueueEncoderWaiters(this);
        }
        for (uint32_t q = 0u; q < 3u; ++q)
        {
            if (releasedPerQueue[q] > 0u)
            {
                mDevice->releaseQueueEncoders(q, releasedPerQueue[q]);
            }
        }
    }

    void GpuGraphExecution::record() noexcept
    {
        try
        {
            for (;;)
            {
                uint32_t index = 0;
                {
                    std::lock_guard lock(mMutex);
                    if (!tryClaimPassLocked(index))
                    {
                        break;
                    }
                }
                auto encoder = mDevice->queue(mPassQueues[index])->createCommandEncoder();
                mPasses[index].record(encoder, mProfileSession.timestampWritesFor(index));
                encoder->end();
                bool submitNow = false;
                {
                    std::lock_guard lock(mMutex);
                    mEncoders[index] = eastl::move(encoder);
                    const uint32_t b = mPassToBatch[index];
                    if (--mBatchRemainingPasses[b] == 0 && mBatchPendingDeps[b] == 0 && !mBatchQueued[b] &&
                        !mStopping.load(eastl::memory_order_acquire))
                    {
                        mBatchQueued[b] = true;
                        mReadyBatches.push_back(b);
                        submitNow = true;
                    }
                }
                if (submitNow)
                {
                    submitReadyBatches();
                }
            }
        }
        catch (...)
        {
            fail(std::current_exception());
        }
        {
            std::lock_guard lock(mMutex);
            --mRecording;
        }
        ensureRecordingProgress();
        tryFinish();
    }

    void GpuGraphExecution::submitReadyBatches() noexcept
    {
        for (;;)
        {
            uint32_t batchIndex = UINT32_MAX;
            uint32_t batchPassCount = 0;
            uint32_t reservedEncoders = 0;
            eastl::vector<RHI::CommandEncoder> encoders;
            eastl::vector<RHI::Submission> waits;
            {
                std::lock_guard lock(mMutex);
                if (mReadyBatches.empty() || mStopping.load(eastl::memory_order_acquire))
                {
                    break;
                }
                batchIndex = mReadyBatches.back();
                mReadyBatches.pop_back();
                ++mInFlightSubmits;
                reservedEncoders = eastl::exchange(mBatchReservedEncoders[batchIndex], 0u);
                const uint32_t begin = batchIndex == 0 ? 0 : mBatches[batchIndex - 1].end;
                batchPassCount = mBatches[batchIndex].end - begin;
                encoders.assign(mEncoders.begin() + begin, mEncoders.begin() + mBatches[batchIndex].end);
                for (uint32_t p = begin; p < mBatches[batchIndex].end; ++p)
                {
                    mEncoders[p] = {};
                }
                for (uint32_t predecessor : mBatches[batchIndex].waits)
                {
                    waits.push_back(*mSubmissions[predecessor]);
                }
                for (uint32_t p = begin; p < mBatches[batchIndex].end; ++p)
                {
                    for (const auto &externalWait : mPasses[p].externalWaits)
                    {
                        if (externalWait.isValid() && eastl::find(waits.begin(), waits.end(), externalWait) == waits.end())
                        {
                            waits.push_back(externalWait);
                        }
                    }
                }
            }

            const uint32_t queueIndex = mBatches[batchIndex].queue;
            const uint32_t batchBeginPass = batchIndex == 0 ? 0 : mBatches[batchIndex - 1].end;
            const uint32_t batchEndPass = mBatches[batchIndex].end;
            RHI::Submission submission;
            bool submitted = false;
            try
            {
                if (mProfileSession.hasBatchQueries(batchBeginPass, batchEndPass))
                {
                    auto resolveEncoder = mDevice->queue(queueIndex)->createCommandEncoder();
                    mProfileSession.encodeBatchResolve(queueIndex, batchBeginPass, batchEndPass, resolveEncoder);
                    resolveEncoder->end();
                    encoders.push_back(eastl::move(resolveEncoder));
                }
                auto lock = mDevice->lockQueue(queueIndex);
                submission = mDevice->queue(queueIndex)->submit(encoders, waits);
                submitted = true;
            }
            catch (...)
            {
                fail(std::current_exception());
                encoders.clear();
                if (reservedEncoders > 0u)
                {
                    mDevice->releaseQueueEncoders(queueIndex, reservedEncoders);
                }
                {
                    std::lock_guard lock(mMutex);
                    --mInFlightSubmits;
                }
                break;
            }
            encoders.clear();

            if (submitted)
            {
                RHI::Submission callbackSub = submission;
                bool trackCompletion = false;
                {
                    std::lock_guard lock(mMutex);
                    mSubmissions[batchIndex] = eastl::move(submission);
                    mBatchSubmitted[batchIndex] = true;
                    const auto &qBatches = mQueueBatches[queueIndex];
                    auto &cursor = mQueueSubmittedCursor[queueIndex];
                    while (cursor < qBatches.size() && mBatchSubmitted[qBatches[cursor]])
                    {
                        ++cursor;
                    }
                    mQueueUnsubmittedPasses[queueIndex] -= batchPassCount;
                    ++mSubmittedBatches;
                    const bool stopping = mStopping.load(eastl::memory_order_acquire);
                    trackCompletion = mBatchHasCompletionNode[batchIndex] ||
                        mBatchDependents[batchIndex].empty() ||
                        stopping;
                    if (trackCompletion)
                    {
                        mBatchAwaitAttempted[batchIndex] = true;
                        mBatchCompletionTracked[batchIndex] = true;
                        ++mPendingCompletions;
                    }
                    if (!stopping)
                    {
                        for (uint32_t dep : mBatchDependents[batchIndex])
                        {
                            if (--mBatchPendingDeps[dep] == 0 && mBatchRemainingPasses[dep] == 0 && !mBatchQueued[dep])
                            {
                                mBatchQueued[dep] = true;
                                mReadyBatches.push_back(dep);
                            }
                        }
                    }
                }
                if (reservedEncoders > 0u)
                {
                    mDevice->releaseQueueEncoders(queueIndex, reservedEncoders);
                }
                if (trackCompletion)
                {
                    bool registered = false;
                    try
                    {
                        mDevice->awaitSubmission(
                            queueIndex,
                            callbackSub,
                            BatchCompletionCallback(mRuntime, shared_from_this(), batchIndex));
                        registered = true;
                    }
                    catch (...)
                    {
                        fail(std::current_exception());
                        try
                        {
                            callbackSub.wait();
                        }
                        catch (...)
                        {
                            fail(std::current_exception());
                        }
                    }
                    {
                        std::lock_guard lock(mMutex);
                        if (!registered)
                        {
                            mBatchCompletionTracked[batchIndex] = false;
                            --mPendingCompletions;
                        }
                        --mInFlightSubmits;
                    }
                }
                else
                {
                    std::lock_guard lock(mMutex);
                    --mInFlightSubmits;
                }
            }
        }
        ensureRecordingProgress();
        tryFinish();
    }

    void GpuGraphExecution::onBatchCompleted(uint32_t batch) noexcept
    {
        try
        {
            bool batchCompleted = true;
            if (mSubmissions[batch])
            {
                batchCompleted = mSubmissions[batch]->isComplete();
            }
            if (!batchCompleted)
            {
                throw std::runtime_error("GPU batch completion callback fired before submission completed.");
            }
            if (!mStopping.load(eastl::memory_order_acquire))
            {
                const uint32_t begin = batch == 0 ? 0 : mBatches[batch - 1].end;
                for (uint32_t index = begin; index < mBatches[batch].end; ++index)
                {
                    if (mPasses[index].completionNode != UINT32_MAX)
                    {
                        mProgress(mPasses[index].completionNode);
                    }
                }
            }
        }
        catch (...)
        {
            // Device loss terminates this completion; other accepted batches still drain.
            fail(std::current_exception());
        }
        {
            std::lock_guard lock(mMutex);
            eastl::vector<uint32_t> releaseStack;
            releaseStack.push_back(batch);
            while (!releaseStack.empty())
            {
                const uint32_t b = releaseStack.back();
                releaseStack.pop_back();
                if (b >= mBatchLoadRetained.size() || !mBatchLoadRetained[b])
                {
                    continue;
                }
                mBatchLoadRetained[b] = false;
                mDevice->releaseQueueLoad(mBatches[b].queue);
                if (mBatches[b].previousOnQueue != UINT32_MAX && mBatchLoadRetained[mBatches[b].previousOnQueue])
                {
                    releaseStack.push_back(mBatches[b].previousOnQueue);
                }
                for (uint32_t pred : mBatches[b].waits)
                {
                    if (pred != UINT32_MAX && mBatchLoadRetained[pred])
                    {
                        releaseStack.push_back(pred);
                    }
                }
            }
            --mPendingCompletions;
        }
        tryFinish();
    }

    void GpuGraphExecution::tryFinish() noexcept
    {
        releaseAbandonedReservations();
        eastl::vector<eastl::pair<uint32_t, RHI::Submission>> frontierBatches;
        eastl::function<void(std::exception_ptr)> completion;
        std::exception_ptr failure;
        PassProfileSession finishedProfileSession;
        bool canHarvestProfile = false;
        {
            std::lock_guard lock(mMutex);
            if (mCompletionCalled || mRecording != 0 || mInFlightSubmits != 0 || mPendingCompletions != 0)
            {
                return;
            }
            if (!mStopping.load(eastl::memory_order_acquire) && mSubmittedBatches != mBatches.size())
            {
                return;
            }
            for (uint32_t b = 0; b < mBatches.size(); ++b)
            {
                if (!mBatchSubmitted[b] || mBatchCompletionTracked[b] || mBatchAwaitAttempted[b])
                {
                    continue;
                }
                bool coveredBySubmittedDependent = false;
                for (uint32_t dep : mBatchDependents[b])
                {
                    if (mBatchSubmitted[dep])
                    {
                        coveredBySubmittedDependent = true;
                        break;
                    }
                }
                if (!coveredBySubmittedDependent && mSubmissions[b])
                {
                    mBatchAwaitAttempted[b] = true;
                    mBatchCompletionTracked[b] = true;
                    ++mPendingCompletions;
                    frontierBatches.emplace_back(b, *mSubmissions[b]);
                }
            }
            if (frontierBatches.empty())
            {
                if (!mFailure)
                {
                    try
                    {
                        for (uint32_t b = 0; b < mBatches.size(); ++b)
                        {
                            if (mBatchSubmitted[b] && !mBatchCompletionTracked[b] && mSubmissions[b])
                            {
                                (void)mSubmissions[b]->isComplete();
                            }
                        }
                    }
                    catch (...)
                    {
                        mFailure = std::current_exception();
                    }
                }
                for (size_t b = 0; b < mBatches.size(); ++b)
                {
                    if (b < mBatchLoadRetained.size() && mBatchLoadRetained[b])
                    {
                        mBatchLoadRetained[b] = false;
                        mDevice->releaseQueueLoad(mBatches[b].queue);
                    }
                }
                mCompletionCalled = true;
                failure = mFailure;
                canHarvestProfile = !mFailure &&
                    !mStopping.load(eastl::memory_order_acquire) &&
                    mSubmittedBatches == mBatches.size();
                finishedProfileSession = eastl::move(mProfileSession);
                completion = eastl::move(mCompletion);
            }
        }
        if (!frontierBatches.empty())
        {
            uint32_t failedRegistrations = 0;
            for (const auto &entry : frontierBatches)
            {
                const uint32_t batchIndex = entry.first;
                try
                {
                    mDevice->awaitSubmission(
                        mBatches[batchIndex].queue,
                        entry.second,
                        BatchCompletionCallback(mRuntime, shared_from_this(), batchIndex));
                }
                catch (...)
                {
                    fail(std::current_exception());
                    try
                    {
                        entry.second.wait();
                    }
                    catch (...)
                    {
                        fail(std::current_exception());
                    }
                    std::lock_guard lock(mMutex);
                    mBatchCompletionTracked[batchIndex] = false;
                    --mPendingCompletions;
                    ++failedRegistrations;
                }
            }
            if (failedRegistrations > 0)
            {
                tryFinish();
            }
            return;
        }
        if (finishedProfileSession.active())
        {
            if (auto *profiler = mRuntime.passWeightProfiler())
            {
                if (canHarvestProfile)
                {
                    profiler->harvestSession(eastl::move(finishedProfileSession));
                }
                else
                {
                    profiler->cancelSession(eastl::move(finishedProfileSession));
                }
            }
        }
        if (completion)
        {
            completion(failure);
        }
    }
}
