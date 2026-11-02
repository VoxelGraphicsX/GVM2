#include "GpuGraphExecution.hpp"
#include "TaskRuntimeState.hpp"
#include "PassSchedule.hpp"
#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    GpuGraphExecution::GpuGraphExecution(TaskRuntimeState &runtime, eastl::vector<RenderNodeFragment> fragments,
        eastl::function<void(uint32_t)> progress, eastl::function<void(std::exception_ptr)> completion)
        : mRuntime(runtime), mDevice(runtime.gpuDevice()), mProgress(eastl::move(progress)), mCompletion(eastl::move(completion))
    {
        for (auto &fragment : fragments)
        {
            for (auto &pass : fragment.passes)
            {
                mPasses.push_back(eastl::move(pass));
            }
        }
        if (mPasses.size() > runtime.maximumPasses())
        {
            throw std::length_error("GPU graph exceeds the runtime pass budget.");
        }
        const PassSchedule schedule(mPasses);
        mBatches = schedule.batches(mPasses, runtime.maximumPassesPerBatch());
        eastl::vector<GraphPass> ordered;
        ordered.reserve(mPasses.size());
        for (uint32_t index : schedule.order())
        {
            ordered.push_back(eastl::move(mPasses[index]));
        }
        mPasses = eastl::move(ordered);
        mEncoders.resize(mPasses.size());
        mRecorded.resize(mPasses.size(), false);
        mSubmissions.resize(mBatches.size());
        mCompleted.resize(mBatches.size(), false);
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
            mRecording = lanes;
        }
        for (uint32_t index = 0; index < lanes; ++index)
        {
            try
            {
                ExecutorTask task([self = shared_from_this()] { self->record(); });
                if (mRuntime.schedule(eastl::move(task)) == ScheduleResult::Accepted)
                {
                    continue;
                }
                throw std::runtime_error("Task executor rejected GPU pass recording.");
            }
            catch (...)
            {
                fail(std::current_exception());
                std::lock_guard lock(mMutex);
                --mRecording;
            }
        }
    }
    void GpuGraphExecution::cancel() noexcept
    {
        mStopping.store(true, eastl::memory_order_release);
    }
    void GpuGraphExecution::fail(std::exception_ptr failure) noexcept
    {
        std::lock_guard lock(mMutex);
        if (!mFailure)
        {
            mFailure = failure;
        }
        cancel();
    }
    void GpuGraphExecution::record() noexcept
    {
        try
        {
            for (;;)
            {
                uint32_t index;
                {
                    std::lock_guard lock(mMutex);
                    if (mStopping.load(eastl::memory_order_acquire) || mNextPass == mPasses.size())
                    {
                        break;
                    }
                    index = mNextPass++;
                }
                auto encoder = mDevice->queue()->createCommandEncoder();
                mPasses[index].record(encoder);
                encoder->end();
                {
                    std::lock_guard lock(mMutex);
                    mEncoders[index] = eastl::move(encoder);
                    mRecorded[index] = true;
                }
            }
        }
        catch (...)
        {
            fail(std::current_exception());
        }
        std::lock_guard lock(mMutex);
        --mRecording;
    }
    bool GpuGraphExecution::advance() noexcept
    {
        try
        {
            while (mNextBatch < mBatches.size() && !mStopping.load(eastl::memory_order_acquire))
            {
                const uint32_t begin = mNextBatch == 0 ? 0 : mBatches[mNextBatch - 1].end;
                const auto &batch = mBatches[mNextBatch];
                eastl::vector<RHI::CommandEncoder> encoders;
                {
                    std::lock_guard lock(mMutex);
                    bool ready = true;
                    for (uint32_t index = begin; index < batch.end; ++index)
                    {
                        ready = ready && mRecorded[index];
                    }
                    if (!ready)
                    {
                        break;
                    }
                    encoders.assign(mEncoders.begin() + begin, mEncoders.begin() + batch.end);
                }
                eastl::vector<RHI::Submission> waits;
                for (uint32_t predecessor : batch.waits)
                {
                    waits.push_back(*mSubmissions[predecessor]);
                }
                auto lock = mDevice->lock();
                mSubmissions[mNextBatch] = mDevice->queue()->submit(encoders, waits);
                ++mNextBatch;
            }
        }
        catch (...)
        {
            fail(std::current_exception());
        }
        bool drained = true;
        for (uint32_t batch = 0; batch < mNextBatch; ++batch)
        {
            if (mCompleted[batch])
            {
                continue;
            }
            try
            {
                if (!mSubmissions[batch]->isComplete())
                {
                    drained = false;
                    continue;
                }
                mCompleted[batch] = true;
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
                mCompleted[batch] = true;
                fail(std::current_exception());
            }
        }
        std::exception_ptr failure;
        {
            std::lock_guard lock(mMutex);
            if (mRecording != 0 || !drained ||
                (!mStopping.load(eastl::memory_order_acquire) && mNextBatch != mBatches.size()))
            {
                return false;
            }
            failure = mFailure;
        }
        auto completion = eastl::move(mCompletion);
        completion(failure);
        return true;
    }
}
