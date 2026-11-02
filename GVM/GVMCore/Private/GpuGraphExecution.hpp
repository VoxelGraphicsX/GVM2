#pragma once
#include "GraphPass.hpp"
#include "GraphDevice.hpp"
#include "PassSchedule.hpp"
#include <EASTL/atomic.h>
#include <EASTL/optional.h>
#include <exception>
#include <mutex>

namespace GVM::Core::Private
{
    class TaskRuntimeState;
    /// Records one expanded pass wave on the injected executor and submits bounded ordered batches.
    class GpuGraphExecution final : public eastl::enable_shared_from_this<GpuGraphExecution>
    {
    public:
        /// Takes ownership of immutable pass captures before any recording callback is scheduled.
        GpuGraphExecution(TaskRuntimeState &runtime, eastl::vector<RenderNodeFragment> fragments,
            eastl::function<void(uint32_t)> progress,
            eastl::function<void(std::exception_ptr)> completion);
        /// Starts a bounded number of recording lanes on the runtime's existing executor.
        void start() noexcept;
        /// Stops unstarted recordings and submissions while retaining already accepted GPU work.
        void cancel() noexcept;
        /// Advances submission/completion from the runtime monitor only; returns true after notifying its owner.
        bool advance() noexcept;
    private:
        /// Records independent closed command encoders without waiting on GPU dependencies.
        void record() noexcept;
        /// Stores the first failure and prevents additional command recording or submission.
        void fail(std::exception_ptr failure) noexcept;
        TaskRuntimeState &mRuntime;
        eastl::shared_ptr<GraphDevice> mDevice;
        eastl::vector<GraphPass> mPasses;
        eastl::vector<RHI::CommandEncoder> mEncoders;
        eastl::vector<bool> mRecorded;
        eastl::vector<PassBatch> mBatches;
        eastl::vector<eastl::optional<RHI::Submission>> mSubmissions;
        eastl::vector<bool> mCompleted;
        eastl::function<void(uint32_t)> mProgress;
        eastl::function<void(std::exception_ptr)> mCompletion;
        std::mutex mMutex;
        std::exception_ptr mFailure;
        eastl::atomic<bool> mStopping{false};
        uint32_t mRecording = 1;
        uint32_t mNextPass = 0;
        uint32_t mNextBatch = 0;
    };
}
