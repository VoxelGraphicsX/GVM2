#include "GpuGraphService.hpp"
#include <EASTL/algorithm.h>
#include <chrono>
#include <stdexcept>

namespace GVM::Core::Private
{
    GpuGraphService::GpuGraphService() : mThread(&GpuGraphService::progress, this)
    {
    }
    GpuGraphService::~GpuGraphService()
    {
        close();
    }
    void GpuGraphService::enqueue(eastl::shared_ptr<GpuGraphExecution> execution)
    {
        {
            std::lock_guard lock(mMutex);
            if (mClosing)
            {
                throw std::logic_error("GPU graph monitor is closed.");
            }
            mExecutions.push_back(eastl::move(execution));
        }
        mChanged.notify_one();
    }
    void GpuGraphService::close()
    {
        {
            std::lock_guard lock(mMutex);
            mClosing = true;
        }
        mChanged.notify_all();
        if (mThread.joinable())
        {
            mThread.join();
        }
    }
    void GpuGraphService::progress() noexcept
    {
        for (;;)
        {
            eastl::shared_ptr<GpuGraphExecution> execution;
            size_t count;
            {
                std::unique_lock lock(mMutex);
                if (mClosing && mExecutions.empty())
                {
                    return;
                }
                if (mExecutions.empty())
                {
                    mChanged.wait(lock);
                    continue;
                }
                count = mExecutions.size();
            }
            for (size_t index = 0; index < count;)
            {
                {
                    std::lock_guard lock(mMutex);
                    execution = mExecutions[index];
                }
                if (execution->advance())
                {
                    std::lock_guard lock(mMutex);
                    mExecutions.erase(mExecutions.begin() + index);
                    --count;
                }
                else
                {
                    ++index;
                }
                execution.reset();
            }
            std::unique_lock lock(mMutex);
            mChanged.wait_for(lock, std::chrono::milliseconds(1));
        }
    }
}
