#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <cstdint>
#include <limits>

namespace UGL
{
    /// Names a deferred graph result; values are supplied to dependent graph.add calls automatically.
    template <class T>
    class Task
    {
    public:
        using Value = T;
    };

    namespace Private
    {
        /// Resolves an ordinary graph argument to its owned value type during source checking.
        template <class T>
        struct TaskArgument
        {
            using Value = T;
        };

        /// Resolves a deferred graph input without adding a blocking conversion to the DSL.
        template <class T>
        struct TaskArgument<Task<T>>
        {
            using Value = T;
        };
    }

    /// Describes terminal and nonterminal execution states without exposing an executor or queue.
    enum class ExecutionStatus
    {
        Pending,
        Running,
        Succeeded,
        Failed,
        Cancelled,
        Rejected
    };

    /// Describes an asynchronously evaluated graph whose successful output is an owned value.
    template <class T>
    class Execution
    {
    public:
        /// Queries lifecycle state without waiting.
        ExecutionStatus status() const;
        /// Reports any terminal outcome, including failure and cancellation.
        bool ready() const;
        /// Returns bounded diagnostics without waiting.
        eastl::string error() const;
        /// Waits at a host boundary only; internal task dependencies use graph values.
        bool wait(uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max()) const;
        /// Requests cancellation while accepted operations retain their resources until drained.
        void cancel() const;
        /// Retrieves an owned successful result without implicitly waiting.
        T get() const;
    };

    /// Collects ordinary functions and class invocations into one graph without execution-domain flags.
    class TaskGraph
    {
    public:
        /// Captures values and infers dependencies from Task arguments; functions are static declarations.
        template <class Callable, class... Arguments>
        Task<eastl::invoke_result_t<Callable &, typename Private::TaskArgument<Arguments>::Value...>>
        add(const Callable &callable, Arguments... arguments);
    };

    /// Drives a graph entry independently of Renderer lifecycle; execution details remain in Core.
    class TaskRuntime
    {
    public:
        /// Creates a runtime using the default portable executor.
        TaskRuntime();
        /// Captures an entry and input snapshot, then asynchronously computes its deferred output.
        template <class Builder, class Input>
        Execution<typename eastl::invoke_result_t<Builder &, Input>::Value> submit(const Builder &builder, Input input);
    };
}
