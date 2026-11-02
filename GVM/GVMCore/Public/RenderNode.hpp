#pragma once
#include "../Private/GraphDevice.hpp"
#include "../Private/GraphPass.hpp"
#include "../Private/GSync.hpp"
#include <EASTL/tuple.h>
#include <EASTL/type_traits.h>
#include <cstring>

namespace GVM::Core
{
    namespace Private { class TaskExecutionState; }
    template <class T> class ReadbackNode;

    /// Builds one invocation-local subgraph; configuration objects never store a current context or encoder.
    /// Native C++ builders receive this value by reference; the DSL supplies it as a hidden build parameter.
    class RenderNode final
    {
    public:
        /// Releases an unsubmitted fragment or transfers its resource ownership to the runtime.
        ~RenderNode();
        RenderNode(const RenderNode &) = delete;
        RenderNode &operator=(const RenderNode &) = delete;
        /// Allocates graph-local storage using the existing RHI handle; the execution owns its lifetime.
        RHI::Buffer createBuffer(const RHI::BufferDescriptor &descriptor);
        /// Captures a complete ordered list of dispatches as one independently recordable compute pass.
        void computePass(const eastl::string &label, eastl::vector<ComputePassTaskDescriptor> tasks);
        /// Captures a framebuffer and draw list; distinct graphics passes remain GPU-ordered.
        void renderPass(RHI::RenderPassDescriptor descriptor, eastl::vector<RenderPassTaskDescriptor> tasks);
        /// Copies a graph byte range as its own transfer pass without touching queue pending lists.
        void copyBuffer(RHI::BufferRange source, RHI::BufferRange destination);
        /// Fills a graph byte range as its own transfer pass; ranges must be four-byte aligned.
        void fillBuffer(RHI::BufferRange target, uint8_t value);

        /// Packs ordinary dispatch descriptors into one logical compute pass.
        template <class... Tasks>
            requires((eastl::is_same_v<Tasks, ComputePassTaskDescriptor>) && ...)
        void computePass(const eastl::string &label, Tasks... tasks)
        {
            computePass(label, eastl::vector<ComputePassTaskDescriptor>{eastl::move(tasks)...});
        }
        /// Captures a generated framebuffer and its ordinary draw descriptors by value.
        template <class Framebuffer, class... Tasks>
            requires((eastl::is_same_v<Tasks, RenderPassTaskDescriptor>) && ...)
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
            renderPass(eastl::move(descriptor), {eastl::move(tasks)...});
        }
        /// Copies owned CPU elements into a retained staging allocation and emits a transfer pass.
        template <class T>
        void writeBuffer(RHI::BufferRange destination, const eastl::vector<T> &values)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "GPU uploads require trivially copyable elements.");
            upload(eastl::move(destination), values.data(), values.size() * sizeof(T));
        }
        /// Creates an immutable Args shader dispatch from creation inputs followed by three thread counts.
        template <class Shader, class... Arguments>
        ComputePassTaskDescriptor dispatchThreads(Arguments... arguments)
        {
            static_assert(sizeof...(Arguments) >= 3, "Dispatch requires three thread counts.");
            auto values = eastl::make_tuple(eastl::move(arguments)...);
            return dispatch<Shader>(values, eastl::make_index_sequence<sizeof...(Arguments) - 3>{});
        }

    private:
        friend class Private::TaskExecutionState;
        template <class T> friend class ReadbackNode;
        /// Starts an isolated invocation with a finite pass budget and a borrowed live device domain.
        RenderNode(eastl::shared_ptr<Private::GraphDevice> device, uint32_t maximumPasses);
        /// Transfers the frozen fragment exactly once after build completes successfully.
        Private::RenderNodeFragment finish();
        /// Enforces the runtime's per-execution pass limit before appending another operation.
        void append(Private::GraphPass pass);
        /// Creates a private allocation owner retained by this invocation and its execution.
        eastl::shared_ptr<Private::GraphBufferAllocation> allocateBuffer(const RHI::BufferDescriptor &descriptor);
        /// Snapshots host bytes synchronously, then captures an upload pass.
        void upload(RHI::BufferRange destination, const void *data, uint64_t size);
        /// Emits an owned staging copy for a typed readback node.
        eastl::shared_ptr<Private::GraphBufferAllocation> prepareReadback(RHI::BufferRange source);
        /// Publishes a typed CPU resolver that runs only after this fragment's GPU completion.
        void resolveAfterGpu(eastl::function<eastl::any()> resolver);
        /// Constructs shader objects under the graph device lock while retaining immutable argument snapshots.
        template <class Shader, class Tuple, size_t... Indices>
        ComputePassTaskDescriptor dispatch(Tuple &values, eastl::index_sequence<Indices...>)
        {
            auto lock = mDevice->lock();
            Shader shader(&mDevice->device(), eastl::get<Indices>(values)...);
            return shader.dispatchThreads(eastl::get<Indices>(values)...,
                eastl::get<sizeof...(Indices)>(values), eastl::get<sizeof...(Indices) + 1>(values),
                eastl::get<sizeof...(Indices) + 2>(values));
        }
        eastl::shared_ptr<Private::GraphDevice> mDevice;
        Private::RenderNodeFragment mFragment;
        uint32_t mMaximumPasses;
    };
}
