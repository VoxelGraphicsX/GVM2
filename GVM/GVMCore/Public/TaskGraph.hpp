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
        template <class Value>
        TaskGraphOutput makeTaskGraphOutput(Task<Value> &&output)
        {
            return TaskGraphOutput{
                eastl::move(output.mDefinition),
                output.mIndex,
                [resolve = eastl::move(output.mResolve)](const eastl::any &value) -> eastl::any
                {
                    if constexpr (eastl::is_void_v<Value>)
                    {
                        return {};
                    }
                    else
                    {
                        return resolve(value);
                    }
                },
                [](const eastl::any &value)
                {
                    eastl::vector<const void *> resources;
                    if constexpr (!eastl::is_void_v<Value> && containsGraphResources<Value>)
                    {
                        visitGraphResources(eastl::any_cast<const Value &>(value), [&resources](const void *resource)
                        {
                            resources.push_back(resource);
                        });
                    }
                    return resources;
                }};
        }

        template <class Callable, class... Values>
        inline constexpr bool isExplicitTaskSubgraphBuilder = []
        {
            if constexpr (eastl::is_invocable_v<Callable &, TaskGraph &, Values...>)
            {
                return IsTask<eastl::invoke_result_t<Callable &, TaskGraph &, Values...>>::value;
            }
            return false;
        }();

        template <class Callable, class... Values>
        inline constexpr bool isEntryTaskSubgraphBuilder = []
        {
            if constexpr (eastl::is_invocable_v<Callable &, Values...>)
            {
                return IsTask<eastl::invoke_result_t<Callable &, Values...>>::value;
            }
            return false;
        }();

        enum class TaskNodeKind : uint8_t
        {
            CpuLeaf,
            RenderSubgraph,
            ExplicitTaskSubgraph,
            EntryTaskSubgraph,
        };

        template <class Callable, TaskNodeKind Kind, class... Values>
        struct TaskResult;

        /// Infers the result of an ordinary owned CPU callable.
        template <class Callable, class... Values>
        struct TaskResult<Callable, TaskNodeKind::CpuLeaf, Values...>
        {
            using Value = eastl::invoke_result_t<Callable &, Values...>;
        };

        /// Infers the declared result of a statically resolved RenderNode subgraph builder.
        template <class Callable, class... Values>
        struct TaskResult<Callable, TaskNodeKind::RenderSubgraph, Values...>
        {
            using Value = typename Callable::Output;
        };

        /// Unwraps Task<R> returned by an explicit child TaskGraph builder callable(TaskGraph &, Values...).
        template <class Callable, class... Values>
        struct TaskResult<Callable, TaskNodeKind::ExplicitTaskSubgraph, Values...>
        {
            using SubgraphTask = eastl::invoke_result_t<Callable &, TaskGraph &, Values...>;
            using Value = typename SubgraphTask::Value;
        };

        /// Unwraps Task<R> returned by a nested graph entry callable(Values...).
        template <class Callable, class... Values>
        struct TaskResult<Callable, TaskNodeKind::EntryTaskSubgraph, Values...>
        {
            using SubgraphTask = eastl::invoke_result_t<Callable &, Values...>;
            using Value = typename SubgraphTask::Value;
        };

        /// Maps an ordinary argument to its owned value type.
        template <class T>
        struct TaskArgument
        {
            using Value = T;
            static constexpr bool deferred = false;
            static constexpr bool deferredVector = false;
        };

        /// Maps a deferred argument to the value supplied after its producer completes.
        template <class T>
        struct TaskArgument<Task<T>>
        {
            using Value = T;
            static constexpr bool deferred = true;
            static constexpr bool deferredVector = false;
        };

        /// Maps a dynamic vector of deferred tasks to an owned vector of resolved values.
        template <class T>
        struct TaskArgument<eastl::vector<Task<T>>>
        {
            static_assert(!eastl::is_void_v<T>, "A vector of void tasks is not a value argument.");
            using Element = T;
            using Value = eastl::vector<T>;
            static constexpr bool deferred = false;
            static constexpr bool deferredVector = true;
        };
    }

    /// Builds an owning DAG of ordinary CPU callables, nested TaskGraphs, and invocation-local RenderNode subgraphs.
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
            constexpr bool buildsRenderSubgraph = requires { typename Callable::RenderNodeProtocol; };
            constexpr bool buildsExplicitTaskSubgraph = !buildsRenderSubgraph &&
                Private::isExplicitTaskSubgraphBuilder<Callable, typename Private::TaskArgument<Arguments>::Value...>;
            constexpr bool buildsEntryTaskSubgraph = !buildsRenderSubgraph && !buildsExplicitTaskSubgraph &&
                Private::isEntryTaskSubgraphBuilder<Callable, typename Private::TaskArgument<Arguments>::Value...>;
            constexpr bool buildsTaskSubgraph = buildsExplicitTaskSubgraph || buildsEntryTaskSubgraph;
            constexpr bool allowsGraphResources = buildsRenderSubgraph || buildsTaskSubgraph;
            constexpr auto nodeKind = buildsRenderSubgraph
                ? Private::TaskNodeKind::RenderSubgraph
                : (buildsExplicitTaskSubgraph
                       ? Private::TaskNodeKind::ExplicitTaskSubgraph
                       : (buildsEntryTaskSubgraph ? Private::TaskNodeKind::EntryTaskSubgraph : Private::TaskNodeKind::CpuLeaf));

            using Result = typename Private::TaskResult<
                Callable,
                nodeKind,
                typename Private::TaskArgument<Arguments>::Value...>::Value;

            static_assert(allowsGraphResources || ((!Private::containsGraphResources<typename Private::TaskArgument<Arguments>::Value>) && ...),
                "GPU resources can only be consumed by RenderNode builders, ReadbackNode adapters, or nested TaskGraphs.");
            static_assert(allowsGraphResources || !Private::containsGraphResources<Result>,
                "CPU tasks cannot produce GPU resources.");
            static_assert(allowsGraphResources || !Private::containsGraphResources<Callable>,
                "CPU callables cannot capture GPU resources.");
            static_assert(!eastl::is_reference_v<Result> && !eastl::is_pointer_v<Result>, "Task results must own their values.");
            static_assert((!eastl::is_pointer_v<Arguments> && ...), "Task inputs cannot borrow pointer storage.");

            Private::TaskDefinition definition;
            definition.buildsSubgraph = buildsRenderSubgraph;
            definition.buildsTaskSubgraph = buildsTaskSubgraph;
            (appendDependency(definition, arguments), ...);
            auto inputs = eastl::make_tuple(captureInput(arguments)...);
            if constexpr (buildsTaskSubgraph)
            {
                definition.evaluateSubgraph = [callable = eastl::move(callable), inputs = eastl::move(inputs)](
                                                  const Private::TaskResults &results) mutable -> Private::TaskGraphOutput
                {
                    return eastl::apply([&callable, &results](const auto &...resolve) -> Private::TaskGraphOutput
                    {
                        if constexpr (buildsExplicitTaskSubgraph)
                        {
                            TaskGraph childGraph;
                            auto childOutput = eastl::invoke(callable, childGraph, resolve(results)...);
                            if (!childOutput.mDefinition || childOutput.mDefinition != childGraph.mDefinition)
                            {
                                throw std::invalid_argument(
                                    "Nested TaskGraph builder must return a Task belonging to its child TaskGraph.");
                            }
                            return Private::makeTaskGraphOutput(eastl::move(childOutput));
                        }
                        else
                        {
                            auto childOutput = eastl::invoke(callable, resolve(results)...);
                            if (!childOutput.mDefinition)
                            {
                                throw std::invalid_argument("Nested TaskGraph entry returned an unbound Task.");
                            }
                            return Private::makeTaskGraphOutput(eastl::move(childOutput));
                        }
                    }, inputs);
                };
            }
            else
            {
                definition.evaluate = [callable = eastl::move(callable), inputs = eastl::move(inputs)](
                                          const Private::TaskResults &results, RenderNode *node) mutable -> eastl::any
                {
                    return eastl::apply([&callable, &results, node](const auto &...resolve) -> eastl::any
                    {
                        if constexpr (buildsRenderSubgraph)
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
            }
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
            else if constexpr (Private::TaskArgument<Argument>::deferredVector)
            {
                for (const auto &element : argument)
                {
                    if (!element.mDefinition || element.mDefinition != mDefinition)
                    {
                        throw std::invalid_argument(
                            "TaskGraph vector input element belongs to a different graph or is unbound.");
                    }
                    definition.predecessors.push_back(element.mIndex);
                }
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
                return [index = argument.mIndex, resolve = argument.mResolve](const Private::TaskResults &results) -> Value
                {
                    return resolve(results[index]);
                };
            }
            else if constexpr (Private::TaskArgument<Argument>::deferredVector)
            {
                using Element = typename Private::TaskArgument<Argument>::Element;
                struct ElementResolver
                {
                    uint32_t index = 0;
                    eastl::function<Element(const eastl::any &)> resolve;
                };
                eastl::vector<ElementResolver> resolvers;
                resolvers.reserve(argument.size());
                for (const auto &element : argument)
                {
                    resolvers.push_back(ElementResolver{element.mIndex, element.mResolve});
                }
                return [resolvers = eastl::move(resolvers)](const Private::TaskResults &results) -> eastl::vector<Element>
                {
                    eastl::vector<Element> values;
                    values.reserve(resolvers.size());
                    for (const auto &entry : resolvers)
                    {
                        values.push_back(entry.resolve(results[entry.index]));
                    }
                    return values;
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
