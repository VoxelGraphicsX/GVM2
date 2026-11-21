#include "ITaskExecutor.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace GVM::Core
{
    ITaskExecutor::~ITaskExecutor() = default;

    size_t ITaskExecutor::scheduleBatch(ExecutorTask *tasks, size_t count) noexcept
    {
        size_t accepted = 0;
        for (; accepted < count; ++accepted)
        {
            if (trySchedule(std::move(tasks[accepted])) != ScheduleResult::Accepted)
            {
                break;
            }
        }
        return accepted;
    }

    bool ITaskExecutor::waitCondition(
        std::unique_lock<std::mutex> &lock,
        std::condition_variable &condition,
        uint64_t timeoutNanoseconds,
        WaitPredicate predicate,
        const void *context) const noexcept
    {
        if (predicate(context))
        {
            return true;
        }
        if (timeoutNanoseconds == 0)
        {
            return false;
        }
        if (timeoutNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            while (!predicate(context))
            {
                condition.wait(lock);
            }
            return true;
        }
        const auto bounded = std::min(timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        return condition.wait_for(lock, std::chrono::nanoseconds(bounded), [predicate, context]
        {
            return predicate(context);
        });
    }

    void ITaskExecutor::notifyCondition(std::condition_variable &condition) const noexcept
    {
        condition.notify_all();
    }
}
