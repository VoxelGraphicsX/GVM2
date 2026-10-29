#include "VKTaskDependencyResolverDiagnostics.hpp"

#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include <GVMRHI/GVMCpuProbe.hpp>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view LogCategory = "gvmrhi.vulkan.command_resource_state_tracker";
    } // namespace

    void TaskDependencyResolverDiagnostics::emitFinalize(const VKQueue &queue, size_t trackedBuffers, size_t trackedTextures)
    {
#if GVM_LOGGING_ENABLED
        GVMLogDebug(&queue, LogCategory,
            "event=command_resource_state_tracker_finalize buffer_prepare_calls={} buffer_barriers={} buffer_pipeline_barrier_calls={} buffer_write_barriers={} texture_prepare_calls={} texture_prepare_subresources={} texture_barriers={} texture_barrier_subresources={} texture_layout_barriers={} tracked_buffers={} tracked_textures={}",
            mBufferPrepareCalls, mBufferBarrierCount, mBufferPipelineBarrierCallCount, mBufferWriteBarrierCount,
            mTexturePrepareCalls, mTexturePrepareSubresourceCount, mTextureBarrierCount,
            mTextureBarrierSubresourceCount, mTextureLayoutBarrierCount, trackedBuffers, trackedTextures);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_barrier_count", mBufferBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.texture_barrier_count", mTextureBarrierCount);
#else
        (void)queue;
        (void)trackedBuffers;
        (void)trackedTextures;
#endif
        reset();
    }
} // namespace GVM::RHI::Vulkan
