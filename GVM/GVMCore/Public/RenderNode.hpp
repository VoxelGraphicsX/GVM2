#pragma once
#include "GraphResourceTraits.hpp"
#include "../Private/GraphPass.hpp"
#include "../Private/GSync.hpp"
#include <EASTL/tuple.h>
#include <EASTL/type_traits.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace GVM::Core
{
    namespace Private
    {
        class GraphDevice;
        class TaskExecutionState;

        /// Evaluates a two-component extent expression once and converts it to a 2D RHI::Extent3D.
        template <class Extent2>
        constexpr RHI::Extent3D makeTextureExtent2D(const Extent2 &extent) noexcept
        {
            return RHI::Extent3D{uint32_t(extent.x), uint32_t(extent.y), 1u};
        }

        /// Evaluates a three-component extent expression once and converts it to a 3D RHI::Extent3D.
        template <class Extent3>
        constexpr RHI::Extent3D makeTextureExtent3D(const Extent3 &extent) noexcept
        {
            return RHI::Extent3D{uint32_t(extent.x), uint32_t(extent.y), uint32_t(extent.z)};
        }
    }
    template <class T> class ReadbackNode;
    template <class T> class ReadbackTextureNode;

    /// Identifies the texture subresource and optional sub-region targeted by a graph texture readback.
    /// Converts implicitly from RHI::Texture so callers can pass either a handle or an explicit region.
    struct TextureReadbackDescriptor
    {
        RHI::ImageCopyTexture source;
        RHI::Extent3D readSize = {};

        /// Creates an empty texture readback descriptor.
        TextureReadbackDescriptor() = default;
        /// Targets level zero of a live texture handle across its full mip-zero bounds.
        TextureReadbackDescriptor(RHI::Texture texture)
            : source{.texture = eastl::move(texture)}
        {
        }
        /// Targets an explicit subresource origin and optional copy extent within a texture.
        TextureReadbackDescriptor(RHI::ImageCopyTexture copySource, RHI::Extent3D extent = {})
            : source(eastl::move(copySource))
            , readSize(extent)
        {
        }
    };

    /// Enables TaskGraph and TaskRuntime to track exported or deferred TextureReadbackDescriptor lifetimes.
    template <>
    struct GraphResourceTraits<TextureReadbackDescriptor>
    {
        static constexpr bool containsResources = true;

        /// Visits the referenced parent texture handle when non-null.
        template <class Visitor>
        static void visit(const TextureReadbackDescriptor &value, const Visitor &visitor)
        {
            visitor(value.source.texture.get());
        }
    };

    /// Retains staging storage and exact byte size produced by RenderNode::prepareTextureReadback.
    struct TextureReadbackStaging
    {
        eastl::shared_ptr<Private::GraphResourceAllocation> allocation;
        uint64_t byteCount = 0;
    };

    /// Builds one invocation-local subgraph; configuration objects never store a current context or encoder.
    /// Native C++ builders receive this value by reference; the DSL supplies it as a hidden build parameter.
    class RenderNode final
    {
    public:
        /// Releases an unsubmitted fragment or transfers its resource ownership to the runtime.
        ~RenderNode();
        RenderNode(const RenderNode &) = delete;
        RenderNode &operator=(const RenderNode &) = delete;
        /// Allocates existing buffer storage; successful output retrieval transfers explicit destruction to the caller.
        RHI::Buffer createBuffer(const RHI::BufferDescriptor &descriptor);
        /// Allocates existing texture storage; successful output retrieval transfers explicit destruction to the caller.
        RHI::Texture createTexture(const RHI::TextureDescriptor &descriptor);
        /// Obtains the texture's default view through the serialized object-creation boundary.
        RHI::TextureView view(RHI::Texture texture);
        /// Captures an ordered list of dispatches, fusing conflicting serial dispatches while splitting independent ones.
        void computePass(const eastl::string &label, eastl::vector<ComputePassTaskDescriptor> tasks);
        /// Captures a framebuffer and draw list; distinct graphics passes remain GPU-ordered.
        void renderPass(RHI::RenderPassDescriptor descriptor, eastl::vector<RenderPassTaskDescriptor> tasks);
        /// Copies a graph byte range as its own transfer pass without touching queue pending lists.
        void copyBuffer(RHI::BufferRange source, RHI::BufferRange destination);
        /// Fills a graph byte range as its own transfer pass; ranges must be four-byte aligned.
        void fillBuffer(RHI::BufferRange target, uint8_t value);
        /// Snapshots raw CPU bytes into a staging allocation and emits a transfer upload pass into destination.
        void writeTexture(RHI::ImageCopyTexture destination, const void *data, uint64_t dataSize,
            RHI::TextureDataLayout dataLayout = {}, RHI::Extent3D writeSize = {});
        /// Snapshots raw CPU bytes into one mip level and array layer of destination using a transfer pass.
        void writeTexture(RHI::Texture destination, const void *data, uint64_t dataSize,
            uint32_t mipLevel = 0, uint32_t arrayLayer = 0);
        /// Copies a compatible texture subresource or region as its own transfer pass.
        void copyTexture(RHI::ImageCopyTexture source, RHI::ImageCopyTexture destination, RHI::Extent3D copySize = {});
        /// Copies the full mip-zero bounds between two compatible textures as a transfer pass.
        void copyTexture(RHI::Texture source, RHI::Texture destination);
        /// Attaches an accepted external GPU submission dependency to subsequent passes in this invocation.
        void waitFor(RHI::Submission submission);

        /// Packs ordinary dispatch descriptors into one logical compute pass.
        template <class... Tasks>
            requires((eastl::is_same_v<Tasks, ComputePassTaskDescriptor>) && ...)
        void computePass(const eastl::string &label, Tasks... tasks)
        {
            computePass(label, eastl::vector<ComputePassTaskDescriptor>{eastl::move(tasks)...});
        }
        /// Captures a generated framebuffer and its ordinary or pixel-local draw phases by value.
        template <class Framebuffer, class... Tasks>
            requires((Private::RenderPassPhase<Tasks>) && ...)
        void renderPass(const eastl::string &label, const Framebuffer &framebuffer, Tasks... tasks)
        {
            RHI::RenderPassDescriptor descriptor;
            if constexpr (eastl::is_same_v<Framebuffer, RHI::RenderPassDescriptor>)
            {
                descriptor = framebuffer;
            }
            else
            {
                descriptor = framebuffer.getRenderPassDescriptor();
            }
            descriptor.label = label;
            const Private::RenderPassPhaseCounter phaseCounter =
                Private::resolveRenderPassPhaseCounter("RenderNode::renderPass", tasks...);
            descriptor.pixelLocal.enabled = phaseCounter.hasPixelLocalPhase;
            descriptor.pixelLocal.passCount = phaseCounter.phaseCount;
            Private::PassResourceUsage taskResources;
            taskResources.complete = true;
            uint32_t drawCount = 0u;
            (collectRenderPhaseResources(taskResources, drawCount, tasks), ...);
            auto packedTasks = eastl::make_tuple(eastl::move(tasks)...);
            renderPassWithEncoder(eastl::move(descriptor), eastl::move(taskResources), drawCount,
                [packedTasks = eastl::move(packedTasks)](RHI::RenderPassEncoder render)
                {
                    Private::RenderPassPhaseState phaseState = {};
                    eastl::apply([&](const auto &...phase)
                    {
                        (Private::encodeRenderPassPhase(render, phaseState, phase), ...);
                    }, packedTasks);
                });
        }
        /// Blits a rendered texture onto a queried swapchain image and appends optional ordinary overlay draws on the main queue.
        template <class... Tasks>
            requires((eastl::is_same_v<Tasks, RenderPassTaskDescriptor>) && ...)
        void renderToSwapchain(const RHI::SwapchainQueryResult &swapchainResult, RHI::Texture sourceTexture,
            const RHI::RenderToSwapchainDescriptor &descriptor, Tasks... tasks)
        {
            renderToSwapchainImpl(swapchainResult, eastl::move(sourceTexture), descriptor,
                eastl::vector<RenderPassTaskDescriptor>{eastl::move(tasks)...});
        }
        /// Blits a rendered texture onto a queried swapchain image with default presentation options on the main queue.
        template <class... Tasks>
            requires((eastl::is_same_v<Tasks, RenderPassTaskDescriptor>) && ...)
        void renderToSwapchain(const RHI::SwapchainQueryResult &swapchainResult, RHI::Texture sourceTexture, Tasks... tasks)
        {
            renderToSwapchainImpl(swapchainResult, eastl::move(sourceTexture), RHI::RenderToSwapchainDescriptor{},
                eastl::vector<RenderPassTaskDescriptor>{eastl::move(tasks)...});
        }
        /// Copies owned CPU elements into a retained staging allocation and emits a transfer pass.
        template <class T>
        void writeBuffer(RHI::BufferRange destination, const eastl::vector<T> &values)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "GPU uploads require trivially copyable elements.");
            upload(eastl::move(destination), values.data(), values.size() * sizeof(T));
        }
        /// Copies owned CPU texels into a retained staging allocation and emits a transfer pass into destination.
        template <class T>
        void writeTexture(RHI::ImageCopyTexture destination, const eastl::vector<T> &values,
            RHI::TextureDataLayout dataLayout = {}, RHI::Extent3D writeSize = {})
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "GPU uploads require trivially copyable elements.");
            writeTexture(eastl::move(destination), values.data(), values.size() * sizeof(T), dataLayout, writeSize);
        }
        /// Copies owned CPU texels into one mip level and array layer of destination using a transfer pass.
        template <class T>
        void writeTexture(RHI::Texture destination, const eastl::vector<T> &values,
            uint32_t mipLevel = 0, uint32_t arrayLayer = 0)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "GPU uploads require trivially copyable elements.");
            writeTexture(eastl::move(destination), values.data(), values.size() * sizeof(T), mipLevel, arrayLayer);
        }
        /// Creates an immutable Args shader dispatch from creation inputs followed by three thread counts.
        template <class Shader, class... Arguments>
        ComputePassTaskDescriptor dispatchThreads(Arguments... arguments)
        {
            static_assert(sizeof...(Arguments) >= 3, "Dispatch requires three thread counts.");
            auto values = eastl::make_tuple(eastl::move(arguments)...);
            return dispatch<Shader>(values, eastl::make_index_sequence<sizeof...(Arguments) - 3>{});
        }
        /// Captures a raster or pixel-local shader invocation; ordinary raster shaders append four draw arguments.
        template <class Shader, class... Arguments>
        RenderPassTaskDescriptor draw(Arguments... arguments)
        {
            auto values = eastl::make_tuple(eastl::move(arguments)...);
            if constexpr (requires { Shader(&nativeDevice(), eastl::declval<Arguments>()...).draw(eastl::declval<Arguments>()...); })
            {
                return capturePixelLocalDraw<Shader>(values, eastl::make_index_sequence<sizeof...(Arguments)>{});
            }
            else
            {
                static_assert(sizeof...(Arguments) >= 4, "Draw requires four draw arguments.");
                return captureDraw<Shader>(values, eastl::make_index_sequence<sizeof...(Arguments) - 4>{});
            }
        }

    private:
        friend class Private::TaskExecutionState;
        template <class T> friend class ReadbackNode;
        template <class T> friend class ReadbackTextureNode;
        /// Starts an isolated invocation with a finite pass budget and a borrowed live device domain.
        RenderNode(eastl::shared_ptr<Private::GraphDevice> device, uint32_t maximumPasses,
            std::atomic<uint32_t> *sharedPassCount = nullptr);
        /// Serializes device object creation without exposing GraphDevice internals in public headers.
        std::unique_lock<std::mutex> lockDeviceResources();
        /// Borrows the underlying RHI device without exposing GraphDevice internals in public headers.
        RHI::DeviceImpl &nativeDevice() const;
        /// Estimates static compute pass cost from total dispatched threads.
        static uint16_t estimateComputeWeight(uint64_t x, uint64_t y, uint64_t z) noexcept;
        /// Transfers the frozen fragment exactly once after build completes successfully.
        Private::RenderNodeFragment finish();
        /// Enforces the runtime's per-execution pass limit before appending another operation.
        void append(Private::GraphPass pass);
        /// Creates a private allocation owner retained by this invocation and its execution.
        eastl::shared_ptr<Private::GraphResourceAllocation> allocateBuffer(const RHI::BufferDescriptor &descriptor);
        /// Snapshots host bytes synchronously, then captures an upload pass.
        void upload(RHI::BufferRange destination, const void *data, uint64_t size);
        /// Emits an owned staging copy for a typed readback node.
        eastl::shared_ptr<Private::GraphResourceAllocation> prepareReadback(RHI::BufferRange source);
        /// Emits a tightly packed texture-to-buffer staging copy for a typed texture readback node.
        TextureReadbackStaging prepareTextureReadback(const TextureReadbackDescriptor &source);
        /// Publishes a typed CPU resolver that runs only after this fragment's GPU completion.
        void resolveAfterGpu(eastl::function<eastl::any()> resolver);
        /// Validates and collects resource accesses and draw count from an ordinary render task descriptor.
        static void collectRenderPhaseResources(Private::PassResourceUsage &usage, uint32_t &drawCount, const RenderPassTaskDescriptor &task);
        /// Validates and collects resource accesses and draw count from every subpass in a pixel-local task group.
        static void collectRenderPhaseResources(Private::PassResourceUsage &usage, uint32_t &drawCount, const PixelLocalPassTaskDescriptor &tasks);
        /// Records one render pass from framebuffer attachment accesses, task resources, and an encoder callback.
        void renderPassWithEncoder(RHI::RenderPassDescriptor descriptor, Private::PassResourceUsage taskResources,
            uint32_t drawCount, eastl::function<void(RHI::RenderPassEncoder)> encodeTasks);
        /// Records a main-queue swapchain fullscreen blit and optional ordinary overlay draws.
        void renderToSwapchainImpl(const RHI::SwapchainQueryResult &swapchainResult, RHI::Texture sourceTexture,
            const RHI::RenderToSwapchainDescriptor &descriptor, eastl::vector<RenderPassTaskDescriptor> tasks);
        /// Constructs shader objects under the graph device lock while retaining immutable argument snapshots.
        template <class Shader, class Tuple, size_t... Indices>
        ComputePassTaskDescriptor dispatch(Tuple &values, eastl::index_sequence<Indices...>)
        {
            auto lock = lockDeviceResources();
            Shader shader(&nativeDevice(), eastl::get<Indices>(values)...);
            auto task = shader.dispatchThreads(eastl::get<Indices>(values)...,
                eastl::get<sizeof...(Indices)>(values), eastl::get<sizeof...(Indices) + 1>(values),
                eastl::get<sizeof...(Indices) + 2>(values));
            if constexpr (
                eastl::is_convertible_v<decltype(eastl::get<sizeof...(Indices)>(values)), uint64_t> &&
                eastl::is_convertible_v<decltype(eastl::get<sizeof...(Indices) + 1>(values)), uint64_t> &&
                eastl::is_convertible_v<decltype(eastl::get<sizeof...(Indices) + 2>(values)), uint64_t>)
            {
                task.weight = estimateComputeWeight(
                    static_cast<uint64_t>(eastl::get<sizeof...(Indices)>(values)),
                    static_cast<uint64_t>(eastl::get<sizeof...(Indices) + 1>(values)),
                    static_cast<uint64_t>(eastl::get<sizeof...(Indices) + 2>(values)));
            }
            return task;
        }
        /// Creates a raster shader and immutable draw arguments while the device object cache is protected.
        template <class Shader, class Tuple, size_t... Indices>
        RenderPassTaskDescriptor captureDraw(Tuple &values, eastl::index_sequence<Indices...>)
        {
            auto lock = lockDeviceResources();
            Shader shader(&nativeDevice(), eastl::get<Indices>(values)...);
            return shader.draw(eastl::get<Indices>(values)...,
                eastl::get<sizeof...(Indices)>(values), eastl::get<sizeof...(Indices) + 1>(values),
                eastl::get<sizeof...(Indices) + 2>(values), eastl::get<sizeof...(Indices) + 3>(values));
        }
        /// Creates a pixel-local shader and immutable draw arguments while the device object cache is protected.
        template <class Shader, class Tuple, size_t... Indices>
        RenderPassTaskDescriptor capturePixelLocalDraw(Tuple &values, eastl::index_sequence<Indices...>)
        {
            auto lock = lockDeviceResources();
            Shader shader(&nativeDevice(), eastl::get<Indices>(values)...);
            return shader.draw(eastl::get<Indices>(values)...);
        }
        eastl::shared_ptr<Private::GraphDevice> mDevice;
        Private::RenderNodeFragment mFragment;
        eastl::vector<RHI::Submission> mPendingExternalWaits;
        bool mHasUnattachedExternalWaits = false;
        uint32_t mMaximumPasses;
        std::atomic<uint32_t> *mSharedPassCount = nullptr;
        bool mCommitted = false;
    };
}
