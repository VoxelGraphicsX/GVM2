#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <limits>

namespace UGL
{
    /// Identifies a readable texture handle targeted by a graph texture readback.
    struct TextureReadbackDescriptor final
    {
        /// Creates an empty texture readback descriptor.
        TextureReadbackDescriptor() = default;
        /// Targets level zero of a copy-source texture across its full mip-zero bounds.
        template <class Format, class Usages, class Dimension>
            requires(IsCopySrcTexture<Usages>)
        TextureReadbackDescriptor(Texture<Format, Usages, Dimension>)
        {
        }
    };

    /// Explicit invocation-local subgraph builder passed by mutable reference to RenderNode methods when authored explicitly.
    /// Equivalent intrinsics remain available implicitly inside IRenderNode member functions for legacy DSL classes.
    class RenderNode final
    {
    public:
        RenderNode(const RenderNode &) = delete;
        RenderNode &operator=(const RenderNode &) = delete;
        /// Allocates an existing typed Buffer whose lifetime is managed internally by the graph execution.
        template <class BufferType>
        BufferType createBuffer(eastl::string label, uint count);
        /// Allocates an existing one-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension1D<TextureType>)
        TextureType createTexture(eastl::string label, uint width);
        /// Allocates an existing two-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension2D<TextureType>)
        TextureType createTexture(eastl::string label, uint2 extent);
        /// Allocates an existing three-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension3D<TextureType>)
        TextureType createTexture(eastl::string label, uint3 extent);
        /// Creates the default view without exposing a resource pointer or changing its ownership.
        template <class Format, class Usage, class Dimension>
        TextureView<Format, Usage, Dimension> view(Texture<Format, Usage, Dimension> texture);
        /// Captures CPU elements in an upload pass without mutating a shared queue's pending operations.
        template <class BufferType, class T>
        void writeBuffer(BufferType destination, eastl::vector<T> values);
        /// Captures owned CPU texels in a transfer upload pass targeting one mip level and array layer.
        template <class TextureType, class T>
            requires(IsCopyDstTexture<TextureType>)
        void writeTexture(TextureType destination, const eastl::vector<T> &values,
            uint32_t mipLevel = 0, uint32_t arrayLayer = 0);
        /// Creates an immutable shader dispatch; the last three arguments are thread counts.
        template <class Shader, class... Arguments>
        ComputePassTaskDescriptor dispatchThreads(Arguments... arguments);
        /// Captures a raster or pixel-local shader invocation; ordinary raster shaders append four draw arguments.
        template <class Shader, class... Arguments>
        RenderPassTaskDescriptor draw(Arguments... arguments);
        /// Creates one logical compute pass from a set of immutable dispatch descriptors.
        template <class... Tasks>
        void computePass(eastl::string label, Tasks... tasks);
        /// Creates one render pass from ordinary and pixel-local draw phases whose effects drive scheduling.
        template <class Framebuffer, class... Tasks>
            requires(Private::RenderPassPhase<Tasks> && ...)
        void renderPass(eastl::string label, const Framebuffer &framebuffer, Tasks... tasks);
        /// Blits a rendered texture onto a queried swapchain image and appends optional ordinary draws on the main queue.
        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        void renderToSwapchain(const SwapchainQueryResult &swapchainResult,
            SourceTexture sourceTexture,
            const RenderToSwapchainDescriptor &descriptor,
            Args &&...args);
        /// Blits a rendered texture onto a queried swapchain image with default presentation options on the main queue.
        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        void renderToSwapchain(const SwapchainQueryResult &swapchainResult,
            SourceTexture sourceTexture,
            Args &&...args);
        /// Copies equally sized graph ranges as a separately schedulable transfer pass.
        template <class Source, class Destination>
        void copyBuffer(Source source, Destination destination);
        /// Copies mip-zero contents between two compatible graph textures as a transfer pass.
        template <class SourceTexture, class DestinationTexture>
            requires(IsCopySrcTexture<SourceTexture> && IsCopyDstTexture<DestinationTexture>)
        void copyTexture(SourceTexture source, DestinationTexture destination);
        /// Fills an aligned graph range as a separately schedulable transfer pass.
        template <class BufferType>
        void fillBuffer(BufferType target, uint8_t value);
    };

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
        /// Allocates an existing one-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension1D<TextureType>)
        TextureType createTexture(eastl::string label, uint width) const;
        /// Allocates an existing two-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension2D<TextureType>)
        TextureType createTexture(eastl::string label, uint2 extent) const;
        /// Allocates an existing three-dimensional texture whose lifetime belongs to this execution.
        template <class TextureType>
            requires(IsTextureDimension3D<TextureType>)
        TextureType createTexture(eastl::string label, uint3 extent) const;
        /// Creates the default view without exposing a resource pointer or changing its ownership.
        template <class Format, class Usage, class Dimension>
        TextureView<Format, Usage, Dimension> view(Texture<Format, Usage, Dimension> texture) const;
        /// Captures CPU elements in an upload pass, without mutating a shared queue's pending operations.
        template <class BufferType, class T>
        void writeBuffer(BufferType destination, eastl::vector<T> values) const;
        /// Captures owned CPU texels in a transfer upload pass targeting one mip level and array layer.
        template <class TextureType, class T>
            requires(IsCopyDstTexture<TextureType>)
        void writeTexture(TextureType destination, const eastl::vector<T> &values,
            uint32_t mipLevel = 0, uint32_t arrayLayer = 0) const;
        /// Creates an immutable shader dispatch; the last three arguments are thread counts.
        template <class Shader, class... Arguments>
        ComputePassTaskDescriptor dispatchThreads(Arguments... arguments) const;
        /// Captures a raster or pixel-local invocation; ordinary raster shaders append four draw arguments.
        template <class Shader, class... Arguments>
        RenderPassTaskDescriptor draw(Arguments... arguments) const;
        /// Creates one logical compute pass from a set of immutable dispatch descriptors.
        template <class... Tasks>
        void computePass(eastl::string label, Tasks... tasks) const;
        /// Creates one render pass from ordinary and pixel-local draw phases whose effects drive scheduling.
        template <class Framebuffer, class... Tasks>
            requires(Private::RenderPassPhase<Tasks> && ...)
        void renderPass(eastl::string label, const Framebuffer &framebuffer, Tasks... tasks) const;
        /// Blits a rendered texture onto a queried swapchain image and appends optional ordinary draws on the main queue.
        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        void renderToSwapchain(const SwapchainQueryResult &swapchainResult,
            SourceTexture sourceTexture,
            const RenderToSwapchainDescriptor &descriptor,
            Args &&...args) const;
        /// Blits a rendered texture onto a queried swapchain image with default presentation options on the main queue.
        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        void renderToSwapchain(const SwapchainQueryResult &swapchainResult,
            SourceTexture sourceTexture,
            Args &&...args) const;
        /// Copies equally sized graph ranges as a separately schedulable transfer pass.
        template <class Source, class Destination>
        void copyBuffer(Source source, Destination destination) const;
        /// Copies mip-zero contents between two compatible graph textures as a transfer pass.
        template <class SourceTexture, class DestinationTexture>
            requires(IsCopySrcTexture<SourceTexture> && IsCopyDstTexture<DestinationTexture>)
        void copyTexture(SourceTexture source, DestinationTexture destination) const;
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

    /// Converts tightly packed texel bytes from a texture into an owned CPU vector through a completion continuation.
    template <class T>
    class ReadbackTextureNode final : public IRenderNode<TextureReadbackDescriptor, eastl::vector<T>>
    {
    public:
        /// Emits a staging copy and publishes the owned vector only after GPU completion.
        eastl::vector<T> build(TextureReadbackDescriptor input) const;
    };

    /// Names a deferred graph result; values are supplied to dependent graph.add calls automatically.
    template <class T>
    class Task
    {
    public:
        using Value = T;
    };

    class TaskGraph;

    namespace Private
    {
        template <class T>
        struct TypeIdentity
        {
            using Type = T;
        };

        template <class T>
        struct UnwrapTask
        {
            using Value = T;
        };

        template <class T>
        struct UnwrapTask<Task<T>>
        {
            using Value = T;
        };

        /// Resolves ordinary function, nested TaskGraph builder, and class results for the uniform graph.add API.
        template <class Callable, bool Subgraph, class... Arguments>
        struct GraphResult
        {
            static auto resolve(Callable &fn, TaskGraph &sub, Arguments &&...args)
            {
                if constexpr (requires { fn(sub, static_cast<Arguments &&>(args)...); })
                {
                    return TypeIdentity<typename UnwrapTask<decltype(fn(sub, static_cast<Arguments &&>(args)...))>::Value>{};
                }
                else
                {
                    return TypeIdentity<typename UnwrapTask<eastl::invoke_result_t<Callable &, Arguments...>>::Value>{};
                }
            }

            using Value = typename decltype(resolve(
                eastl::declval<Callable &>(),
                eastl::declval<TaskGraph &>(),
                eastl::declval<Arguments>()...))::Type;
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

        /// Resolves a dynamic vector of deferred graph inputs into an owned vector of values.
        template <class T>
        struct TaskArgument<eastl::vector<Task<T>>>
        {
            using Value = eastl::vector<T>;
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
        ExecutionStatus status() const { return ExecutionStatus::Succeeded; }
        /// Reports any terminal outcome, including failure and cancellation.
        bool ready() const { return true; }
        /// Returns bounded diagnostics without waiting.
        eastl::string error() const { return {}; }
        /// Waits at a host boundary only; internal task dependencies use graph values.
        bool wait(uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max()) const { (void)timeoutNanoseconds; return true; }
        /// Requests cancellation while accepted operations retain their resources until drained.
        void cancel() const {}
        /// Retrieves a successful result without waiting; exported Buffer and Texture allocations require explicit destruction.
        T get() const { return {}; }
    };

    /// Collects ordinary functions, nested TaskGraphs, and class invocations into one graph without execution-domain flags.
    class TaskGraph
    {
    public:
        /// Captures values and infers dependencies from Task arguments; functions are static declarations.
        template <class Callable, class... Arguments>
        Task<typename Private::GraphResult<Callable, requires { typename Callable::RenderNodeProtocol; }, typename Private::TaskArgument<Arguments>::Value...>::Value>
        add(const Callable &callable, Arguments... arguments)
        {
            (void)callable;
            ((void)arguments, ...);
            return {};
        }
    };

    /// Drives a graph entry independently of Renderer lifecycle; execution details remain in Core.
    class TaskRuntime
    {
    public:
        /// Creates a runtime using the default portable executor.
        TaskRuntime() = default;
        /// Enables GPU subgraphs on a borrowed device inside DSL host orchestration.
        explicit TaskRuntime(DeviceImpl &device)
        {
            (void)device;
        }
        /// Captures an entry and input snapshot, then asynchronously computes its deferred output.
        template <class Builder, class Input>
        Execution<typename eastl::invoke_result_t<Builder &, Input>::Value> submit(const Builder &builder, Input input)
        {
            (void)builder;
            (void)input;
            return {};
        }
    };
}
