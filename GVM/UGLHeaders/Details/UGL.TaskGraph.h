#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <limits>

namespace UGL
{
    /// Defines a statically resolved subgraph builder with invocation-local construction intrinsics.
    template <class InputValue, class OutputValue>
    class IRenderNode
    {
    public:
        using Input = InputValue;
        using Output = OutputValue;
        using RenderNodeProtocol = void;
    protected:
        /// Allocates an existing typed Buffer whose lifetime is managed internally by the graph execution.
        template <class BufferType>
        BufferType createBuffer(eastl::string label, uint count) const;
        /// Allocates an existing two-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
        TextureType createTexture(eastl::string label, uint2 extent) const;
        /// Creates the default view without exposing a resource pointer or changing its ownership.
        template <class Format, class Usage, class Dimension>
        TextureView<Format, Usage, Dimension> view(Texture<Format, Usage, Dimension> texture) const;
        /// Captures CPU elements in an upload pass, without mutating a shared queue's pending operations.
        template <class BufferType, class T>
        void writeBuffer(BufferType destination, eastl::vector<T> values) const;
        /// Creates an immutable shader dispatch; the last three arguments are thread counts.
        template <class Shader, class... Arguments>
        ComputePassTaskDescriptor dispatchThreads(Arguments... arguments) const;
        /// Captures a raster invocation; the final arguments are vertex count, instance count and first indices.
        template <class Shader, class... Arguments>
        RenderPassTaskDescriptor draw(Arguments... arguments) const;
        /// Creates one logical compute pass from a set of immutable dispatch descriptors.
        template <class... Tasks>
        void computePass(eastl::string label, Tasks... tasks) const;
        /// Creates one ordinary render pass whose attachment and shader effects drive scheduling.
        template <class Framebuffer, class... Tasks>
        void renderPass(eastl::string label, Framebuffer framebuffer, Tasks... tasks) const;
        /// Copies equally sized graph ranges as a separately schedulable transfer pass.
        template <class Source, class Destination>
        void copyBuffer(Source source, Destination destination) const;
        /// Fills an aligned graph range as a separately schedulable transfer pass.
        template <class BufferType>
        void fillBuffer(BufferType target, uint8_t value) const;
    };

    /// Converts tightly packed GPU elements to an owned CPU vector through a completion continuation.
    template <class T>
    class ReadbackNode final : public IRenderNode<BufferRange<T, BufferUsage<CopySrc>>, eastl::vector<T>>
    {
    public:
        /// Emits a staging copy and publishes the owned vector only after GPU completion.
        eastl::vector<T> build(BufferRange<T, BufferUsage<CopySrc>> input) const;
    };

    /// Names a deferred graph result; values are supplied to dependent graph.add calls automatically.
    template <class T>
    class Task
    {
    public:
        using Value = T;
    };

    namespace Private
    {
        /// Resolves ordinary function and class results for the uniform graph.add API.
        template <class Callable, bool Subgraph, class... Arguments>
        struct GraphResult
        {
            using Value = eastl::invoke_result_t<Callable &, Arguments...>;
        };
        /// Resolves a subgraph builder's declared output without invoking its build method during parsing.
        template <class Callable, class... Arguments>
        struct GraphResult<Callable, true, Arguments...>
        {
            using Value = typename Callable::Output;
        };
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
        /// Retrieves a successful result without waiting; exported Buffer and Texture allocations require explicit destruction.
        T get() const;
    };

    /// Collects ordinary functions and class invocations into one graph without execution-domain flags.
    class TaskGraph
    {
    public:
        /// Captures values and infers dependencies from Task arguments; functions are static declarations.
        template <class Callable, class... Arguments>
        Task<typename Private::GraphResult<Callable, requires { typename Callable::RenderNodeProtocol; }, typename Private::TaskArgument<Arguments>::Value...>::Value>
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
