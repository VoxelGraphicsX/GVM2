#pragma once
#include "GSync.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include "GReadback.hpp"
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

        /** Encodes an ordinary task after phase counting has checked its phase requirement. */
        inline void encodeRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state, const RenderPassTaskDescriptor &task)
        {
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
    /**
     * Records render, compute and blit passes onto one RHI queue and executes them together with the
     * buffer/texture transfer methods. Queued writes are hoisted ahead of every pass of the next
     * submit and execute in call order among themselves; queued reads resolve after that submission
     * completes. If any pass-recording call throws, every pass recorded on this proxy since the last
     * submit is discarded without being submitted, the proxy stays usable for further recording, and
     * the original exception is rethrown.
     */
    class QueueProxyImpl final : public GVM::RHI::RefCountedObject
    {
    private:
        GVM::RHI::Queue mQueue;
        GVM::RHI::CommandEncoder mCommandEncoder;
        GVM::RHI::Logger mLogger;

        /** Ends and submits the pending command encoder, if one is currently recording. */
        void submitRecordedPasses();

        /**
         * Discards the pending command encoder without submitting it after a pass-recording call
         * failed. Ends the still-open pass encoder first so no backend encoder stays open; errors
         * raised while ending the failed pass are swallowed because the original exception is
         * rethrown by the caller. The proxy remains usable for further recording.
         */
        template <class TPassEncoder>
        void discardRecordedPasses(TPassEncoder &openPass) noexcept
        {
            if (openPass != nullptr)
            {
                try
                {
                    openPass->end();
                }
                catch (...)
                {
                }
            }
            mCommandEncoder = nullptr;
        }

        /**
         * Records one render pass from task phases; a null timestampScope leaves timestamp and
         * counter writes disabled. Any exception discards the pending encoder and is rethrown.
         */
        template <typename T, class... Args>
            requires((... && Private::RenderPassPhase<Args>))
        QueueProxyImpl *renderPassImpl(const eastl::string &label, const T &framebuffer,
            const GVM::RHI::GpuTimestampFrameProfiler::Scope *timestampScope, Args &&...args)
        {
            GVM::RHI::RenderPassEncoder renderpassEncoder;
            try
            {
                if (mCommandEncoder == nullptr)
                {
                    mCommandEncoder = mQueue->createCommandEncoder();
                }
                auto renderpassDesp = framebuffer.getRenderPassDescriptor();
                renderpassDesp.label = label;
                if (timestampScope != nullptr)
                {
                    renderpassDesp.timestampWrites = timestampScope->timestampWrites;
                    renderpassDesp.counterWrites = timestampScope->counterWrites;
                }
                const Private::RenderPassPhaseCounter phaseCounter = Private::resolveRenderPassPhaseCounter(args...);
                renderpassDesp.pixelLocal.enabled = phaseCounter.hasPixelLocalPhase;
                renderpassDesp.pixelLocal.passCount = phaseCounter.phaseCount;
                renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
                Private::RenderPassPhaseState phaseState = {};
                (Private::encodeRenderPassPhase(renderpassEncoder, phaseState, args), ...);
                renderpassEncoder->end();
                renderpassEncoder = nullptr;
            }
            catch (...)
            {
                discardRecordedPasses(renderpassEncoder);
                throw;
            }
            return this;
        }

        /**
         * Records one render pass from a task range; a null timestampScope leaves timestamp and
         * counter writes disabled. Any exception discards the pending encoder and is rethrown.
         */
        template <typename T>
        QueueProxyImpl *renderPassImpl(const eastl::string &label, const T &framebuffer,
            const GVM::RHI::GpuTimestampFrameProfiler::Scope *timestampScope,
            const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            GVM::RHI::RenderPassEncoder renderpassEncoder;
            try
            {
                Private::validateOrdinaryRenderPassTaskRange(tasks, "QueueProxyImpl::renderPass");
                if (mCommandEncoder == nullptr)
                {
                    mCommandEncoder = mQueue->createCommandEncoder();
                }
                auto renderpassDesp = framebuffer.getRenderPassDescriptor();
                renderpassDesp.label = label;
                if (timestampScope != nullptr)
                {
                    renderpassDesp.timestampWrites = timestampScope->timestampWrites;
                    renderpassDesp.counterWrites = timestampScope->counterWrites;
                }
                renderpassEncoder = mCommandEncoder->beginRenderPass(renderpassDesp);
                for (const auto &task : tasks)
                {
                    task.drawFn(renderpassEncoder);
                }
                renderpassEncoder->end();
                renderpassEncoder = nullptr;
            }
            catch (...)
            {
                discardRecordedPasses(renderpassEncoder);
                throw;
            }
            return this;
        }

        /**
         * Records one compute pass from a task range; a null timestampScope leaves timestamp and
         * counter writes disabled. Any exception discards the pending encoder and is rethrown.
         */
        QueueProxyImpl *computePassImpl(const eastl::string &label,
            const GVM::RHI::GpuTimestampFrameProfiler::Scope *timestampScope,
            const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            GVM::RHI::ComputePassEncoder computePassEncoder;
            try
            {
                if (mCommandEncoder == nullptr)
                {
                    mCommandEncoder = mQueue->createCommandEncoder();
                }

                GVM::RHI::ComputePassDescriptor descriptor = {};
                descriptor.label = label;
                if (timestampScope != nullptr)
                {
                    descriptor.timestampWrites = timestampScope->timestampWrites;
                    descriptor.counterWrites = timestampScope->counterWrites;
                }
                computePassEncoder = mCommandEncoder->beginComputePass(descriptor);
                for (const auto &task : tasks)
                {
                    task.dispatchFn(computePassEncoder);
                }
                computePassEncoder->end();
                computePassEncoder = nullptr;
            }
            catch (...)
            {
                discardRecordedPasses(computePassEncoder);
                throw;
            }
            return this;
        }

        /**
         * Records one blit pass from a task range; a null timestampScope leaves timestamp and
         * counter writes disabled. Any exception discards the pending encoder and is rethrown.
         */
        QueueProxyImpl *blitPassImpl(const eastl::string &label,
            const GVM::RHI::GpuTimestampFrameProfiler::Scope *timestampScope,
            const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            GVM::RHI::BlitPassEncoder blitPassEncoder;
            try
            {
                if (mCommandEncoder == nullptr)
                {
                    mCommandEncoder = mQueue->createCommandEncoder();
                }

                GVM::RHI::BlitPassDescriptor descriptor = {};
                descriptor.label = label;
                if (timestampScope != nullptr)
                {
                    descriptor.timestampWrites = timestampScope->timestampWrites;
                    descriptor.counterWrites = timestampScope->counterWrites;
                }
                blitPassEncoder = mCommandEncoder->beginBlitPass(descriptor);
                for (const auto &task : tasks)
                {
                    task.blitFn(blitPassEncoder);
                }
                blitPassEncoder->end();
                blitPassEncoder = nullptr;
            }
            catch (...)
            {
                discardRecordedPasses(blitPassEncoder);
                throw;
            }
            return this;
        }

    public:
        QueueProxyImpl(GVM::RHI::Queue queue, GVM::RHI::Logger logger = nullptr);
        QueueProxyImpl(GVM::RHI::Device device, GVM::RHI::Queue queue, GVM::RHI::Logger logger = nullptr);

        template <typename T, class... Args>
            requires((... && Private::RenderPassPhase<Args>))
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, Args &&...args)
        {
            return renderPassImpl(label, framebuffer, nullptr, args...);
        }
        template <typename T, class... Args>
            requires((... && Private::RenderPassPhase<Args>))
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return renderPassImpl(label, framebuffer, &timestampScope, args...);
        }
        template <typename T>
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            return renderPassImpl(label, framebuffer, nullptr, tasks);
        }
        template <typename T>
        QueueProxyImpl *renderPass(const eastl::string &label, const T &framebuffer, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<RenderPassTaskDescriptor> auto &tasks)
        {
            return renderPassImpl(label, framebuffer, &timestampScope, tasks);
        }

        template <class... Args>
            requires((... && std::is_same_v<Args, ComputePassTaskDescriptor>))
        QueueProxyImpl *computePass(const eastl::string &label, Args &&...args)
        {
            return computePassImpl(label, nullptr, eastl::vector<ComputePassTaskDescriptor>{args...});
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, ComputePassTaskDescriptor>))
        QueueProxyImpl *computePass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return computePassImpl(label, &timestampScope, eastl::vector<ComputePassTaskDescriptor>{args...});
        }

        QueueProxyImpl *computePass(const eastl::string &label, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            return computePassImpl(label, nullptr, tasks);
        }
        QueueProxyImpl *computePass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<ComputePassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            return computePassImpl(label, &timestampScope, tasks);
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, BlitPassTaskDescriptor>))
        QueueProxyImpl *blitPass(const eastl::string &label, Args &&...args)
        {
            return blitPassImpl(label, nullptr, eastl::vector<BlitPassTaskDescriptor>{args...});
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, BlitPassTaskDescriptor>))
        QueueProxyImpl *blitPass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, Args &&...args)
        {
            return blitPassImpl(label, &timestampScope, eastl::vector<BlitPassTaskDescriptor>{args...});
        }

        QueueProxyImpl *blitPass(const eastl::string &label, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            return blitPassImpl(label, nullptr, tasks);
        }
        QueueProxyImpl *blitPass(const eastl::string &label, const GVM::RHI::GpuTimestampFrameProfiler::Scope &timestampScope, const Private::TaskArrayRange<BlitPassTaskDescriptor> auto &tasks)
        {
            if (tasks.empty())
            {
                return this;
            }
            return blitPassImpl(label, &timestampScope, tasks);
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueProxyImpl *renderToSwapchain(const GVM::RHI::SwapchainQueryResult &swapchainResult,
                                          GVM::RHI::Texture sourceTexture,
                                          const GVM::RHI::RenderToSwapchainDescriptor &descriptor,
                                          Args &&...args)
        {
            GVM::RHI::RenderPassEncoder renderPassEncoder;
            try
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

                renderPassEncoder = mCommandEncoder->beginRenderPass(renderPassDescriptor);
                renderPassEncoder->drawFullscreenTexture(sourceTexture, descriptor);
                for (const auto &task : renderPassTasks)
                {
                    task.drawFn(renderPassEncoder);
                }
                renderPassEncoder->end();
                renderPassEncoder = nullptr;
            }
            catch (...)
            {
                discardRecordedPasses(renderPassEncoder);
                throw;
            }
            return this;
        }
        template <class... Args>
            requires((... && std::is_same_v<Args, RenderPassTaskDescriptor>))
        QueueProxyImpl *renderToSwapchain(const GVM::RHI::SwapchainQueryResult &swapchainResult, GVM::RHI::Texture sourceTexture, Args &&...args)
        {
            return renderToSwapchain(swapchainResult, sourceTexture, GVM::RHI::RenderToSwapchainDescriptor{}, args...);
        }
        /**
         * Queues an upload of `size` bytes into `buffer`. The write is hoisted ahead of every pass
         * of the next `submit()` and executes in call order relative to the other queued writes.
         * Returns `this` for chaining.
         */
        QueueProxyImpl *writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size);
        /**
         * Queues a readback of `size` bytes from `buffer` into `data`. The read resolves after the
         * next `submit()` executes it, so `data` is not valid until that submission completes.
         * Returns `this` for chaining.
         */
        [[deprecated("Use readBufferAsync/readTextureAsync (GpuReadback): the destination pointer must outlive the executing submission")]] QueueProxyImpl *readBuffer(GVM::RHI::BufferRange buffer, void *data, uint64_t size);
        /**
         * Queues an upload of one trivially copyable value, such as a struct or a fixed-size array, into
         * `buffer`; `sizeof(T)` bytes are copied before the call returns. Returns `this` for chaining.
         */
        template <class T>
            requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>)
        QueueProxyImpl *writeBuffer(GVM::RHI::BufferRange buffer, const T &value)
        {
            return writeBuffer(buffer, &value, sizeof(T));
        }
        /**
         * Queues a readback of `sizeof(T)` bytes from `buffer` into `destination`, a trivially copyable
         * value such as a struct or a fixed-size array. `destination` must stay alive until the next
         * `submit()` that executes the read completes. Returns `this` for chaining.
         */
        template <class T>
            requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && !std::is_const_v<T>)
        QueueProxyImpl *readBuffer(GVM::RHI::BufferRange buffer, T &destination)
        {
            return readBuffer(buffer, &destination, sizeof(T));
        }
        /// Queues a buffer readback executed by the next submission; the returned GpuReadback becomes ready once that submission completes.
        template <class T>
        GpuReadback<T> readBufferAsync(GVM::RHI::BufferRange source)
        {
            return GpuReadback<T>(mQueue->readBufferAsync(source));
        }
        /// Queues a texture readback executed by the next submission; the returned GpuReadback becomes ready once that submission completes.
        GpuReadback<uint8_t> readTextureAsync(GVM::RHI::Texture source, uint32_t miplevelOffset = 0, uint32_t arrayLayerOffset = 0);
        /**
         * Queues a fill of `source` with `data`. The fill is hoisted ahead of every pass of the
         * next `submit()` and executes in call order relative to the other queued writes.
         * Returns `this` for chaining.
         */
        QueueProxyImpl *fillBuffer(GVM::RHI::BufferRange source, uint8_t value);
        /**
         * Queues an upload of `dataSize` bytes into `destination`. The write is hoisted ahead of
         * every pass of the next `submit()` and executes in call order relative to the other
         * queued writes. Returns `this` for chaining.
         */
        QueueProxyImpl *writeTexture(GVM::RHI::Texture destination, void const *data, uint64_t dataSize, uint32_t miplevelOffset = 0, uint32_t arrayLayerOffset = 0);
        /**
         * Queues a readback of `dataSize` bytes from `source` into `data`. The read resolves after
         * the next `submit()` executes it, so `data` is not valid until that submission completes.
         * Returns `this` for chaining.
         */
        [[deprecated("Use readBufferAsync/readTextureAsync (GpuReadback): the destination pointer must outlive the executing submission")]] QueueProxyImpl *readTexture(GVM::RHI::Texture source, void *data, uint64_t dataSize, uint32_t miplevelOffset = 0, uint32_t arrayLayerOffset = 0);
        QueueProxyImpl *resolveTimestampProfiler(GVM::RHI::GpuTimestampFrameProfiler &profiler);
        QueueProxyImpl *resolvePassCounterProfiler(GVM::RHI::GpuPassCounterFrameProfiler &profiler);

        void submit();
    };

    BlitPassTaskDescriptor fillBuffer(GVM::RHI::BufferRange source, uint8_t value);
    // BlitPassTaskDescriptor writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size);
    BlitPassTaskDescriptor copyBufferToBuffer(GVM::RHI::Buffer src, uint64_t srcOffsetBytes, GVM::RHI::Buffer dst, uint64_t dstOffsetBytes, uint64_t copyBytes);

    using QueueProxy = eastl::intrusive_ptr<QueueProxyImpl>;


} // namespace GVM::Core
