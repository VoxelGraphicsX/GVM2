#pragma once

#include "Task.hpp"
#include "IRenderNode.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include "../Private/TaskGraphDefinition.hpp"
#include <EASTL/tuple.h>
#include <EASTL/type_traits.h>
#include <stdexcept>

namespace GVM::Core
{
    namespace Private
    {
        /// Infers the result of an ordinary owned CPU callable.
        template <class Callable, bool BuildsSubgraph, class... Values>
        struct TaskResult
        {
            using Value = eastl::invoke_result_t<Callable &, Values...>;
        };
        /// Infers the declared result of a statically resolved RenderNode subgraph builder.
        template <class Callable, class... Values>
        struct TaskResult<Callable, true, Values...>
        {
            using Value = typename Callable::Output;
        };

        /// Maps an ordinary argument to its owned value type.
        template <class T>
        struct TaskArgument
        {
            using Value = T;
            static constexpr bool deferred = false;
        };

        /// Maps a deferred argument to the value supplied after its producer completes.
        template <class T>
        struct TaskArgument<Task<T>>
        {
            using Value = T;
            static constexpr bool deferred = true;
        };
    }

    /// Builds an owning DAG of ordinary CPU callables and invocation-local RenderNode subgraphs.
    /// The DSL compiler enforces the stronger transitive resource and capture contract.
    class TaskGraph final
    {
    public:
        /// Creates an empty graph whose outputs remain valid after the builder is destroyed.
        TaskGraph();
        /// Releases the builder's ownership without invalidating retained task outputs.
        ~TaskGraph();
        TaskGraph(const TaskGraph &) = delete;
        TaskGraph &operator=(const TaskGraph &) = delete;

        /// Copies a callable and its value arguments, adding dependencies for deferred arguments.
        /// Native function pointers are an implementation boundary, never a DSL capability.
        template <class Callable, class... Arguments>
        auto add(Callable callable, const Arguments &...arguments)
        {
            constexpr bool buildsSubgraph = requires { typename Callable::RenderNodeProtocol; };
            using Result = typename Private::TaskResult<Callable, buildsSubgraph, typename Private::TaskArgument<Arguments>::Value...>::Value;
            static_assert(buildsSubgraph || ((!eastl::is_same_v<typename Private::TaskArgument<Arguments>::Value, RHI::Buffer> && !eastl::is_same_v<typename Private::TaskArgument<Arguments>::Value, RHI::BufferRange>) && ...),
                "GPU resources can only be consumed by RenderNode builders or ReadbackNode adapters.");
            static_assert(buildsSubgraph || (!eastl::is_same_v<Result, RHI::Buffer> && !eastl::is_same_v<Result, RHI::BufferRange>), "CPU tasks cannot produce GPU resources.");
            static_assert(!eastl::is_reference_v<Result> && !eastl::is_pointer_v<Result>, "Task results must own their values.");
            static_assert((!eastl::is_pointer_v<Arguments> && ...), "Task inputs cannot borrow pointer storage.");
            Private::TaskDefinition definition;
            definition.buildsSubgraph = buildsSubgraph;
            (appendDependency(definition, arguments), ...);
            auto inputs = eastl::make_tuple(captureInput(arguments)...);
            definition.evaluate = [callable = eastl::move(callable), inputs = eastl::move(inputs)](const Private::TaskResults &results, RenderNode *node) mutable -> eastl::any
            {
                return eastl::apply([&callable, &results, node](const auto &...resolve) -> eastl::any
                {
                    if constexpr (buildsSubgraph)
                    {
                        if constexpr (eastl::is_void_v<Result>)
                        {
                            callable.build(*node, resolve(results)...);
                            return {};
                        }
                        else
                        {
                            return eastl::any(callable.build(*node, resolve(results)...));
                        }
                    }
                    else if constexpr (eastl::is_void_v<Result>)
                    {
                        eastl::invoke(callable, resolve(results)...);
                        return {};
                    }
                    else
                    {
                        return eastl::any(eastl::invoke(callable, resolve(results)...));
                    }
                }, inputs);
            };
            return Task<Result>(mDefinition, mDefinition->append(eastl::move(definition)));
        }

    private:
        /// Adds only deferred producers and rejects missing or foreign graph outputs.
        template <class Argument>
        void appendDependency(Private::TaskDefinition &definition, const Argument &argument) const
        {
            if constexpr (Private::TaskArgument<Argument>::deferred)
            {
                if (!argument.mDefinition || argument.mDefinition != mDefinition)
                {
                    throw std::invalid_argument("TaskGraph input belongs to a different graph or is unbound.");
                }
                definition.predecessors.push_back(argument.mIndex);
            }
        }

        /// Captures a value or producer index without creating a graph-to-task ownership cycle.
        template <class Argument>
        static auto captureInput(const Argument &argument)
        {
            if constexpr (Private::TaskArgument<Argument>::deferred)
            {
                using Value = typename Argument::Value;
                static_assert(!eastl::is_void_v<Value>, "A void task is not a value argument.");
                return [index = argument.mIndex](const Private::TaskResults &results) -> Value
                {
                    return eastl::any_cast<const Value &>(results[index]);
                };
            }
            else
            {
                return [value = argument](const Private::TaskResults &) -> Argument
                {
                    return value;
                };
            }
        }

        eastl::shared_ptr<Private::TaskGraphDefinition> mDefinition;
    };
}
