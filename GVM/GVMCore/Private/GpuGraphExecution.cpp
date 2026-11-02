#include "GpuGraphExecution.hpp"
#include "TaskRuntimeState.hpp"
#include "PassSchedule.hpp"
#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    GpuGraphExecution::GpuGraphExecution(TaskRuntimeState &runtime, eastl::vector<RenderNodeFragment> fragments,
        eastl::function<void(std::exception_ptr)> completion)
        : mRuntime(runtime), mDevice(runtime.gpuDevice()), mCompletion(eastl::move(completion))
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
        mBatchEnds = schedule.batchEnds(mPasses, runtime.maximumPassesPerBatch());
        mEncoders.resize(mPasses.size());
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
                mEncoders[index] = eastl::move(encoder);
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
        {
            std::lock_guard lock(mMutex);
            if (mRecording != 0)
            {
                return false;
            }
        }
        try
        {
            if (!mSubmitted)
            {
                mSubmitted = true;
                uint32_t begin = 0;
                for (uint32_t end : mBatchEnds)
                {
                    if (mStopping.load(eastl::memory_order_acquire))
                    {
                        break;
                    }
                    eastl::vector<RHI::CommandEncoder> batch(mEncoders.begin() + begin, mEncoders.begin() + end);
                    eastl::vector<RHI::Submission> waits;
                    if (mLastSubmission)
                    {
                        waits.push_back(*mLastSubmission);
                    }
                    auto lock = mDevice->lock();
                    mLastSubmission = mDevice->queue()->submit(batch, waits);
                    begin = end;
                }
            }
            if (mLastSubmission && !mLastSubmission->isComplete())
            {
                return false;
            }
        }
        catch (...)
        {
            fail(std::current_exception());
            // A previously accepted batch still owns its resources until completion or device loss.
            try
            {
                if (mLastSubmission && !mLastSubmission->isComplete())
                {
                    return false;
                }
            }
            catch (...)
            {
            }
        }
        auto completion = eastl::move(mCompletion);
        completion(mFailure);
        return true;
    }
}
