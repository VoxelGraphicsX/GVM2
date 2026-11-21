#include "ExecutorTask.hpp"

namespace GVM::Core
{
    ExecutorTask::ExecutorTask() noexcept = default;
    ExecutorTask::ExecutorTask(std::nullptr_t) noexcept {}

    ExecutorTask::ExecutorTask(ExecutorTask &&other) noexcept
        : mOps(other.mOps)
    {
        if (mOps != nullptr)
        {
            mOps->moveConstruct(mStorage, other.mStorage);
            other.mOps = nullptr;
        }
    }

    ExecutorTask &ExecutorTask::operator=(ExecutorTask &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        if (mOps != nullptr)
        {
            mOps->destroy(mStorage);
            mOps = nullptr;
        }
        if (other.mOps != nullptr)
        {
            other.mOps->moveConstruct(mStorage, other.mStorage);
            mOps = other.mOps;
            other.mOps = nullptr;
        }
        return *this;
    }

    ExecutorTask::~ExecutorTask()
    {
        if (mOps != nullptr)
        {
            mOps->destroy(mStorage);
            mOps = nullptr;
        }
    }

    ExecutorTask::operator bool() const noexcept
    {
        return mOps != nullptr;
    }

    void ExecutorTask::execute() noexcept
    {
        if (mOps == nullptr)
        {
            return;
        }
        ExecutorTask local(std::move(*this));
        const Ops *ops = local.mOps;
        local.mOps = nullptr;
        struct Guard
        {
            const Ops *ops;
            void *storage;
            ~Guard()
            {
                if (ops != nullptr)
                {
                    ops->destroy(storage);
                }
            }
        } guard{ops, local.mStorage};
        ops->invoke(local.mStorage);
    }
}
