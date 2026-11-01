#include "ExecutorTask.hpp"

#include <EASTL/utility.h>

namespace GVM::Core
{
    ExecutorTask::ExecutorTask(eastl::function<void()> callback)
        : mCallback(eastl::move(callback))
    {
    }

    ExecutorTask::ExecutorTask(ExecutorTask &&other) noexcept = default;
    ExecutorTask &ExecutorTask::operator=(ExecutorTask &&other) noexcept = default;
    ExecutorTask::~ExecutorTask() = default;

    ExecutorTask::operator bool() const noexcept
    {
        return static_cast<bool>(mCallback);
    }

    void ExecutorTask::execute() noexcept
    {
        auto callback = eastl::move(mCallback);
        mCallback = nullptr;
        if (callback)
        {
            callback();
        }
    }
}
