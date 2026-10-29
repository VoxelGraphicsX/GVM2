#pragma once

#include "VKCommon.hpp"

#include <GVMRHI/GVMLogging.hpp>

#include <cstddef>
#include <cstdint>

namespace GVM::RHI::Vulkan
{
    class VKQueue;

    /** CPU-only counters for barriers actually emitted by one native encoder's dependency resolution.
     *  All counters and record methods compile away when GVM_LOGGING_ENABLED=0. */
    class TaskDependencyResolverDiagnostics final
    {
    public:
        /** Resets all counters; called when a new native encoder run starts. */
        void reset() { *this = TaskDependencyResolverDiagnostics{}; }
        /** Records one buffer prepare pass over the tracked buffer set. */
        void recordBufferPrepare()
        {
#if GVM_LOGGING_ENABLED
            ++mBufferPrepareCalls;
#endif
        }
        /** Records one emitted buffer barrier; writes marks the barrier as covering a write access. */
        void recordBufferBarrier(bool writes)
        {
#if GVM_LOGGING_ENABLED
            ++mBufferBarrierCount;
            if (writes)
            {
                ++mBufferWriteBarrierCount;
            }
#else
            (void)writes;
#endif
        }
        /** Records one native pipeline-barrier call emitted for buffers. */
        void recordBufferPipelineBarrierCall()
        {
#if GVM_LOGGING_ENABLED
            ++mBufferPipelineBarrierCallCount;
#endif
        }
        /** Records one texture prepare pass over the tracked texture set. */
        void recordTexturePrepare()
        {
#if GVM_LOGGING_ENABLED
            ++mTexturePrepareCalls;
#endif
        }
        /** Records one prepared texture subresource. */
        void recordTexturePrepareSubresource()
        {
#if GVM_LOGGING_ENABLED
            ++mTexturePrepareSubresourceCount;
#endif
        }
        /** Records one emitted texture barrier subresource; layoutChanged marks an actual layout transition. */
        void recordTextureBarrierSubresource(bool layoutChanged)
        {
#if GVM_LOGGING_ENABLED
            ++mTextureBarrierSubresourceCount;
            if (layoutChanged)
            {
                ++mTextureLayoutBarrierCount;
            }
#else
            (void)layoutChanged;
#endif
        }
        /** Adds count emitted texture barriers to the total. */
        void recordTextureBarriers(size_t count)
        {
#if GVM_LOGGING_ENABLED
            mTextureBarrierCount += count;
#else
            (void)count;
#endif
        }
        /** Emits the per-encoder finalize log and resets the counters for the next run. */
        void emitFinalize(const VKQueue &queue, size_t trackedBuffers, size_t trackedTextures);

    private:
#if GVM_LOGGING_ENABLED
        uint64_t mBufferPrepareCalls = 0;
        uint64_t mBufferBarrierCount = 0;
        uint64_t mBufferPipelineBarrierCallCount = 0;
        uint64_t mBufferWriteBarrierCount = 0;
        uint64_t mTexturePrepareCalls = 0;
        uint64_t mTexturePrepareSubresourceCount = 0;
        uint64_t mTextureBarrierCount = 0;
        uint64_t mTextureBarrierSubresourceCount = 0;
        uint64_t mTextureLayoutBarrierCount = 0;
#endif
    };
} // namespace GVM::RHI::Vulkan
