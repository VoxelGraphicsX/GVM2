#pragma once
#include "GSync.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
namespace GVM::Core
{
    namespace Private
    {
        /// Tracks the top-level render pass phase while expanding ordinary render tasks and pixel-local task groups.
        struct RenderPassPhaseState
        {
            bool hasEncodedPhase = false;
            bool currentPhaseIsOrdinary = false;
            uint32_t phaseCount = 0u;
        };

        /// Counts top-level render pass phases while folding adjacent ordinary render tasks into one phase.
        struct RenderPassPhaseCounter
        {
            uint32_t phaseCount = 0u;
            bool currentPhaseIsOrdinary = false;
            bool hasPixelLocalPhase = false;
        };

        inline uint32_t getNonEmptyPixelLocalPassCount(const PixelLocalPassTaskDescriptor &tasks)
        {
            uint32_t nonEmptyPassCount = 0u;
            for (const auto &pass : tasks.pixelLocalPasses)
            {
                if (!pass.empty())
                {
                    ++nonEmptyPassCount;
                }
            }
            return nonEmptyPassCount;
        }

        inline void validateOrdinaryRenderPassTask(const RenderPassTaskDescriptor &task, const char *context)
        {
            if (task.phaseRequirement == RenderPassPhaseRequirement::PixelLocalOnly)
            {
                throw std::invalid_argument(std::string(context) + " cannot execute a pixel-local-only RenderPassTaskDescriptor outside pixelLocalPass(...).");
            }
        }

        template <typename TTasks>
        void validateOrdinaryRenderPassTaskRange(const TTasks &tasks, const char *context)
        {
            for (const auto &task : tasks)
            {
                validateOrdinaryRenderPassTask(task, context);
            }
        }

        inline void beginOrdinaryRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state)
        {
            if (state.hasEncodedPhase && state.currentPhaseIsOrdinary)
            {
                return;
            }
            if (state.hasEncodedPhase)
            {
                renderpassEncoder->nextPixelLocalPass();
            }
            state.hasEncodedPhase = true;
            state.currentPhaseIsOrdinary = true;
            ++state.phaseCount;
        }

        inline void beginPixelLocalRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state)
        {
            if (state.hasEncodedPhase)
            {
                renderpassEncoder->nextPixelLocalPass();
            }
            state.hasEncodedPhase = true;
            state.currentPhaseIsOrdinary = false;
            ++state.phaseCount;
        }

        inline void encodeRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state, const RenderPassTaskDescriptor &task)
        {
            validateOrdinaryRenderPassTask(task, "QueueProxyImpl::renderPass");
            beginOrdinaryRenderPassPhase(renderpassEncoder, state);
            task.drawFn(renderpassEncoder);
        }

        inline void encodeRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state, const PixelLocalPassTaskDescriptor &tasks)
        {
            for (const auto &pass : tasks.pixelLocalPasses)
            {
                if (pass.empty())
                {
                    continue;
                }
                beginPixelLocalRenderPassPhase(renderpassEncoder, state);
                for (const auto &task : pass)
                {
                    task.drawFn(renderpassEncoder);
                }
            }
        }

        inline void countRenderPassPhase(RenderPassPhaseCounter &counter, const RenderPassTaskDescriptor &task)
        {
            validateOrdinaryRenderPassTask(task, "QueueProxyImpl::renderPass");
            if (!counter.currentPhaseIsOrdinary)
            {
                ++counter.phaseCount;
                counter.currentPhaseIsOrdinary = true;
            }
        }

        inline void countRenderPassPhase(RenderPassPhaseCounter &counter, const PixelLocalPassTaskDescriptor &tasks)
        {
            const uint32_t nonEmptyPassCount = getNonEmptyPixelLocalPassCount(tasks);
            if (nonEmptyPassCount == 0u)
            {
                return;
            }
            counter.phaseCount += nonEmptyPassCount;
            counter.currentPhaseIsOrdinary = false;
            counter.hasPixelLocalPhase = true;
        }

        template <class... Args>
        RenderPassPhaseCounter resolveRenderPassPhaseCounter(const Args &...args)
        {
            RenderPassPhaseCounter counter = {};
            (countRenderPassPhase(counter, args), ...);
            if (counter.phaseCount == 0u)
            {
                counter.phaseCount = 1u;
            }
            return counter;
        }
    }
    class QueueProxyImpl final : public GVM::RHI::RefCountedObject
    {
    private:
        GVM::RHI::Queue mQueue;
        GVM::RHI::CommandEncoder mCommandEncoder;
        GVM::RHI::Logger mLogger;

    public:
        QueueProxyImpl(GVM::RHI::Queue queue, GVM::RHI::Logger logger = nullptr);
        QueueProxyImpl(GVM::RHI::Device device, GVM::RHI::Queue queue, GVM::RHI::Logger logger = nullptr);

        template <typename T, class... Args>
            requires((... && Private::RenderPassPhase<Args>))
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }
            auto renderpassDesp = framebuffer.getRenderPassDescriptor();
            renderpassDesp.label = label;
            const Private::RenderPassPhaseCounter phaseCounter = Private::resolveRenderPassPhaseCounter(args...);
            renderpassDesp.pixelLocal.enabled = phaseCounter.hasPixelLocalPhase;
            renderpassDesp.pixelLocal.passCount = phaseCounter.phaseCount;
            auto renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
            Private::RenderPassPhaseState phaseState = {};
            (Private::encodeRenderPassPhase(renderpassEncoder, phaseState, args), ...);
            renderpassEncoder->end();
            return this;
        }
        template <typename T, class... Args>
            requires((... && Private::RenderPassPhase<Args>))
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }
            auto renderpassDesp = framebuffer.getRenderPassDescriptor();
            renderpassDesp.label = label;
            renderpassDesp.timestampWrites = timestampScope.timestampWrites;
            renderpassDesp.counterWrites = timestampScope.counterWrites;
            const Private::RenderPassPhaseCounter phaseCounter = Private::resolveRenderPassPhaseCounter(args...);
            renderpassDesp.pixelLocal.enabled = phaseCounter.hasPixelLocalPhase;
            renderpassDesp.pixelLocal.passCount = phaseCounter.phaseCount;
            auto renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
            Private::RenderPassPhaseState phaseState = {};
            (Private::encodeRenderPassPhase(renderpassEncoder, phaseState, args), ...);
            renderpassEncoder->end();
            return this;
        }
        template <typename T>
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            Private::validateOrdinaryRenderPassTaskRange(tasks, "QueueProxyImpl::renderPass");
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }
            auto renderpassDesp = framebuffer.getRenderPassDescriptor();
            renderpassDesp.label = label;
            auto renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
            for (const auto &task : tasks)
            {
                task.drawFn(renderpassEncoder);
            }
            renderpassEncoder->end();
            return this;
        }
        template <typename T>
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            Private::validateOrdinaryRenderPassTaskRange(tasks, "QueueProxyImpl::renderPass");
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }
            auto renderpassDesp = framebuffer.getRenderPassDescriptor();
            renderpassDesp.label = label;
            renderpassDesp.timestampWrites = timestampScope.timestampWrites;
            renderpassDesp.counterWrites = timestampScope.counterWrites;
            auto renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
            for (const auto &task : tasks)
            {
                task.drawFn(renderpassEncoder);
            }
            renderpassEncoder->end();
            return this;
        }

        template <class... Args>
            requires((... && std::is_same_v<Args, ComputePassTaskDescriptor>))
        QueueProxyImpl *computePass(const eastl::string &label, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::ComputePassDescriptor descriptor = {};
            descriptor.label = label;
            auto computePassEncoder = mCommandEncoder->beginComputePass(descriptor);

            eastl::vector<ComputePassTaskDescriptor> computePassTasks = {args...};
            for (auto &task : computePassTasks)
            {
                task.dispatchFn(computePassEncoder);
            }
            computePassEncoder->end();
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, ComputePassTaskDescriptor>))
        QueueProxyImpl *computePass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::ComputePassDescriptor descriptor = {};
            descriptor.label = label;
            descriptor.timestampWrites = timestampScope.timestampWrites;
            descriptor.counterWrites = timestampScope.counterWrites;
            auto computePassEncoder = mCommandEncoder->beginComputePass(descriptor);

            eastl::vector<ComputePassTaskDescriptor> computePassTasks = {args...};
            for (auto &task : computePassTasks)
            {
                task.dispatchFn(computePassEncoder);
            }
            computePassEncoder->end();
            return this;
        }

        QueueProxyImpl *computePass(const eastl::string &label, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::ComputePassDescriptor descriptor = {};
            descriptor.label = label;
            auto computePassEncoder = mCommandEncoder->beginComputePass(descriptor);
            for (const auto &task : tasks)
            {
                task.dispatchFn(computePassEncoder);
            }
            computePassEncoder->end();
            return this;
        }
        QueueProxyImpl *computePass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::ComputePassDescriptor descriptor = {};
            descriptor.label = label;
            descriptor.timestampWrites = timestampScope.timestampWrites;
            descriptor.counterWrites = timestampScope.counterWrites;
            auto computePassEncoder = mCommandEncoder->beginComputePass(descriptor);
            for (const auto &task : tasks)
            {
                task.dispatchFn(computePassEncoder);
            }
            computePassEncoder->end();
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, BlitPassTaskDescriptor>))
        QueueProxyImpl *blitPass(const eastl::string &label, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::BlitPassDescriptor descriptor = {};
            descriptor.label = label;
            auto blitPassEncoder = mCommandEncoder->beginBlitPass(descriptor);
            eastl::vector<BlitPassTaskDescriptor> blitPassTasks = {args...};
            for (auto &task : blitPassTasks)
            {
                task.blitFn(blitPassEncoder);
            }
            blitPassEncoder->end();
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, BlitPassTaskDescriptor>))
        QueueProxyImpl *blitPass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::BlitPassDescriptor descriptor = {};
            descriptor.label = label;
            descriptor.timestampWrites = timestampScope.timestampWrites;
            descriptor.counterWrites = timestampScope.counterWrites;
            auto blitPassEncoder = mCommandEncoder->beginBlitPass(descriptor);
            eastl::vector<BlitPassTaskDescriptor> blitPassTasks = {args...};
            for (auto &task : blitPassTasks)
            {
                task.blitFn(blitPassEncoder);
            }
            blitPassEncoder->end();
            return this;
        }

        QueueProxyImpl *blitPass(const eastl::string &label, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::BlitPassDescriptor descriptor = {};
            descriptor.label = label;
            auto blitPassEncoder = mCommandEncoder->beginBlitPass(descriptor);
            for (const auto &task : tasks)
            {
                task.blitFn(blitPassEncoder);
            }
            blitPassEncoder->end();
            return this;
        }
        QueueProxyImpl *blitPass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::BlitPassDescriptor descriptor = {};
            descriptor.label = label;
            descriptor.timestampWrites = timestampScope.timestampWrites;
            descriptor.counterWrites = timestampScope.counterWrites;
            auto blitPassEncoder = mCommandEncoder->beginBlitPass(descriptor);
            for (const auto &task : tasks)
            {
                task.blitFn(blitPassEncoder);
            }
            blitPassEncoder->end();
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueProxyImpl *renderToSwapchain(const GVM::RHI::SwapchainQueryResult &swapchainResult,
                                          GVM::RHI::Texture sourceTexture,
                                          const GVM::RHI::RenderToSwapchainDescriptor &descriptor,
                                          Args &&...args)
        {
            if (swapchainResult.status != GVM::RHI::SwapchainNextTextureQueryStatus::Success || swapchainResult.texture.isNull())
            {
                throw std::invalid_argument("QueueProxyImpl::renderToSwapchain requires a successful SwapchainQueryResult with a valid swapchain texture.");
            }
            if (sourceTexture.isNull())
            {
                throw std::invalid_argument("QueueProxyImpl::renderToSwapchain requires a valid source texture.");
            }

            if (mCommandEncoder == nullptr)
            {
                mCommandEncoder = mQueue->createCommandEncoder();
            }

            GVM::RHI::RenderPassDescriptor renderPassDescriptor = {};
            renderPassDescriptor.label = "RenderToSwapchain";
            renderPassDescriptor.colorAttachments.push_back({
                .view = swapchainResult.texture->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 1.0},
            });

            eastl::vector<RenderPassTaskDescriptor> renderPassTasks = {args...};
            Private::validateOrdinaryRenderPassTaskRange(renderPassTasks, "QueueProxyImpl::renderToSwapchain");

            auto renderPassEncoder = mCommandEncoder->beginRenderPass(renderPassDescriptor);
            renderPassEncoder->drawFullscreenTexture(sourceTexture, descriptor);
            for (const auto &task : renderPassTasks)
            {
                task.drawFn(renderPassEncoder);
            }
            renderPassEncoder->end();
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueProxyImpl *renderToSwapchain(const GVM::RHI::SwapchainQueryResult &swapchainResult, GVM::RHI::Texture sourceTexture, Args &&...args)
        {
            return renderToSwapchain(swapchainResult, sourceTexture, GVM::RHI::RenderToSwapchainDescriptor{}, args...);
        }
        QueueProxyImpl *writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size);
        QueueProxyImpl *readBuffer(GVM::RHI::BufferRange buffer, void *data, uint64_t size);
        QueueProxyImpl *fillBuffer(GVM::RHI::BufferRange source, uint32_t data);
        QueueProxyImpl *writeTexture(GVM::RHI::Texture destination, void const *data, uint64_t dataSize, uint32_t miplevelOffset = 0, uint32_t arrayLayerOffset = 0);
        QueueProxyImpl *readTexture(GVM::RHI::Texture source, void *data, uint64_t dataSize, uint32_t miplevelOffset = 0, uint32_t arrayLayerOffset = 0);
        QueueProxyImpl *resolveTimestampProfiler(GVM::RHI::GpuTimestampFrameProfiler &profiler);
        QueueProxyImpl *resolvePassCounterProfiler(GVM::RHI::GpuPassCounterFrameProfiler &profiler);

        void submit();
    };

    BlitPassTaskDescriptor fillBuffer(GVM::RHI::BufferRange source, uint32_t data);
    // BlitPassTaskDescriptor writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size);
    BlitPassTaskDescriptor copyBufferToBuffer(GVM::RHI::Buffer src, uint64_t srcOffsetBytes, GVM::RHI::Buffer dst, uint64_t dstOffsetBytes, uint64_t copyBytes);

    using QueueProxy = eastl::intrusive_ptr<QueueProxyImpl>;


} // namespace GVM::Core
