#include "TaskRuntimeState.hpp"
#include "GraphDevice.hpp"
#include "PassWeightProfiler.hpp"

#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    TaskRuntimeState::TaskRuntimeState(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor, RHI::DeviceImpl *device)
        : mExecutor(executor), mDescriptor(descriptor), mMaximumInFlight(executor.concurrency())
    {
        if (mMaximumInFlight == 0 || descriptor.maximumActiveExecutions == 0 || descriptor.maximumTasksPerExecution == 0 ||
            descriptor.maximumPassesPerExecution == 0 || descriptor.maximumPassesPerBatch == 0)
        {
            throw std::invalid_argument("TaskRuntime requires a usable executor and nonzero admission limits.");
        }
        if (descriptor.maximumInFlightTasksPerExecution != 0)
        {
            mMaximumInFlight = eastl::min(mMaximumInFlight, descriptor.maximumInFlightTasksPerExecution);
        }
        if (device != nullptr)
        {
            mGpuDevice = GraphDevice::acquire(*device);
            if (descriptor.enablePassTimestampFeedback)
            {
                mPassWeightProfiler = eastl::make_unique<PassWeightProfiler>(mGpuDevice, descriptor.queueHandoffThresholdNs);
            }
        }
    }

    TaskRuntimeState::~TaskRuntimeState() = default;

    const eastl::shared_ptr<GraphDevice> &TaskRuntimeState::gpuDevice() const
    {
        return mGpuDevice;
    }

    PassWeightProfiler *TaskRuntimeState::passWeightProfiler() const noexcept
    {
        return mPassWeightProfiler.get();
    }
    uint32_t TaskRuntimeState::maximumPasses() const noexcept
    {
        return mDescriptor.maximumPassesPerExecution;
    }
    uint32_t TaskRuntimeState::maximumPassesPerBatch() const noexcept
    {
        return mDescriptor.maximumPassesPerBatch;
    }

    bool TaskRuntimeState::tryAdmit()
    {
        std::lock_guard lock(mMutex);
        if (mClosing || mActive >= mDescriptor.maximumActiveExecutions)
        {
            return false;
        }
        ++mActive;
        return true;
    }

    void TaskRuntimeState::complete() noexcept
    {
        std::lock_guard lock(mMutex);
        --mActive;
        if (mActive == 0u && mClosing)
        {
            mExecutor.notifyCondition(mDrained);
        }
    }

    void TaskRuntimeState::releaseDrainCounter(std::atomic<uint32_t> &counter) noexcept
    {
        uint32_t current = counter.load(std::memory_order_acquire);
        while (current > 1u)
        {
            if (counter.compare_exchange_weak(
                    current, current - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return;
            }
        }
        std::lock_guard lock(mMutex);
        if (counter.fetch_sub(1u, std::memory_order_seq_cst) == 1u && mClosing)
        {
            mExecutor.notifyCondition(mDrained);
        }
    }

    ScheduleResult TaskRuntimeState::schedule(ExecutorTask task) noexcept
    {
        mDispatching.fetch_add(1, std::memory_order_acq_rel);
        const auto result = mExecutor.trySchedule(std::move(task));
        releaseDrainCounter(mDispatching);
        return result;
    }

    size_t TaskRuntimeState::scheduleBatch(ExecutorTask *tasks, size_t count) noexcept
    {
        mDispatching.fetch_add(1, std::memory_order_acq_rel);
        const size_t accepted = mExecutor.scheduleBatch(tasks, count);
        releaseDrainCounter(mDispatching);
        return accepted;
    }

    void TaskRuntimeState::closeAndWait() noexcept
    {
        eastl::unique_ptr<PassWeightProfiler> profilerToRelease;
        eastl::shared_ptr<GraphDevice> deviceToRelease;
        {
            std::unique_lock lock(mMutex);
            mClosing = true;
            mClosingAtomic.store(true, std::memory_order_seq_cst);
            const auto drained = [](const void *ctx) noexcept -> bool
            {
                const auto *self = static_cast<const TaskRuntimeState *>(ctx);
                return self->mActive == 0 &&
                    self->mDispatching.load(std::memory_order_seq_cst) == 0 &&
                    self->mWaiting.load(std::memory_order_seq_cst) == 0;
            };
            (void)mExecutor.waitCondition(lock, mDrained, std::numeric_limits<uint64_t>::max(), drained, this);
            profilerToRelease = eastl::move(mPassWeightProfiler);
            deviceToRelease = eastl::move(mGpuDevice);
        }
        profilerToRelease.reset();
        deviceToRelease.reset();
    }

    uint32_t TaskRuntimeState::maximumTasks() const noexcept
    {
        return mDescriptor.maximumTasksPerExecution;
    }

    uint32_t TaskRuntimeState::maximumInFlight() const noexcept
    {
        return mMaximumInFlight;
    }

    bool TaskRuntimeState::waitCondition(
        std::unique_lock<std::mutex> &lock,
        std::condition_variable &condition,
        uint64_t timeoutNanoseconds,
        ITaskExecutor::WaitPredicate predicate,
        const void *context) noexcept
    {
        if (predicate(context))
        {
            return true;
        }
        mWaiting.fetch_add(1, std::memory_order_acq_rel);
        const bool satisfied = mExecutor.waitCondition(lock, condition, timeoutNanoseconds, predicate, context);
        releaseDrainCounter(mWaiting);
        return satisfied;
    }

    void TaskRuntimeState::notifyCondition(std::condition_variable &condition) noexcept
    {
        mExecutor.notifyCondition(condition);
    }

    void TaskRuntimeState::beginCallbackDrain() noexcept
    {
        mDispatching.fetch_add(1, std::memory_order_acq_rel);
    }

    void TaskRuntimeState::endCallbackDrain() noexcept
    {
        releaseDrainCounter(mDispatching);
    }
}
