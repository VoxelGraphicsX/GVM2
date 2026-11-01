#pragma once

#include <EASTL/functional.h>

namespace GVM::Core
{
    /// Owns one ready invocation; its callback must contain exception and terminal accounting.
    class ExecutorTask final
    {
    public:
        /// Creates an empty task for queue extraction without allocating a callback.
        ExecutorTask() = default;
        /// Takes ownership of a ready callback; the executor must never invoke it inline.
        explicit ExecutorTask(eastl::function<void()> callback);
        /// Transfers the single right to execute the callback.
        ExecutorTask(ExecutorTask &&other) noexcept;
        /// Replaces an unexecuted callback by transferring ownership from another task.
        ExecutorTask &operator=(ExecutorTask &&other) noexcept;
        /// Releases captures after execution or rejection without invoking the callback.
        ~ExecutorTask();
        ExecutorTask(const ExecutorTask &) = delete;
        ExecutorTask &operator=(const ExecutorTask &) = delete;

        /// Returns whether this task still owns an invocation.
        explicit operator bool() const noexcept;
        /// Consumes the invocation exactly once; escaping exceptions violate the executor contract.
        void execute() noexcept;

    private:
        eastl::function<void()> mCallback;
    };
}
