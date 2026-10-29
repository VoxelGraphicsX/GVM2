#pragma once
#include "UGL.Resources.h"
#include "UGL.Swapchain.h"
#include "UGL.Sync.h"
#include <type_traits>
namespace UGL
{
    /// Typed view over one queued GPU readback in the DSL; isReady()/wait() mirror the host GpuReadback lifecycle.
    template <class T>
    class GpuReadback final
    {
    public:
        /// Reports whether the readback bytes are available.
        bool isReady() const
        {
            return false;
        }
        /// Blocks until the executing submission completes.
        void wait()
        {
        }
        /// Returns the readback bytes reinterpreted as a single T.
        const T &get() const
        {
            static const T empty = {};
            return empty;
        }
        /// Returns the readback bytes reinterpreted as a span of T.
        std::span<const T> elements() const
        {
            return {};
        }
    };

    class QueueImpl final
    {
    public:
        template <class T>
            requires(std::is_base_of_v<IFrameBuffer, T>)
        QueueImpl *renderPass(string name, const T &framebuffer, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            return this;
        }

        template <class T>
            requires(std::is_base_of_v<IFrameBuffer, T>)
        QueueImpl *renderPass(string name, const T &framebuffer, const GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            return this;
        }

        template <typename T, class... Args>
            requires(std::is_base_of_v<IFrameBuffer, T> && (... && Private::RenderPassPhase<Args>))
        QueueImpl *renderPass(string name, const T &framebuffer, Args &&...args)
        {
            return this;
        }

        template <typename T, class... Args>
            requires(std::is_base_of_v<IFrameBuffer, T> && (... && Private::RenderPassPhase<Args>))
        QueueImpl *renderPass(string name, const T &framebuffer, const GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return this;
        }

        template <class... Args>
            requires(... && std::is_same_v<Args, ComputePassTaskDescriptor>)
        QueueImpl *computePass(string name, Args &&...args)
        {
            return this;
        }
        template <class... Args>
            requires(... && std::is_same_v<Args, ComputePassTaskDescriptor>)
        QueueImpl *computePass(string name, const GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return this;
        }
        QueueImpl *computePass(string name, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            return this;
        }
        QueueImpl *computePass(string name, const GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            return this;
        }

        template <class... Args>
            requires(... && std::is_same_v<Args, BlitPassTaskDescriptor>)
        QueueImpl *blitPass(string name, Args &&...args)
        {
            return this;
        }

        template <class... Args>
            requires(... && std::is_same_v<Args, BlitPassTaskDescriptor>)
        QueueImpl *blitPass(string name, const GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return this;
        }

        QueueImpl *blitPass(string name, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            return this;
        }

        QueueImpl *blitPass(string name, const GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            return this;
        }

        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueImpl *renderToSwapchain(const SwapchainQueryResult &swapchainResult,
                                     SourceTexture sourceTexture,
                                     const RenderToSwapchainDescriptor &descriptor,
                                     Args &&...args)
        {
            return this;
        }

        template <class SourceTexture, class... Args>
            requires(IsSampledTexture<SourceTexture> && IsTextureDimension2D<SourceTexture> && (... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueImpl *renderToSwapchain(const SwapchainQueryResult &swapchainResult, SourceTexture sourceTexture, Args &&...args)
        {
            return renderToSwapchain(swapchainResult, sourceTexture, RenderToSwapchainDescriptor{}, args...);
        }

        QueueImpl *writeBuffer(IsCopyDstBuffer auto buffer, void const *data, uint64_t size)
        {
            return this;
        }
        [[deprecated("Use readBufferAsync/readTextureAsync (GpuReadback): the destination pointer must outlive the executing submission")]] QueueImpl *readBuffer(IsCopySrcBuffer auto buffer, void *data, uint64_t size)
        {
            return this;
        }
        /** Queues an upload of one trivially copyable value, such as a struct or a fixed-size array; sizeof(T) bytes are copied before the call returns. */
        template <class T>
            requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>)
        QueueImpl *writeBuffer(IsCopyDstBuffer auto buffer, const T &value)
        {
            return this;
        }
        /** Queues a readback of sizeof(T) bytes into a trivially copyable destination that must stay alive until the next submit executing the read completes. */
        template <class T>
            requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && !std::is_const_v<T>)
        QueueImpl *readBuffer(IsCopySrcBuffer auto buffer, T &destination)
        {
            return this;
        }
        QueueImpl *fillBuffer(IsCopyDstBuffer auto buffer, uint8_t value)
        {
            return this;
        }
        void submit()
        {
        }
        QueueImpl *resolveTimestampProfiler(GpuTimestampFrameProfiler &profiler)
        {
            return this;
        }
        QueueImpl *resolvePassCounterProfiler(GpuPassCounterFrameProfiler &profiler)
        {
            return this;
        }
        QueueImpl *writeTexture(IsCopyDstTexture auto destination, void const *data, uint64_t dataSize, uint32_t mipLevelOffset = 0, uint32_t arrayLayerOffset = 0)
        {
            return this;
        }
        [[deprecated("Use readBufferAsync/readTextureAsync (GpuReadback): the destination pointer must outlive the executing submission")]] QueueImpl *readTexture(IsCopySrcTexture auto source, void *data, uint64_t dataSize, uint32_t mipLevelOffset = 0, uint32_t arrayLayerOffset = 0)
        {
            return this;
        }
        /** Queues a buffer readback executed by the next submission; the returned GpuReadback becomes ready once that submission completes. */
        template <class T>
        GpuReadback<T> readBufferAsync(IsCopySrcBuffer auto source)
        {
            return {};
        }
        /** Queues a texture readback executed by the next submission; the returned GpuReadback becomes ready once that submission completes. */
        GpuReadback<uint8_t> readTextureAsync(IsCopySrcTexture auto source, uint32_t mipLevelOffset = 0, uint32_t arrayLayerOffset = 0)
        {
            return {};
        }
    };

    BlitPassTaskDescriptor fillBuffer(IsCopyDstBuffer auto buffer, uint8_t value)
    {
        return {};
    }
    /* BlitPassTaskDescriptor writeBuffer(IsCopyDstBuffer auto buffer, void const *data, uint64_t size)
    {
        return {};
    } */

    BlitPassTaskDescriptor copyBufferToBuffer(IsCopySrcBuffer auto src, uint64_t srcOffsetBytes, IsCopyDstBuffer auto dst, uint64_t dstOffsetBytes, uint64_t copyBytes)
    {
        return {};
    }

    class Queue
    {
        QueueImpl *impl;

    public:
        QueueImpl *operator->()
        {
            return impl;
        }
    };
} // namespace UGL
