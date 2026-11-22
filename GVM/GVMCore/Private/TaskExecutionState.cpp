#include "TaskExecutionState.hpp"
#include "TaskRuntimeState.hpp"
#include "GpuGraphExecution.hpp"
#include "GraphResourceAllocation.hpp"
#include "../Public/RenderNode.hpp"

#include <EASTL/algorithm.h>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace GVM::Core::Private
{
    TaskExecutionState::TaskExecutionState(eastl::shared_ptr<TaskRuntimeState> runtime)
        : mRuntime(eastl::move(runtime)), mBudget(eastl::make_shared<ExecutionBudget>()), mOwnsRuntimeAdmission(true)
    {
        mBudget->maxInFlight = mRuntime->maximumInFlight();
    }

    TaskExecutionState::TaskExecutionState(
        eastl::shared_ptr<TaskRuntimeState> runtime,
        eastl::shared_ptr<ExecutionBudget> budget,
        eastl::shared_ptr<TaskExecutionState> parent,
        uint32_t parentNodeIndex,
        uint64_t childId)
        : mRuntime(eastl::move(runtime)),
          mBudget(eastl::move(budget)),
          mParent(eastl::move(parent)),
          mParentNodeIndex(parentNodeIndex),
          mChildId(childId),
          mOwnsRuntimeAdmission(false)
    {
    }

    void TaskExecutionState::start(eastl::function<TaskGraphOutput()> builder)
    {
        mBudget->inFlightTasks.store(1u, std::memory_order_relaxed);
        {
            std::lock_guard lock(mMutex);
            mInFlight = 1;
        }
        try
        {
            ExecutorTask task([self = shared_from_this(), builder = eastl::move(builder)]() mutable
            {
                self->prepare(eastl::move(builder));
            });
            const auto accepted = mRuntime->schedule(eastl::move(task));
            if (accepted != ScheduleResult::Accepted)
            {
                scheduleFailed(Preparation, accepted);
            }
        }
        catch (...)
        {
            finish(Preparation, {}, std::current_exception());
        }
    }

    void TaskExecutionState::reject()
    {
        std::lock_guard lock(mMutex);
        mStatus = ExecutionStatus::Rejected;
        mDiagnostic = "TaskRuntime admission is closed or full.";
        mTerminalPublished = true;
        mRuntime.reset();
    }

    ExecutionStatus TaskExecutionState::status() const
    {
        std::lock_guard lock(mMutex);
        return terminalLocked() && !mTerminalPublished ? ExecutionStatus::Running : mStatus;
    }

    eastl::string TaskExecutionState::error() const
    {
        std::exception_ptr failure;
        const char *diagnostic;
        {
            std::lock_guard lock(mMutex);
            failure = mFailure;
            diagnostic = mDiagnostic;
        }
        if (failure)
        {
            try
            {
                std::rethrow_exception(failure);
            }
            catch (const std::exception &error)
            {
                const char *message = error.what();
                size_t length = 0;
                while (length < 4096 && message[length] != '\0')
                {
                    ++length;
                }
                return eastl::string(message, length);
            }
            catch (...)
            {
                return "Task raised a non-standard exception.";
            }
        }
        return diagnostic;
    }

    bool TaskExecutionState::wait(uint64_t timeoutNanoseconds) const
    {
        std::unique_lock lock(mMutex);
        if (mTerminalPublished)
        {
            return true;
        }
        auto runtime = mRuntime;
        const auto completed = [](const void *ctx) noexcept -> bool
        {
            return static_cast<const TaskExecutionState *>(ctx)->mTerminalPublished;
        };
        return runtime->waitCondition(lock, mCompleted, timeoutNanoseconds, completed, this);
    }

    void TaskExecutionState::cancelSingleNode(
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> &outGpus,
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> &outChildren,
        bool &outTerminal) noexcept
    {
        std::lock_guard lock(mMutex);
        if (terminalLocked())
        {
            outTerminal = false;
            return;
        }
        if (mStopStatus == ExecutionStatus::Pending)
        {
            mStopStatus = ExecutionStatus::Cancelled;
            mDiagnostic = "Task execution was cancelled.";
        }
        for (const auto &group : mActiveGpuLaunchGroups)
        {
            if (group.execution)
            {
                outGpus.push_back(group.execution);
            }
        }
        for (const auto &child : mActiveChildExecutions)
        {
            if (child.execution)
            {
                outChildren.push_back(child.execution);
            }
        }
        outTerminal = finishLocked();
    }

    void TaskExecutionState::cancelSubgraphTrees(eastl::vector<eastl::shared_ptr<TaskExecutionState>> roots) noexcept
    {
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> gpusToCancel;
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> terminalStates;
        while (!roots.empty())
        {
            auto current = eastl::move(roots.back());
            roots.pop_back();
            if (!current)
            {
                continue;
            }
            bool nodeTerminal = false;
            current->cancelSingleNode(gpusToCancel, roots, nodeTerminal);
            if (nodeTerminal)
            {
                terminalStates.push_back(eastl::move(current));
            }
        }
        for (const auto &gpu : gpusToCancel)
        {
            gpu->cancel();
        }
        for (const auto &state : terminalStates)
        {
            state->notifyTerminal();
        }
    }

    void TaskExecutionState::cancel()
    {
        bool terminal = false;
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> activeGpus;
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> activeChildren;
        cancelSingleNode(activeGpus, activeChildren, terminal);
        for (const auto &gpu : activeGpus)
        {
            gpu->cancel();
        }
        if (!activeChildren.empty())
        {
            cancelSubgraphTrees(eastl::move(activeChildren));
        }
        if (terminal)
        {
            notifyTerminal();
        }
    }

    eastl::any *TaskExecutionState::beginResultClaim(bool &outSingleTransfer) const
    {
        mMutex.lock();
        bool locked = true;
        try
        {
            if (!mTerminalPublished)
            {
                throw std::logic_error("Execution::get requires a terminal successful execution.");
            }
            if (mFailure)
            {
                const auto failure = mFailure;
                locked = false;
                mMutex.unlock();
                std::rethrow_exception(failure);
            }
            if (mStatus != ExecutionStatus::Succeeded)
            {
                throw std::runtime_error(mDiagnostic);
            }
            if (mHasExportedResources && mExportedResourcesClaimed)
            {
                throw std::logic_error("Exported GPU allocations have already been claimed from this Execution.");
            }
            outSingleTransfer = mHasExportedResources;
            return &mFinalResult;
        }
        catch (...)
        {
            if (locked)
            {
                mMutex.unlock();
            }
            throw;
        }
    }

    void TaskExecutionState::endResultClaim(bool commitSingleTransfer, bool movedFrom) const noexcept
    {
        if (mHasExportedResources)
        {
            if (commitSingleTransfer)
            {
                mExportedResourcesClaimed = true;
                for (const auto &allocation : mAllocations)
                {
                    allocation->releaseOwnership();
                }
                mAllocations.clear();
            }
            else if (movedFrom)
            {
                mExportedResourcesClaimed = true;
            }
        }
        mMutex.unlock();
    }

    eastl::any TaskExecutionState::result() const
    {
        bool singleTransfer = false;
        eastl::any *stored = beginResultClaim(singleTransfer);
        try
        {
            eastl::any copy = singleTransfer ? eastl::move(*stored) : *stored;
            endResultClaim(true, singleTransfer);
            return copy;
        }
        catch (...)
        {
            endResultClaim(false, singleTransfer);
            throw;
        }
    }

    void TaskExecutionState::initializePlan(const TaskGraphOutput &output)
    {
        if (!output.definition)
        {
            throw std::invalid_argument("Graph entry returned an unbound Task.");
        }
        const uint32_t maxTasks = mRuntime->maximumTasks();
        auto tasks = output.definition->snapshot(maxTasks);
        if (tasks.empty() || output.index >= tasks.size())
        {
            throw std::invalid_argument("Graph entry returned an invalid producer index.");
        }
        const uint32_t addCount = static_cast<uint32_t>(tasks.size());
        uint32_t currentTasks = mBudget->taskCount.load(std::memory_order_relaxed);
        for (;;)
        {
            if (addCount > maxTasks || currentTasks > maxTasks - addCount)
            {
                throw std::length_error("TaskGraph exceeds the runtime task budget.");
            }
            if (mBudget->taskCount.compare_exchange_weak(
                    currentTasks, currentTasks + addCount, std::memory_order_relaxed))
            {
                break;
            }
        }

        eastl::vector<eastl::vector<uint32_t>> successors(tasks.size());
        eastl::vector<uint32_t> pending(tasks.size());
        eastl::vector<uint32_t> ready;
        // A readback enters the ready list once for expansion and once for CPU resolution.
        ready.reserve(tasks.size() * 2);
        TaskResults results(tasks.size());
        eastl::vector<eastl::function<eastl::any()>> resolvers(tasks.size());
        eastl::vector<bool> gpuValuesReady(tasks.size(), false);
        uint32_t readyGpuBuilds = 0;
        bool hasGpuNodes = false;
        for (uint32_t index = 0; index < tasks.size(); ++index)
        {
            if (tasks[index].buildsSubgraph)
            {
                hasGpuNodes = true;
            }
            pending[index] = static_cast<uint32_t>(tasks[index].predecessors.size());
            if (pending[index] == 0)
            {
                ready.push_back(index);
                if (tasks[index].buildsSubgraph)
                {
                    ++readyGpuBuilds;
                }
            }
            for (uint32_t predecessor : tasks[index].predecessors)
            {
                successors[predecessor].push_back(index);
            }
        }

        constexpr size_t kMaxBitsetReachabilityTasks = 8192u;
        size_t reachabilityWords = 0;
        eastl::vector<uint64_t> descendantBits;
        if (hasGpuNodes && tasks.size() <= kMaxBitsetReachabilityTasks)
        {
            reachabilityWords = (tasks.size() + 63u) / 64u;
            descendantBits.assign(tasks.size() * reachabilityWords, 0ull);
            for (size_t rev = tasks.size(); rev > 0; --rev)
            {
                const size_t node = rev - 1u;
                uint64_t *dstRow = descendantBits.data() + node * reachabilityWords;
                for (uint32_t succ : successors[node])
                {
                    dstRow[succ >> 6u] |= (1ull << (succ & 63u));
                    const uint64_t *succRow = descendantBits.data() + static_cast<size_t>(succ) * reachabilityWords;
                    for (size_t w = 0; w < reachabilityWords; ++w)
                    {
                        dstRow[w] |= succRow[w];
                    }
                }
            }
        }

        {
            std::lock_guard lock(mMutex);
            mStatus = ExecutionStatus::Running;
            mTasks = eastl::move(tasks);
            mSuccessors = eastl::move(successors);
            mDescendantBits = eastl::move(descendantBits);
            mReachabilityWords = reachabilityWords;
            mPending = eastl::move(pending);
            mReady = eastl::move(ready);
            mResults = eastl::move(results);
            mGpuResolvers = eastl::move(resolvers);
            mGpuValuesReady = eastl::move(gpuValuesReady);
            mOutput = output.index;
            mResolveOutput = output.resolve;
            mOutputResources = output.resources;
            mReadyGpuBuilds = readyGpuBuilds;
            mRemaining = static_cast<uint32_t>(mTasks.size());
            mPrepared = true;
        }
    }

    uint32_t TaskExecutionState::tryAcquireSharedInFlightSlots(uint32_t requested, uint32_t maxInFlight) noexcept
    {
        if (requested == 0u || maxInFlight == 0u)
        {
            return 0u;
        }
        uint32_t observed = mBudget->inFlightTasks.load(std::memory_order_acquire);
        while (observed < maxInFlight)
        {
            const uint32_t grant = eastl::min(requested, maxInFlight - observed);
            if (mBudget->inFlightTasks.compare_exchange_weak(
                    observed, observed + grant, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return grant;
            }
        }
        return 0u;
    }

    void TaskExecutionState::registerCpuBudgetWaiterLocked()
    {
        if (!mQueuedInCpuSlotWaiters)
        {
            if (mBudget->cpuSlotWaiterHead >= 64u &&
                mBudget->cpuSlotWaiterHead * 2u >= mBudget->cpuSlotWaiters.size())
            {
                mBudget->cpuSlotWaiters.erase(
                    mBudget->cpuSlotWaiters.begin(),
                    mBudget->cpuSlotWaiters.begin() + mBudget->cpuSlotWaiterHead);
                mBudget->cpuSlotWaiterHead = 0u;
            }
            mBudget->cpuSlotWaiters.push_back(shared_from_this());
            mQueuedInCpuSlotWaiters = true;
        }
        mBudget->hasCpuSlotWaiters.store(true, std::memory_order_seq_cst);
    }

    void TaskExecutionState::registerGpuBudgetWaiterLocked()
    {
        if (!mQueuedInGpuHazardWaiters)
        {
            mBudget->gpuHazardWaiters.push_back(shared_from_this());
            mQueuedInGpuHazardWaiters = true;
        }
    }

    bool TaskExecutionState::registerWaitingAndCheckInFlightAvailable(uint32_t maxInFlight)
    {
        std::lock_guard budgetLock(mBudget->coordinatorMutex);
        registerCpuBudgetWaiterLocked();
        return mBudget->inFlightTasks.load(std::memory_order_seq_cst) < maxInFlight;
    }

    void TaskExecutionState::wakeCpuBudgetWaiters() noexcept
    {
        const uint32_t maxInFlight = mBudget->maxInFlight;
        for (;;)
        {
            eastl::shared_ptr<TaskExecutionState> nextState;
            {
                std::lock_guard budgetLock(mBudget->coordinatorMutex);
                while (mBudget->cpuSlotWaiterHead < mBudget->cpuSlotWaiters.size())
                {
                    if (mBudget->inFlightTasks.load(std::memory_order_seq_cst) >= maxInFlight)
                    {
                        mBudget->hasCpuSlotWaiters.store(true, std::memory_order_seq_cst);
                        if (mBudget->inFlightTasks.load(std::memory_order_seq_cst) >= maxInFlight)
                        {
                            return;
                        }
                    }
                    auto state = mBudget->cpuSlotWaiters[mBudget->cpuSlotWaiterHead++].lock();
                    if (state)
                    {
                        state->mQueuedInCpuSlotWaiters = false;
                        nextState = eastl::move(state);
                        break;
                    }
                }
                if (!nextState)
                {
                    mBudget->cpuSlotWaiters.clear();
                    mBudget->cpuSlotWaiterHead = 0u;
                    mBudget->hasCpuSlotWaiters.store(false, std::memory_order_seq_cst);
                    return;
                }
                if (mBudget->cpuSlotWaiterHead >= mBudget->cpuSlotWaiters.size())
                {
                    mBudget->cpuSlotWaiters.clear();
                    mBudget->cpuSlotWaiterHead = 0u;
                    mBudget->hasCpuSlotWaiters.store(false, std::memory_order_seq_cst);
                }
            }
            nextState->dispatchReady();
        }
    }

    void TaskExecutionState::wakeGpuBudgetWaiters() noexcept
    {
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> toWake;
        {
            std::lock_guard budgetLock(mBudget->coordinatorMutex);
            eastl::vector<eastl::weak_ptr<TaskExecutionState>> pending;
            pending.swap(mBudget->gpuHazardWaiters);
            toWake.reserve(pending.size());
            for (const auto &weakState : pending)
            {
                if (auto state = weakState.lock())
                {
                    state->mQueuedInGpuHazardWaiters = false;
                    toWake.push_back(eastl::move(state));
                }
            }
        }
        for (const auto &state : toWake)
        {
            state->dispatchReady();
        }
    }

    void TaskExecutionState::releaseSharedInFlightSlotAndWake() noexcept
    {
        mBudget->inFlightTasks.fetch_sub(1u, std::memory_order_seq_cst);
        if (mBudget->hasCpuSlotWaiters.load(std::memory_order_seq_cst))
        {
            wakeCpuBudgetWaiters();
        }
    }

    void TaskExecutionState::releaseSharedGpuGroupAndWake(uint64_t globalId) noexcept
    {
        if (globalId == 0u)
        {
            return;
        }
        bool shouldWake = false;
        {
            std::lock_guard budgetLock(mBudget->coordinatorMutex);
            for (auto it = mBudget->activeGpuGroups.begin(); it != mBudget->activeGpuGroups.end(); ++it)
            {
                if (it->globalId == globalId)
                {
                    mBudget->activeGpuGroups.erase(it);
                    break;
                }
            }
            shouldWake = !mBudget->gpuHazardWaiters.empty();
        }
        if (shouldWake)
        {
            wakeGpuBudgetWaiters();
        }
    }

    void TaskExecutionState::startChild() noexcept
    {
        bool retriedPendingGpu = false;
        for (;;)
        {
            dispatchReady();
            bool retryDispatch = false;
            bool terminal = false;
            {
                std::lock_guard lock(mMutex);
                if (terminalLocked())
                {
                    return;
                }
                if (mInFlight == 0 && mActiveGpuLaunchGroups.empty() && mActiveChildExecutions.empty())
                {
                    if (mStopStatus == ExecutionStatus::Pending && mReadyCursor < mReady.size())
                    {
                        if (!mDispatching && mBudget->inFlightTasks.load(std::memory_order_acquire) < mRuntime->maximumInFlight())
                        {
                            retryDispatch = true;
                        }
                    }
                    else if (mStopStatus == ExecutionStatus::Pending && !mPendingGpuFragments.empty())
                    {
                        bool sharedGpuActive = false;
                        {
                            std::lock_guard budgetLock(mBudget->coordinatorMutex);
                            sharedGpuActive = !mBudget->activeGpuGroups.empty();
                        }
                        if (!sharedGpuActive && !mDispatching && !retriedPendingGpu)
                        {
                            retriedPendingGpu = true;
                            retryDispatch = true;
                        }
                    }
                    else if (mPendingGpuFragments.empty() && mBlockedTaskSubgraphs.empty())
                    {
                        if (mStopStatus == ExecutionStatus::Pending && mRemaining > 0)
                        {
                            mStopStatus = ExecutionStatus::Failed;
                            mDiagnostic = "Nested TaskGraph failed to make progress.";
                        }
                        terminal = finishLocked();
                    }
                }
            }
            if (terminal)
            {
                notifyTerminal();
                return;
            }
            if (!retryDispatch)
            {
                return;
            }
        }
    }

    void TaskExecutionState::applyChildCompletionStep(ChildCompletionStep &&step, bool &outTerminal) noexcept
    {
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> failedGpus;
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> failedChildren;
        {
            std::lock_guard lock(mMutex);
            for (auto it = mActiveChildExecutions.begin(); it != mActiveChildExecutions.end(); ++it)
            {
                if (it->id == step.childId)
                {
                    mActiveChildExecutions.erase(it);
                    break;
                }
            }
            if (step.status != ExecutionStatus::Succeeded)
            {
                if (step.failure && !mFailure)
                {
                    mFailure = step.failure;
                }
                if (mStopStatus == ExecutionStatus::Pending)
                {
                    mStopStatus = (step.status == ExecutionStatus::Cancelled)
                        ? ExecutionStatus::Cancelled
                        : ExecutionStatus::Failed;
                    if (step.diagnostic != nullptr && step.diagnostic[0] != '\0')
                    {
                        mDiagnostic = step.diagnostic;
                    }
                }
            }
            if (mStopStatus != ExecutionStatus::Pending)
            {
                for (const auto &group : mActiveGpuLaunchGroups)
                {
                    if (group.execution)
                    {
                        failedGpus.push_back(group.execution);
                    }
                }
                for (const auto &child : mActiveChildExecutions)
                {
                    if (child.execution)
                    {
                        failedChildren.push_back(child.execution);
                    }
                }
            }
            --mRemaining;
            if (mStopStatus == ExecutionStatus::Pending)
            {
                for (auto &allocation : step.exportedAllocations)
                {
                    mAllocations.push_back(eastl::move(allocation));
                }
                step.exportedAllocations.clear();
                publishLocked(step.parentNodeIndex, eastl::move(step.finalResult));
            }
            outTerminal = finishLocked();
        }
        step.exportedAllocations.clear();
        for (const auto &gpu : failedGpus)
        {
            gpu->cancel();
        }
        if (!failedChildren.empty())
        {
            cancelSubgraphTrees(eastl::move(failedChildren));
        }
        if (!outTerminal)
        {
            dispatchReady();
        }
    }

    void TaskExecutionState::prepare(eastl::function<TaskGraphOutput()> builder) noexcept
    {
        std::exception_ptr failure;
        try
        {
            bool proceed;
            {
                std::lock_guard lock(mMutex);
                proceed = mStopStatus == ExecutionStatus::Pending;
                mStatus = ExecutionStatus::Running;
            }
            if (proceed)
            {
                const auto output = builder();
                initializePlan(output);
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        builder = nullptr;
        finish(Preparation, {}, failure);
    }

    bool TaskExecutionState::LaunchGroupResourceSet::conflictsWith(const PassResourceAccess &access) const
    {
        if (access.resource == nullptr)
        {
            return false;
        }
        const auto found = byResource.find(access.resource);
        if (found == byResource.end())
        {
            return false;
        }
        const auto &intervals = found->second;
        for (const auto &priorWrite : intervals.writes)
        {
            if (access.conflicts(priorWrite))
            {
                return true;
            }
        }
        if (access.writes)
        {
            for (const auto &priorRead : intervals.reads)
            {
                if (access.conflicts(priorRead))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void TaskExecutionState::LaunchGroupResourceSet::record(const PassResourceAccess &access)
    {
        if (access.resource == nullptr)
        {
            return;
        }
        auto &intervals = byResource[access.resource];
        for (const auto &priorWrite : intervals.writes)
        {
            if (priorWrite.coversRange(access))
            {
                return;
            }
        }
        if (access.writes)
        {
            size_t keptWrites = 0;
            for (size_t i = 0; i < intervals.writes.size(); ++i)
            {
                if (!access.coversRange(intervals.writes[i]))
                {
                    if (keptWrites != i)
                    {
                        intervals.writes[keptWrites] = intervals.writes[i];
                    }
                    ++keptWrites;
                }
            }
            intervals.writes.resize(keptWrites);

            size_t keptReads = 0;
            for (size_t i = 0; i < intervals.reads.size(); ++i)
            {
                if (!access.coversRange(intervals.reads[i]))
                {
                    if (keptReads != i)
                    {
                        intervals.reads[keptReads] = intervals.reads[i];
                    }
                    ++keptReads;
                }
            }
            intervals.reads.resize(keptReads);
            intervals.writes.push_back(access);
        }
        else
        {
            for (const auto &priorRead : intervals.reads)
            {
                if (priorRead.coversRange(access))
                {
                    return;
                }
            }
            size_t keptReads = 0;
            for (size_t i = 0; i < intervals.reads.size(); ++i)
            {
                if (!access.coversRange(intervals.reads[i]))
                {
                    if (keptReads != i)
                    {
                        intervals.reads[keptReads] = intervals.reads[i];
                    }
                    ++keptReads;
                }
            }
            intervals.reads.resize(keptReads);
            intervals.reads.push_back(access);
        }
    }

    bool TaskExecutionState::isStrictDescendantBitsetLocked(uint32_t sourceNode, uint32_t targetNode) const noexcept
    {
        if (sourceNode >= mTasks.size() || targetNode >= mTasks.size() || mReachabilityWords == 0)
        {
            return false;
        }
        const size_t wordOffset = static_cast<size_t>(sourceNode) * mReachabilityWords + (targetNode >> 6u);
        return (mDescendantBits[wordOffset] & (1ull << (targetNode & 63u))) != 0ull;
    }

    void TaskExecutionState::mergeDescendantBitsetLocked(
        uint32_t rootNode,
        eastl::fixed_vector<uint64_t, 8, true> &targetBits) const noexcept
    {
        if (rootNode >= mTasks.size() || mReachabilityWords == 0)
        {
            return;
        }
        const uint64_t *srcRow = mDescendantBits.data() + static_cast<size_t>(rootNode) * mReachabilityWords;
        for (size_t w = 0; w < mReachabilityWords; ++w)
        {
            targetBits[w] |= srcRow[w];
        }
    }

    bool TaskExecutionState::hasUnfinishedGpuAncestorLocked(uint32_t nodeIndex) const
    {
        const bool useBitsets = mReachabilityWords > 0;
        for (const auto &active : mActiveGpuLaunchGroups)
        {
            if (useBitsets)
            {
                const size_t wordIdx = nodeIndex >> 6u;
                if (wordIdx < active.descendantBits.size() &&
                    (active.descendantBits[wordIdx] & (1ull << (nodeIndex & 63u))) != 0ull)
                {
                    return true;
                }
            }
            else if (nodeIndex < active.descendants.size() && active.descendants[nodeIndex])
            {
                return true;
            }
        }
        if (mPendingGpuFragments.empty())
        {
            return false;
        }
        if (useBitsets)
        {
            for (const auto &entry : mPendingGpuFragments)
            {
                if (entry.nodeIndex == nodeIndex || isStrictDescendantBitsetLocked(entry.nodeIndex, nodeIndex))
                {
                    return true;
                }
            }
            return false;
        }
        eastl::vector<bool> ancestors(mTasks.size(), false);
        markAncestorsLocked(nodeIndex, ancestors);
        for (const auto &entry : mPendingGpuFragments)
        {
            if (entry.nodeIndex < ancestors.size() && ancestors[entry.nodeIndex])
            {
                return true;
            }
        }
        return false;
    }

    void TaskExecutionState::markDescendantsLocked(uint32_t rootNode, eastl::vector<bool> &visited) const
    {
        eastl::vector<uint32_t> stack;
        stack.push_back(rootNode);
        while (!stack.empty())
        {
            const uint32_t current = stack.back();
            stack.pop_back();
            for (uint32_t successor : mSuccessors[current])
            {
                if (!visited[successor])
                {
                    visited[successor] = true;
                    stack.push_back(successor);
                }
            }
        }
    }

    void TaskExecutionState::markAncestorsLocked(uint32_t rootNode, eastl::vector<bool> &visited) const
    {
        eastl::vector<uint32_t> stack;
        if (!visited[rootNode])
        {
            visited[rootNode] = true;
            stack.push_back(rootNode);
        }
        while (!stack.empty())
        {
            const uint32_t current = stack.back();
            stack.pop_back();
            for (uint32_t predecessor : mTasks[current].predecessors)
            {
                if (!visited[predecessor])
                {
                    visited[predecessor] = true;
                    stack.push_back(predecessor);
                }
            }
        }
    }

    bool TaskExecutionState::tryCollectGpuLaunchGroupLocked(PreparedGpuLaunchGroup &outLaunchGroup, bool eagerReadbackOnly)
    {
        if (mPendingGpuFragments.empty())
        {
            return false;
        }
        for (const auto &active : mActiveGpuLaunchGroups)
        {
            if (active.hasOpaque)
            {
                return false;
            }
        }
        std::unique_lock budgetLock(mBudget->coordinatorMutex);
        for (const auto &sharedActive : mBudget->activeGpuGroups)
        {
            if (sharedActive.hasOpaque)
            {
                registerGpuBudgetWaiterLocked();
                return false;
            }
        }

        const bool useBitsets = mReachabilityWords > 0;
        eastl::vector<bool> reachesDeferredFallback;
        eastl::vector<bool> reachesSubgraphFallback;
        if (eagerReadbackOnly)
        {
            bool hasPendingDeferred = !mBlockedTaskSubgraphs.empty();
            for (const auto &entry : mPendingGpuFragments)
            {
                if (entry.deferred)
                {
                    hasPendingDeferred = true;
                    break;
                }
            }
            if (!hasPendingDeferred)
            {
                return false;
            }
            if (!useBitsets)
            {
                reachesDeferredFallback.assign(mTasks.size(), false);
                reachesSubgraphFallback.assign(mTasks.size(), false);
                for (const auto &entry : mPendingGpuFragments)
                {
                    if (entry.deferred)
                    {
                        markAncestorsLocked(entry.nodeIndex, reachesDeferredFallback);
                    }
                }
                for (uint32_t blockedSubgraph : mBlockedTaskSubgraphs)
                {
                    markAncestorsLocked(blockedSubgraph, reachesSubgraphFallback);
                }
            }
        }

        ActiveGpuLaunchGroup activeGroup;
        activeGroup.id = mNextGpuLaunchGroupId;
        eastl::fixed_vector<uint64_t, 8, true> blockedDescendantBits;
        eastl::vector<bool> blockedDescendantsFallback;
        if (useBitsets)
        {
            activeGroup.descendantBits.assign(mReachabilityWords, 0ull);
            blockedDescendantBits.assign(mReachabilityWords, 0ull);
        }
        else
        {
            activeGroup.descendants.assign(mTasks.size(), false);
            blockedDescendantsFallback.assign(mTasks.size(), false);
        }

        LaunchGroupResourceSet blockedResources;
        eastl::fixed_vector<uint8_t, 32, true> selected(mPendingGpuFragments.size(), 0u);
        bool blockedHasOpaque = false;
        bool anyHeldBack = false;
        bool heldBackBySharedGpu = false;
        bool anySelected = false;
        bool collectedDeferred = false;
        bool collectedSubgraphUnblocker = false;

        for (size_t idx = 0; idx < mPendingGpuFragments.size(); ++idx)
        {
            const auto &entry = mPendingGpuFragments[idx];
            bool fragmentOpaque = false;
            for (const auto &pass : entry.fragment.passes)
            {
                if (!pass.resources.complete)
                {
                    fragmentOpaque = true;
                    break;
                }
                for (const auto &access : pass.resources.accesses)
                {
                    if (access.resource == nullptr)
                    {
                        fragmentOpaque = true;
                        break;
                    }
                }
                if (fragmentOpaque)
                {
                    break;
                }
            }

            bool reachesDeferredTarget = true;
            bool reachesBlockedSubgraph = false;
            if (eagerReadbackOnly)
            {
                if (useBitsets)
                {
                    bool reachesDeferredReadback = false;
                    for (const auto &candidate : mPendingGpuFragments)
                    {
                        if (candidate.deferred &&
                            (entry.nodeIndex == candidate.nodeIndex ||
                             isStrictDescendantBitsetLocked(entry.nodeIndex, candidate.nodeIndex)))
                        {
                            reachesDeferredReadback = true;
                            break;
                        }
                    }
                    for (uint32_t blockedSubgraph : mBlockedTaskSubgraphs)
                    {
                        if (entry.nodeIndex == blockedSubgraph ||
                            isStrictDescendantBitsetLocked(entry.nodeIndex, blockedSubgraph))
                        {
                            reachesBlockedSubgraph = true;
                            break;
                        }
                    }
                    reachesDeferredTarget = reachesDeferredReadback || reachesBlockedSubgraph;
                }
                else
                {
                    reachesBlockedSubgraph = reachesSubgraphFallback[entry.nodeIndex];
                    reachesDeferredTarget = reachesDeferredFallback[entry.nodeIndex] || reachesBlockedSubgraph;
                }
            }

            const bool isBlockedDescendant = useBitsets
                ? ((blockedDescendantBits[entry.nodeIndex >> 6u] & (1ull << (entry.nodeIndex & 63u))) != 0ull)
                : blockedDescendantsFallback[entry.nodeIndex];

            bool conflicts = false;
            if (!reachesDeferredTarget ||
                (fragmentOpaque && (!mActiveGpuLaunchGroups.empty() || anyHeldBack)) ||
                blockedHasOpaque ||
                isBlockedDescendant)
            {
                conflicts = true;
            }
            if (!conflicts && fragmentOpaque && !mBudget->activeGpuGroups.empty())
            {
                conflicts = true;
                heldBackBySharedGpu = true;
            }
            if (!conflicts)
            {
                for (const auto &active : mActiveGpuLaunchGroups)
                {
                    const bool activeDescendant = useBitsets
                        ? ((active.descendantBits[entry.nodeIndex >> 6u] & (1ull << (entry.nodeIndex & 63u))) != 0ull)
                        : active.descendants[entry.nodeIndex];
                    if (activeDescendant)
                    {
                        conflicts = true;
                        break;
                    }
                }
            }
            if (!conflicts && !fragmentOpaque)
            {
                for (const auto &pass : entry.fragment.passes)
                {
                    for (const auto &access : pass.resources.accesses)
                    {
                        if (blockedResources.conflictsWith(access))
                        {
                            conflicts = true;
                            break;
                        }
                        for (const auto &active : mActiveGpuLaunchGroups)
                        {
                            if (active.resources.conflictsWith(access))
                            {
                                conflicts = true;
                                break;
                            }
                        }
                        if (!conflicts)
                        {
                            for (const auto &sharedActive : mBudget->activeGpuGroups)
                            {
                                if (sharedActive.resources.conflictsWith(access))
                                {
                                    conflicts = true;
                                    heldBackBySharedGpu = true;
                                    break;
                                }
                            }
                        }
                        if (conflicts)
                        {
                            break;
                        }
                    }
                    if (conflicts)
                    {
                        break;
                    }
                }
            }

            if (conflicts)
            {
                anyHeldBack = true;
                blockedHasOpaque = blockedHasOpaque || fragmentOpaque;
                if (useBitsets)
                {
                    mergeDescendantBitsetLocked(entry.nodeIndex, blockedDescendantBits);
                }
                else
                {
                    markDescendantsLocked(entry.nodeIndex, blockedDescendantsFallback);
                }
                for (const auto &pass : entry.fragment.passes)
                {
                    for (const auto &access : pass.resources.accesses)
                    {
                        blockedResources.record(access);
                    }
                }
            }
            else
            {
                selected[idx] = 1u;
                anySelected = true;
                activeGroup.hasOpaque = activeGroup.hasOpaque || fragmentOpaque;
                if (entry.deferred)
                {
                    collectedDeferred = true;
                    activeGroup.deferredNodes.push_back(entry.nodeIndex);
                }
                if (reachesBlockedSubgraph)
                {
                    collectedSubgraphUnblocker = true;
                }
                if (useBitsets)
                {
                    mergeDescendantBitsetLocked(entry.nodeIndex, activeGroup.descendantBits);
                }
                else
                {
                    markDescendantsLocked(entry.nodeIndex, activeGroup.descendants);
                }
                for (const auto &pass : entry.fragment.passes)
                {
                    for (const auto &access : pass.resources.accesses)
                    {
                        activeGroup.resources.record(access);
                    }
                }
            }
        }

        if (heldBackBySharedGpu)
        {
            registerGpuBudgetWaiterLocked();
        }
        if (!anySelected || (eagerReadbackOnly && !collectedDeferred && !collectedSubgraphUnblocker))
        {
            return false;
        }

        outLaunchGroup.id = mNextGpuLaunchGroupId++;
        outLaunchGroup.fragments.clear();
        eastl::fixed_vector<uint32_t, 32, true> selectedNodeIndices;
        uint32_t flattenedPassOffset = 0u;
        size_t writePos = 0;
        for (size_t idx = 0; idx < mPendingGpuFragments.size(); ++idx)
        {
            if (selected[idx] != 0u)
            {
                auto &frag = mPendingGpuFragments[idx].fragment;
                const uint32_t fragPassCount = static_cast<uint32_t>(frag.passes.size());
                if (flattenedPassOffset > 0u)
                {
                    for (auto &pass : frag.passes)
                    {
                        for (uint32_t &pred : pass.controlPredecessors)
                        {
                            pred += flattenedPassOffset;
                        }
                    }
                }
                selectedNodeIndices.push_back(mPendingGpuFragments[idx].nodeIndex);
                flattenedPassOffset += fragPassCount;
                outLaunchGroup.fragments.push_back(eastl::move(frag));
            }
            else
            {
                if (writePos != idx)
                {
                    mPendingGpuFragments[writePos] = eastl::move(mPendingGpuFragments[idx]);
                }
                ++writePos;
            }
        }
        mPendingGpuFragments.resize(writePos);

        const size_t selectedCount = outLaunchGroup.fragments.size();
        if (selectedCount > 1u)
        {
            bool anyExternalWaits = false;
            for (const auto &frag : outLaunchGroup.fragments)
            {
                if (!frag.externalWaits.empty())
                {
                    anyExternalWaits = true;
                    break;
                }
            }
            if (anyExternalWaits)
            {
                eastl::vector<bool> descendants;
                for (size_t a = 0; a < selectedCount; ++a)
                {
                    const auto &srcWaits = outLaunchGroup.fragments[a].externalWaits;
                    if (srcWaits.empty())
                    {
                        continue;
                    }
                    if (!useBitsets)
                    {
                        descendants.assign(mTasks.size(), false);
                        markDescendantsLocked(selectedNodeIndices[a], descendants);
                    }
                    for (size_t b = 0; b < selectedCount; ++b)
                    {
                        if (a == b || outLaunchGroup.fragments[b].passes.empty())
                        {
                            continue;
                        }
                        const bool reaches = useBitsets
                            ? isStrictDescendantBitsetLocked(selectedNodeIndices[a], selectedNodeIndices[b])
                            : (selectedNodeIndices[b] < descendants.size() && descendants[selectedNodeIndices[b]]);
                        if (!reaches)
                        {
                            continue;
                        }
                        for (auto &dstPass : outLaunchGroup.fragments[b].passes)
                        {
                            for (const auto &wait : srcWaits)
                            {
                                if (eastl::find(dstPass.externalWaits.begin(), dstPass.externalWaits.end(), wait) ==
                                    dstPass.externalWaits.end())
                                {
                                    dstPass.externalWaits.push_back(wait);
                                }
                            }
                        }
                    }
                }
            }
        }

        const uint64_t globalId = mBudget->nextGlobalGpuGroupId++;
        activeGroup.globalId = globalId;
        mBudget->activeGpuGroups.push_back(SharedActiveGpuGroup{
            globalId,
            activeGroup.resources,
            activeGroup.hasOpaque,
        });
        budgetLock.unlock();

        mActiveGpuLaunchGroups.push_back(eastl::move(activeGroup));
        return true;
    }

    void TaskExecutionState::dispatchReady() noexcept
    {
        {
            std::lock_guard lock(mMutex);
            if (mDispatching || terminalLocked())
            {
                return;
            }
            mDispatching = true;
        }
        constexpr size_t kDispatchBatchLimit = 32;
        for (;;)
        {
            uint32_t indices[kDispatchBatchLimit];
            bool claimsGpuBuild[kDispatchBatchLimit];
            size_t count = 0;
            bool launchGroup = false;
            bool retryImmediately = false;
            PreparedGpuLaunchGroup preparedGroup;
            {
                std::lock_guard lock(mMutex);
                if (terminalLocked() || mStopStatus != ExecutionStatus::Pending)
                {
                    mDispatching = false;
                    return;
                }
                if (!mPendingGpuFragments.empty())
                {
                    const bool eagerReadbackOnly = (mBuildingGpuInFlight > 0 || mReadyGpuBuilds > 0);
                    launchGroup = tryCollectGpuLaunchGroupLocked(preparedGroup, eagerReadbackOnly);
                }
                const uint32_t maxInFlight = mRuntime->maximumInFlight();
                if (mReadyCursor < mReady.size() && mInFlight < maxInFlight)
                {
                    const size_t localBudget = maxInFlight - mInFlight;
                    const size_t available = mReady.size() - mReadyCursor;
                    const uint32_t requested = static_cast<uint32_t>(
                        eastl::min(kDispatchBatchLimit, eastl::min(localBudget, available)));
                    count = tryAcquireSharedInFlightSlots(requested, maxInFlight);
                    if (count > 0)
                    {
                        for (size_t i = 0; i < count; ++i)
                        {
                            const uint32_t idx = mReady[mReadyCursor++];
                            indices[i] = idx;
                            claimsGpuBuild[i] = false;
                            if (mTasks[idx].buildsSubgraph && !mGpuValuesReady[idx])
                            {
                                --mReadyGpuBuilds;
                                ++mBuildingGpuInFlight;
                                claimsGpuBuild[i] = true;
                            }
                        }
                        mInFlight += static_cast<uint32_t>(count);
                    }
                    else if (registerWaitingAndCheckInFlightAvailable(maxInFlight))
                    {
                        retryImmediately = true;
                    }
                }
                if (!launchGroup && count == 0 && !retryImmediately)
                {
                    mDispatching = false;
                    return;
                }
            }
            if (launchGroup)
            {
                beginGpu(eastl::move(preparedGroup));
            }
            if (count > 0)
            {
                ExecutorTask tasks[kDispatchBatchLimit];
                size_t built = 0;
                try
                {
                    auto self = shared_from_this();
                    for (; built < count; ++built)
                    {
                        const uint32_t idx = indices[built];
                        const bool claimedGpu = claimsGpuBuild[built];
                        tasks[built] = ExecutorTask([self, idx, claimedGpu]
                        {
                            self->evaluate(idx, claimedGpu);
                        });
                    }
                }
                catch (...)
                {
                    const auto failure = std::current_exception();
                    for (size_t i = built; i < count; ++i)
                    {
                        finish(indices[i], {}, failure, false, claimsGpuBuild[i]);
                    }
                    count = built;
                }
                if (count > 0)
                {
                    const size_t accepted = mRuntime->scheduleBatch(tasks, count);
                    for (size_t i = accepted; i < count; ++i)
                    {
                        scheduleFailed(indices[i], ScheduleResult::Unavailable, claimsGpuBuild[i]);
                    }
                }
            }
        }
    }

    void TaskExecutionState::evaluate(uint32_t index, bool claimedGpuBuild) noexcept
    {
        eastl::any value;
        std::exception_ptr failure;
        bool deferred = false;
        eastl::shared_ptr<TaskExecutionState> childToStart;
        try
        {
            bool proceed;
            eastl::function<eastl::any()> resolve;
            {
                std::lock_guard lock(mMutex);
                proceed = mStopStatus == ExecutionStatus::Pending;
                if (proceed && !claimedGpuBuild && mGpuValuesReady[index])
                {
                    resolve = eastl::move(mGpuResolvers[index]);
                }
            }
            if (proceed)
            {
                if (resolve)
                {
                    value = resolve();
                }
                else if (mTasks[index].buildsTaskSubgraph)
                {
                    auto buildSubgraph = eastl::move(mTasks[index].evaluateSubgraph);
                    TaskGraphOutput subOutput = buildSubgraph(mResults);
                    eastl::shared_ptr<TaskExecutionState> child;
                    {
                        std::lock_guard lock(mMutex);
                        if (mStopStatus == ExecutionStatus::Pending)
                        {
                            const uint64_t childId = mNextChildExecutionId++;
                            child = eastl::shared_ptr<TaskExecutionState>(
                                new TaskExecutionState(mRuntime, mBudget, shared_from_this(), index, childId));
                        }
                    }
                    if (child)
                    {
                        child->initializePlan(subOutput);
                        std::lock_guard lock(mMutex);
                        if (mStopStatus == ExecutionStatus::Pending)
                        {
                            mActiveChildExecutions.push_back(ActiveChildExecution{child->mChildId, index, child});
                            deferred = true;
                            childToStart = eastl::move(child);
                        }
                    }
                }
                else if (mTasks[index].buildsSubgraph)
                {
                    RenderNode node(mRuntime->gpuDevice(), mRuntime->maximumPasses(), &mBudget->passCount);
                    // Retain GPU builder captures until accepted passes complete; Args may reference literal input resources.
                    value = mTasks[index].evaluate(mResults, &node);
                    auto fragment = node.finish();
                    if (fragment.resolve && fragment.passes.empty())
                    {
                        throw std::logic_error("A GPU readback requires a producing pass.");
                    }
                    std::lock_guard lock(mMutex);
                    if (mStopStatus == ExecutionStatus::Pending)
                    {
                        deferred = bool(fragment.resolve);
                        if (deferred)
                        {
                            fragment.passes.back().completionNode = index;
                            mGpuResolvers[index] = eastl::move(fragment.resolve);
                        }
                        if (!fragment.allocations.empty())
                        {
                            mAllocations.insert(mAllocations.end(), fragment.allocations.begin(), fragment.allocations.end());
                            fragment.allocations.clear();
                        }
                        if (!fragment.passes.empty())
                        {
                            mPendingGpuFragments.push_back(PendingGpuFragment{index, eastl::move(fragment), deferred});
                        }
                    }
                }
                else
                {
                    auto operation = eastl::move(mTasks[index].evaluate);
                    value = operation(mResults, nullptr);
                }
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        finish(index, eastl::move(value), failure, deferred, claimedGpuBuild);
        if (childToStart)
        {
            childToStart->startChild();
        }
    }

    void TaskExecutionState::finish(
        uint32_t index,
        eastl::any value,
        std::exception_ptr failure,
        bool deferred,
        bool completedGpuBuild) noexcept
    {
        bool terminal;
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> failedGpus;
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> failedChildren;
        {
            std::lock_guard lock(mMutex);
            if (failure && !mFailure)
            {
                mFailure = failure;
                mStopStatus = ExecutionStatus::Failed;
            }
            if (mStopStatus != ExecutionStatus::Pending)
            {
                for (const auto &group : mActiveGpuLaunchGroups)
                {
                    if (group.execution)
                    {
                        failedGpus.push_back(group.execution);
                    }
                }
                for (const auto &child : mActiveChildExecutions)
                {
                    if (child.execution)
                    {
                        failedChildren.push_back(child.execution);
                    }
                }
            }
            --mInFlight;
            if (index != Preparation)
            {
                if (completedGpuBuild)
                {
                    --mBuildingGpuInFlight;
                }
                if (!deferred)
                {
                    --mRemaining;
                    if (mStopStatus == ExecutionStatus::Pending)
                    {
                        publishLocked(index, eastl::move(value));
                    }
                }
            }
            terminal = finishLocked();
        }
        releaseSharedInFlightSlotAndWake();
        for (const auto &gpu : failedGpus)
        {
            gpu->cancel();
        }
        if (!failedChildren.empty())
        {
            cancelSubgraphTrees(eastl::move(failedChildren));
        }
        if (terminal)
        {
            notifyTerminal();
        }
        else
        {
            dispatchReady();
        }
    }

    void TaskExecutionState::publishLocked(uint32_t index, eastl::any value)
    {
        mResults[index] = eastl::move(value);
        for (uint32_t successor : mSuccessors[index])
        {
            if (--mPending[successor] == 0)
            {
                if (mTasks[successor].buildsTaskSubgraph && hasUnfinishedGpuAncestorLocked(successor))
                {
                    mBlockedTaskSubgraphs.push_back(successor);
                }
                else
                {
                    mReady.push_back(successor);
                    if (mTasks[successor].buildsSubgraph && !mGpuValuesReady[successor])
                    {
                        ++mReadyGpuBuilds;
                    }
                }
            }
        }
    }

    void TaskExecutionState::beginGpu(PreparedGpuLaunchGroup launchGroup) noexcept
    {
        const uint64_t launchGroupId = launchGroup.id;
        try
        {
            auto gpu = eastl::make_shared<GpuGraphExecution>(*mRuntime, eastl::move(launchGroup.fragments),
                [self = shared_from_this()](uint32_t index) { self->gpuValueReady(index); },
                [self = shared_from_this(), launchGroupId](std::exception_ptr failure) { self->gpuFinished(launchGroupId, failure); });
            bool cancelled = false;
            {
                std::lock_guard lock(mMutex);
                for (auto &active : mActiveGpuLaunchGroups)
                {
                    if (active.id == launchGroupId)
                    {
                        active.execution = gpu;
                        break;
                    }
                }
                cancelled = mStopStatus != ExecutionStatus::Pending;
            }
            if (cancelled)
            {
                gpu->cancel();
            }
            gpu->start();
        }
        catch (...)
        {
            gpuFinished(launchGroupId, std::current_exception());
        }
    }

    void TaskExecutionState::gpuValueReady(uint32_t index) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            if (mStopStatus != ExecutionStatus::Pending || mGpuValuesReady[index])
            {
                return;
            }
            mGpuValuesReady[index] = true;
            mReady.push_back(index);
        }
        dispatchReady();
    }

    void TaskExecutionState::gpuFinished(uint64_t launchGroupId, std::exception_ptr failure) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            ++mInFlight;
        }
        try
        {
            ExecutorTask task([self = shared_from_this(), launchGroupId, failure] { self->finishGpu(launchGroupId, failure); });
            if (mRuntime->schedule(eastl::move(task)) == ScheduleResult::Accepted)
            {
                return;
            }
            throw std::runtime_error("Task executor rejected GPU completion continuation.");
        }
        catch (...)
        {
            finishGpu(launchGroupId, failure ? failure : std::current_exception());
        }
    }

    void TaskExecutionState::finishGpu(uint64_t launchGroupId, std::exception_ptr failure) noexcept
    {
        bool terminal;
        uint64_t finishedGlobalId = 0;
        eastl::shared_ptr<GpuGraphExecution> completedGpu;
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> failedGpus;
        eastl::vector<eastl::shared_ptr<TaskExecutionState>> failedChildren;
        {
            std::lock_guard lock(mMutex);
            if (failure && !mFailure)
            {
                mFailure = failure;
                mStopStatus = ExecutionStatus::Failed;
            }
            for (auto it = mActiveGpuLaunchGroups.begin(); it != mActiveGpuLaunchGroups.end(); ++it)
            {
                if (it->id == launchGroupId)
                {
                    for (uint32_t index : it->deferredNodes)
                    {
                        if (!mGpuValuesReady[index])
                        {
                            --mRemaining;
                            mGpuResolvers[index] = nullptr;
                            if (mStopStatus == ExecutionStatus::Pending)
                            {
                                mStopStatus = ExecutionStatus::Failed;
                                mDiagnostic = "GPU launch group completed without its observable result.";
                            }
                        }
                    }
                    finishedGlobalId = it->globalId;
                    completedGpu = eastl::move(it->execution);
                    mActiveGpuLaunchGroups.erase(it);
                    break;
                }
            }
            if (mStopStatus != ExecutionStatus::Pending)
            {
                for (const auto &group : mActiveGpuLaunchGroups)
                {
                    if (group.execution)
                    {
                        failedGpus.push_back(group.execution);
                    }
                }
                for (const auto &child : mActiveChildExecutions)
                {
                    if (child.execution)
                    {
                        failedChildren.push_back(child.execution);
                    }
                }
            }
            else if (!mBlockedTaskSubgraphs.empty())
            {
                size_t writePos = 0;
                for (size_t i = 0; i < mBlockedTaskSubgraphs.size(); ++i)
                {
                    const uint32_t nodeIdx = mBlockedTaskSubgraphs[i];
                    if (!hasUnfinishedGpuAncestorLocked(nodeIdx))
                    {
                        mReady.push_back(nodeIdx);
                    }
                    else
                    {
                        mBlockedTaskSubgraphs[writePos++] = nodeIdx;
                    }
                }
                mBlockedTaskSubgraphs.resize(writePos);
            }
            --mInFlight;
            terminal = finishLocked();
        }
        completedGpu.reset();
        releaseSharedGpuGroupAndWake(finishedGlobalId);
        for (const auto &gpu : failedGpus)
        {
            gpu->cancel();
        }
        if (!failedChildren.empty())
        {
            cancelSubgraphTrees(eastl::move(failedChildren));
        }
        if (terminal)
        {
            notifyTerminal();
        }
        else
        {
            dispatchReady();
        }
    }

    void TaskExecutionState::scheduleFailed(uint32_t index, ScheduleResult result, bool claimedGpuBuild) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            if (mStopStatus == ExecutionStatus::Pending)
            {
                mStopStatus = index == Preparation ? ExecutionStatus::Rejected : ExecutionStatus::Failed;
                mDiagnostic = result == ScheduleResult::Stopping ? "Task executor is stopping." : "Task executor could not accept ready work.";
            }
        }
        finish(index, {}, {}, false, claimedGpuBuild);
    }

    bool TaskExecutionState::finishLocked() noexcept
    {
        if (terminalLocked() || mInFlight != 0 || !mActiveGpuLaunchGroups.empty() || !mActiveChildExecutions.empty())
        {
            return false;
        }
        if (mStopStatus != ExecutionStatus::Pending)
        {
            mStatus = mStopStatus;
            return true;
        }
        if (mPrepared && mRemaining == 0 && mPendingGpuFragments.empty() && mBlockedTaskSubgraphs.empty())
        {
            try
            {
                mFinalResult = mResolveOutput ? mResolveOutput(mResults[mOutput]) : eastl::move(mResults[mOutput]);
                if (mOutputResources)
                {
                    mExportedResources = mOutputResources(mFinalResult);
                }
            }
            catch (...)
            {
                mFailure = std::current_exception();
                mStatus = ExecutionStatus::Failed;
                return true;
            }
            mStatus = ExecutionStatus::Succeeded;
            return true;
        }
        return false;
    }

    bool TaskExecutionState::prepareTerminalStep(ChildCompletionStep &outStep) noexcept
    {
        eastl::vector<TaskDefinition>().swap(mTasks);
        eastl::vector<eastl::vector<uint32_t>>().swap(mSuccessors);
        eastl::vector<uint64_t>().swap(mDescendantBits);
        mReachabilityWords = 0;
        eastl::vector<uint32_t>().swap(mPending);
        eastl::vector<uint32_t>().swap(mReady);
        eastl::vector<uint32_t>().swap(mBlockedTaskSubgraphs);
        TaskResults().swap(mResults);
        mResolveOutput = nullptr;
        mOutputResources = nullptr;
        eastl::vector<PendingGpuFragment>().swap(mPendingGpuFragments);
        eastl::vector<eastl::function<eastl::any()>>().swap(mGpuResolvers);
        eastl::vector<bool>().swap(mGpuValuesReady);
        eastl::vector<ActiveGpuLaunchGroup>().swap(mActiveGpuLaunchGroups);
        eastl::vector<ActiveChildExecution>().swap(mActiveChildExecutions);
        for (auto allocation = mAllocations.begin(); allocation != mAllocations.end();)
        {
            if (mStatus != ExecutionStatus::Succeeded ||
                eastl::find(mExportedResources.begin(), mExportedResources.end(), (*allocation)->resource()) == mExportedResources.end())
            {
                allocation = mAllocations.erase(allocation);
            }
            else
            {
                ++allocation;
            }
        }
        eastl::vector<const void *>().swap(mExportedResources);

        if (mParent)
        {
            outStep.parent = eastl::move(mParent);
            outStep.parentNodeIndex = mParentNodeIndex;
            outStep.childId = mChildId;
            outStep.exportedAllocations = eastl::move(mAllocations);
            outStep.finalResult = eastl::move(mFinalResult);
            {
                std::lock_guard lock(mMutex);
                outStep.status = mStatus;
                outStep.failure = mFailure;
                outStep.diagnostic = mDiagnostic;
                mTerminalPublished = true;
                mRuntime.reset();
            }
            return true;
        }

        eastl::shared_ptr<TaskRuntimeState> runtime;
        {
            std::lock_guard lock(mMutex);
            runtime = mRuntime;
        }
        runtime->beginCallbackDrain();
        if (mOwnsRuntimeAdmission)
        {
            runtime->complete();
        }
        {
            std::lock_guard lock(mMutex);
            mHasExportedResources = !mAllocations.empty();
            mTerminalPublished = true;
            mRuntime.reset();
        }
        runtime->notifyCondition(mCompleted);
        runtime->endCallbackDrain();
        return false;
    }

    void TaskExecutionState::notifyTerminal() noexcept
    {
        TaskExecutionState *current = this;
        eastl::shared_ptr<TaskExecutionState> keepCurrentAlive;
        for (;;)
        {
            ChildCompletionStep step;
            if (!current->prepareTerminalStep(step))
            {
                return;
            }
            auto parent = step.parent;
            bool parentTerminal = false;
            parent->applyChildCompletionStep(eastl::move(step), parentTerminal);
            if (!parentTerminal)
            {
                return;
            }
            keepCurrentAlive = eastl::move(parent);
            current = keepCurrentAlive.get();
        }
    }

    bool TaskExecutionState::terminalLocked() const noexcept
    {
        return mStatus != ExecutionStatus::Pending && mStatus != ExecutionStatus::Running;
    }
}
