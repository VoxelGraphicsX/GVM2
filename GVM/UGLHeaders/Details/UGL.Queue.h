#pragma once
#include "UGL.Resources.h"
#include "UGL.Swapchain.h"
#include "UGL.Sync.h"
#include <type_traits>
namespace UGL
{
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
        QueueImpl *readBuffer(IsCopySrcBuffer auto buffer, void *data, uint64_t size)
        {
            return this;
        }
        QueueImpl *fillBuffer(IsCopyDstBuffer auto buffer, uint32_t data)
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
        QueueImpl *readTexture(IsCopySrcTexture auto source, void *data, uint64_t dataSize, uint32_t mipLevelOffset = 0, uint32_t arrayLayerOffset = 0)
        {
            return this;
        }
    };

    BlitPassTaskDescriptor fillBuffer(IsCopyDstBuffer auto buffer, uint32_t data)
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
