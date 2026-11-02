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
        : mRuntime(eastl::move(runtime))
    {
    }

    void TaskExecutionState::start(eastl::function<TaskGraphOutput()> builder)
    {
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
        if (timeoutNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            while (!mTerminalPublished)
            {
                mCompleted.wait(lock);
            }
            return true;
        }
        const auto bounded = eastl::min(timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        return mCompleted.wait_for(lock, std::chrono::nanoseconds(bounded), [this]
        {
            return mTerminalPublished;
        });
    }

    void TaskExecutionState::cancel()
    {
        bool terminal = false;
        eastl::shared_ptr<GpuGraphExecution> gpu;
        {
            std::lock_guard lock(mMutex);
            if (terminalLocked())
            {
                return;
            }
            if (mStopStatus == ExecutionStatus::Pending)
            {
                mStopStatus = ExecutionStatus::Cancelled;
                mDiagnostic = "Task execution was cancelled.";
            }
            gpu = mGpuExecution;
            terminal = finishLocked();
        }
        if (gpu)
        {
            gpu->cancel();
        }
        if (terminal)
        {
            notifyTerminal();
        }
    }

    eastl::any TaskExecutionState::result() const
    {
        std::unique_lock lock(mMutex);
        if (!mTerminalPublished)
        {
            throw std::logic_error("Execution::get requires a terminal successful execution.");
        }
        if (mFailure)
        {
            const auto failure = mFailure;
            lock.unlock();
            std::rethrow_exception(failure);
        }
        if (mStatus != ExecutionStatus::Succeeded)
        {
            throw std::runtime_error(mDiagnostic);
        }
        auto result = mFinalResult;
        for (const auto &allocation : mAllocations)
        {
            allocation->releaseOwnership();
        }
        return result;
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
                if (!output.definition)
                {
                    throw std::invalid_argument("Graph entry returned an unbound Task.");
                }
                auto tasks = output.definition->snapshot(mRuntime->maximumTasks());
                if (output.index >= tasks.size())
                {
                    throw std::invalid_argument("Graph entry returned an invalid producer index.");
                }
                eastl::vector<eastl::vector<uint32_t>> successors(tasks.size());
                eastl::vector<uint32_t> pending(tasks.size());
                eastl::vector<uint32_t> ready;
                // A readback enters the ready list once for expansion and once for CPU resolution.
                ready.reserve(tasks.size() * 2);
                TaskResults results(tasks.size());
                eastl::vector<RenderNodeFragment> fragments(tasks.size());
                eastl::vector<eastl::function<eastl::any()>> resolvers(tasks.size());
                eastl::vector<bool> gpuValuesReady(tasks.size(), false);
                eastl::vector<uint32_t> deferred;
                deferred.reserve(tasks.size());
                for (uint32_t index = 0; index < tasks.size(); ++index)
                {
                    pending[index] = static_cast<uint32_t>(tasks[index].predecessors.size());
                    if (pending[index] == 0)
                    {
                        ready.push_back(index);
                    }
                    for (uint32_t predecessor : tasks[index].predecessors)
                    {
                        successors[predecessor].push_back(index);
                    }
                }
                {
                    std::lock_guard lock(mMutex);
                    mTasks = eastl::move(tasks);
                    mSuccessors = eastl::move(successors);
                    mPending = eastl::move(pending);
                    mReady = eastl::move(ready);
                    mResults = eastl::move(results);
                    mFragments = eastl::move(fragments);
                    mGpuResolvers = eastl::move(resolvers);
                    mGpuValuesReady = eastl::move(gpuValuesReady);
                    mDeferredGpuNodes = eastl::move(deferred);
                    mOutput = output.index;
                    mResolveOutput = output.resolve;
                    mOutputResources = output.resources;
                    mRemaining = static_cast<uint32_t>(mTasks.size());
                    mPrepared = true;
                }
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        builder = nullptr;
        finish(Preparation, {}, failure);
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
        for (;;)
        {
            uint32_t index = Preparation;
            bool beginGpuWave = false;
            {
                std::lock_guard lock(mMutex);
                if (terminalLocked() || mStopStatus != ExecutionStatus::Pending || mReadyCursor == mReady.size() || mInFlight >= mRuntime->maximumInFlight())
                {
                    mDispatching = false;
                    if (!terminalLocked() && mStopStatus == ExecutionStatus::Pending &&
                        mInFlight == 0 && mGpuPending && !mGpuActive)
                    {
                        mGpuActive = true;
                        mGpuPending = false;
                        beginGpuWave = true;
                    }
                    else
                    {
                        return;
                    }
                }
                else
                {
                    index = mReady[mReadyCursor++];
                    ++mInFlight;
                }
            }
            if (beginGpuWave)
            {
                beginGpu();
                return;
            }
            try
            {
                ExecutorTask task([self = shared_from_this(), index]
                {
                    self->evaluate(index);
                });
                const auto accepted = mRuntime->schedule(eastl::move(task));
                if (accepted != ScheduleResult::Accepted)
                {
                    scheduleFailed(index, accepted);
                }
            }
            catch (...)
            {
                finish(index, {}, std::current_exception());
            }
        }
    }

    void TaskExecutionState::evaluate(uint32_t index) noexcept
    {
        eastl::any value;
        std::exception_ptr failure;
        bool deferred = false;
        try
        {
            bool proceed;
            eastl::function<eastl::any()> resolve;
            {
                std::lock_guard lock(mMutex);
                proceed = mStopStatus == ExecutionStatus::Pending;
                if (proceed && mGpuValuesReady[index])
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
                else if (mTasks[index].buildsSubgraph)
                {
                    RenderNode node(mRuntime->gpuDevice(), mRuntime->maximumPasses());
                    // Retain GPU builder captures until accepted passes complete; Args may reference literal input resources.
                    value = mTasks[index].evaluate(mResults, &node);
                    auto fragment = node.finish();
                    std::lock_guard lock(mMutex);
                    if (fragment.passes.size() > mRuntime->maximumPasses() - mPassCount)
                    {
                        throw std::length_error("GPU graph exceeds the execution pass budget.");
                    }
                    mPassCount += static_cast<uint32_t>(fragment.passes.size());
                    deferred = bool(fragment.resolve);
                    if (deferred)
                    {
                        if (fragment.passes.empty())
                        {
                            throw std::logic_error("A GPU readback requires a producing pass.");
                        }
                        fragment.passes.back().completionNode = index;
                        mGpuResolvers[index] = eastl::move(fragment.resolve);
                        mDeferredGpuNodes.push_back(index);
                    }
                    mAllocations.insert(mAllocations.end(), fragment.allocations.begin(), fragment.allocations.end());
                    fragment.allocations.clear();
                    mGpuPending = mGpuPending || !fragment.passes.empty();
                    mFragments[index] = eastl::move(fragment);
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
        finish(index, eastl::move(value), failure, deferred);
    }

    void TaskExecutionState::finish(uint32_t index, eastl::any value, std::exception_ptr failure, bool deferred) noexcept
    {
        bool terminal;
        eastl::shared_ptr<GpuGraphExecution> failedGpu;
        {
            std::lock_guard lock(mMutex);
            if (failure && !mFailure)
            {
                mFailure = failure;
                mStopStatus = ExecutionStatus::Failed;
                failedGpu = mGpuExecution;
            }
            --mInFlight;
            if (index != Preparation && !deferred)
            {
                --mRemaining;
                if (mStopStatus == ExecutionStatus::Pending)
                {
                    publishLocked(index, eastl::move(value));
                }
            }
            terminal = finishLocked();
        }
        if (failedGpu)
        {
            failedGpu->cancel();
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
                mReady.push_back(successor);
            }
        }
    }

    void TaskExecutionState::beginGpu() noexcept
    {
        try
        {
            eastl::vector<RenderNodeFragment> fragments(mTasks.size());
            {
                std::lock_guard lock(mMutex);
                fragments.swap(mFragments);
                mActiveGpuNodes.swap(mDeferredGpuNodes);
            }
            auto gpu = eastl::make_shared<GpuGraphExecution>(*mRuntime, eastl::move(fragments),
                [self = shared_from_this()](uint32_t index) { self->gpuValueReady(index); },
                [self = shared_from_this()](std::exception_ptr failure) { self->gpuFinished(failure); });
            {
                std::lock_guard lock(mMutex);
                mGpuExecution = gpu;
                if (mStopStatus != ExecutionStatus::Pending)
                {
                    gpu->cancel();
                }
            }
            mRuntime->enqueueGpu(gpu);
            gpu->start();
        }
        catch (...)
        {
            gpuFinished(std::current_exception());
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

    void TaskExecutionState::gpuFinished(std::exception_ptr failure) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            ++mInFlight;
        }
        try
        {
            ExecutorTask task([self = shared_from_this(), failure] { self->finishGpu(failure); });
            if (mRuntime->schedule(eastl::move(task)) == ScheduleResult::Accepted)
            {
                return;
            }
            throw std::runtime_error("Task executor rejected GPU completion continuation.");
        }
        catch (...)
        {
            finishGpu(failure ? failure : std::current_exception());
        }
    }

    void TaskExecutionState::finishGpu(std::exception_ptr failure) noexcept
    {
        bool terminal;
        eastl::shared_ptr<GpuGraphExecution> completedGpu;
        {
            std::lock_guard lock(mMutex);
            if (failure && !mFailure)
            {
                mFailure = failure;
                mStopStatus = ExecutionStatus::Failed;
            }
            for (uint32_t index : mActiveGpuNodes)
            {
                if (!mGpuValuesReady[index])
                {
                    --mRemaining;
                    mGpuResolvers[index] = nullptr;
                    if (mStopStatus == ExecutionStatus::Pending)
                    {
                        mStopStatus = ExecutionStatus::Failed;
                        mDiagnostic = "GPU wave completed without its observable result.";
                    }
                }
            }
            mActiveGpuNodes.clear();
            mGpuActive = false;
            --mInFlight;
            completedGpu = eastl::move(mGpuExecution);
            terminal = finishLocked();
        }
        completedGpu.reset();
        if (terminal)
        {
            notifyTerminal();
        }
        else
        {
            dispatchReady();
        }
    }

    void TaskExecutionState::scheduleFailed(uint32_t index, ScheduleResult result) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            if (mStopStatus == ExecutionStatus::Pending)
            {
                mStopStatus = index == Preparation ? ExecutionStatus::Rejected : ExecutionStatus::Failed;
                mDiagnostic = result == ScheduleResult::Stopping ? "Task executor is stopping." : "Task executor could not accept ready work.";
            }
        }
        finish(index, {}, {});
    }

    bool TaskExecutionState::finishLocked() noexcept
    {
        if (terminalLocked() || mInFlight != 0 || mGpuActive)
        {
            return false;
        }
        if (mStopStatus != ExecutionStatus::Pending)
        {
            mStatus = mStopStatus;
            return true;
        }
        if (mPrepared && mRemaining == 0 && !mGpuPending)
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

    void TaskExecutionState::notifyTerminal() noexcept
    {
        eastl::vector<TaskDefinition>().swap(mTasks);
        eastl::vector<eastl::vector<uint32_t>>().swap(mSuccessors);
        eastl::vector<uint32_t>().swap(mPending);
        eastl::vector<uint32_t>().swap(mReady);
        TaskResults().swap(mResults);
        mResolveOutput = nullptr;
        mOutputResources = nullptr;
        eastl::vector<RenderNodeFragment>().swap(mFragments);
        eastl::vector<eastl::function<eastl::any()>>().swap(mGpuResolvers);
        eastl::vector<bool>().swap(mGpuValuesReady);
        eastl::vector<uint32_t>().swap(mDeferredGpuNodes);
        eastl::vector<uint32_t>().swap(mActiveGpuNodes);
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
        mRuntime->complete();
        {
            std::lock_guard lock(mMutex);
            mTerminalPublished = true;
        }
        mCompleted.notify_all();
    }

    bool TaskExecutionState::terminalLocked() const noexcept
    {
        return mStatus != ExecutionStatus::Pending && mStatus != ExecutionStatus::Running;
    }
}
