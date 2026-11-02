#pragma once
#include "GpuGraphExecution.hpp"
#include <condition_variable>
#include <thread>

namespace GVM::Core::Private
{
    /// Monitors GPU completion and submits recorded batches without occupying executor workers.
    /// This thread performs no user CPU task evaluation and owns no secondary task pool.
    class GpuGraphService final
    {
    public:
        /// Starts one bounded runtime monitor before graph admission opens.
        GpuGraphService();
        /// Joins the monitor after accepted graph work has drained; call from a host thread.
        ~GpuGraphService();
        /// Retains one admitted execution wave before its recording jobs are dispatched.
        void enqueue(eastl::shared_ptr<GpuGraphExecution> execution);
        /// Stops the monitor after draining its retained submissions; safe to call repeatedly.
        void close();
    private:
        /// Polls retained GPU work and delivers nonblocking completion notifications.
        void progress() noexcept;
        std::mutex mMutex;
        std::condition_variable mChanged;
        eastl::vector<eastl::shared_ptr<GpuGraphExecution>> mExecutions;
        bool mClosing = false;
        std::thread mThread;
    };
}
