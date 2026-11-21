#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/functional.h>
#include <EASTL/any.h>
#include <EASTL/type_traits.h>
#include <cstdint>

namespace GVM::Core
{
    template <class T> class Task;
    namespace Private
    {
        class TaskGraphDefinition;
        struct TaskGraphOutput;
        template <class U>
        struct IsTask : eastl::false_type
        {
        };
        template <class U>
        struct IsTask<Task<U>> : eastl::true_type
        {
        };
        /// Packages a Task<Value> handle into an execution-ready TaskGraphOutput descriptor.
        template <class Value>
        TaskGraphOutput makeTaskGraphOutput(Task<Value> &&output);
        /// Lowers a compiler-checked field projection without allocating a task or waiting for its producer.
        template <class Record, class Projection>
        auto projectTask(const Task<Record> &source, Projection projection);
    }
    class TaskGraph;
    class TaskRuntime;

    /// Names a deferred value and owns its graph definition; it never waits or exposes result storage.
    template <class T>
    class Task final
    {
    public:
        using Value = T;
        /// Creates an unbound output slot; using it as an input or execution output is an error.
        Task() = default;

    private:
        friend class TaskGraph;
        friend class TaskRuntime;
        template <class Value>
        friend Private::TaskGraphOutput Private::makeTaskGraphOutput(Task<Value> &&output);
        template <class Record, class Projection>
        friend auto Private::projectTask(const Task<Record> &source, Projection projection);
        /// Binds a typed output to its producer without retaining an execution or a caller stack.
        Task(eastl::shared_ptr<Private::TaskGraphDefinition> definition, uint32_t index)
            : mDefinition(eastl::move(definition)), mIndex(index)
        {
        }

        eastl::shared_ptr<Private::TaskGraphDefinition> mDefinition;
        uint32_t mIndex = 0;
        eastl::function<T(const eastl::any &)> mResolve = [](const eastl::any &value) -> T
        {
            if constexpr (!eastl::is_void_v<T>)
            {
                return eastl::any_cast<const T &>(value);
            }
        };
    };

    namespace Private
    {
        /// Composes a typed field accessor with its producer's immutable result resolver.
        template <class Record, class Projection>
        auto projectTask(const Task<Record> &source, Projection projection)
        {
            using Field = eastl::decay_t<eastl::invoke_result_t<Projection, const Record &>>;
            Task<Field> result(source.mDefinition, source.mIndex);
            result.mResolve = [resolve = source.mResolve, projection = eastl::move(projection)](const eastl::any &value)
            {
                return projection(resolve(value));
            };
            return result;
        }
    }
}
