#include "VKTaskDependencyResolver.Diagnostics.hpp"

#include "VKLogging.hpp"
#include "VKQueue.hpp"

#include <GVMRHI/GVMCpuProbe.hpp>

namespace GVM::RHI::Vulkan
{
    void TaskDependencyResolverDiagnostics::reset()
    {
        *this = {};
    }
} // namespace GVM::RHI::Vulkan

namespace GVM::RHI::Vulkan::TaskDependencyResolverDiagnosticUtils
{
    namespace
    {
        constexpr size_t BufferBarrierMemoryBarrierPromotionThreshold = 4u;
    } // namespace

    uint64_t stageMaskBits(vk::PipelineStageFlags flags)
    {
        return static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(flags));
    }

    uint64_t accessMaskBits(vk::AccessFlags flags)
    {
        return static_cast<uint64_t>(static_cast<VkAccessFlags>(flags));
    }

    bool shouldPromoteBufferBarrierGroupToMemoryBarrier(
        vk::PipelineStageFlags srcStageMask,
        vk::PipelineStageFlags dstStageMask,
        vk::AccessFlags srcAccessMask,
        vk::AccessFlags dstAccessMask,
        size_t barrierCount)
    {
        if (barrierCount < BufferBarrierMemoryBarrierPromotionThreshold)
        {
            return false;
        }

        const bool srcHasCompute = static_cast<bool>(srcStageMask & vk::PipelineStageFlagBits::eComputeShader);
        const bool dstHasCompute = static_cast<bool>(dstStageMask & vk::PipelineStageFlagBits::eComputeShader);
        const bool dstHasDrawIndirect = static_cast<bool>(dstStageMask & vk::PipelineStageFlagBits::eDrawIndirect);
        const bool srcHasShaderWrite = static_cast<bool>(srcAccessMask & vk::AccessFlagBits::eShaderWrite);
        const bool dstHasShaderAccess = static_cast<bool>(dstAccessMask & (vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
        const bool dstHasIndirectRead = static_cast<bool>(dstAccessMask & vk::AccessFlagBits::eIndirectCommandRead);

        return (srcHasCompute && dstHasCompute && srcHasShaderWrite && dstHasShaderAccess) ||
            (srcHasCompute && dstHasDrawIndirect && srcHasShaderWrite && dstHasIndirectRead);
    }

    void emitFinalize(
        const VKQueue &queue,
        const TaskDependencyResolverDiagnostics &diagnostics,
        size_t trackedBufferCount,
        size_t trackedTextureCount,
        size_t finalBufferCount,
        size_t finalTextureCount,
        size_t trackedBufferRangeCount)
    {
        GVMLogDebug(
            &queue, LogCategory,
            "event=command_resource_state_tracker_finalize buffer_prepare_calls={} buffer_barriers={} buffer_pipeline_barrier_calls={} buffer_global_memory_barriers={} buffer_promoted_barriers={} buffer_write_barriers={} buffer_outstanding_write_barriers={} buffer_suspicious_repeated_read_barriers={} buffer_outstanding_write_consumed={} texture_prepare_calls={} texture_prepare_subresources={} texture_barriers={} texture_barrier_subresources={} texture_layout_barriers={} texture_stage_barriers={} texture_access_barriers={} texture_write_barriers={} texture_steady_state_restore_calls={} tracked_buffers={} tracked_textures={} final_buffers={} final_textures={} tracked_buffer_ranges={} final_buffer_ranges={}",
            diagnostics.bufferPrepareCalls,
            diagnostics.bufferBarrierCount,
            diagnostics.bufferPipelineBarrierCallCount,
            diagnostics.bufferGlobalMemoryBarrierCount,
            diagnostics.bufferPromotedBarrierCount,
            diagnostics.bufferWriteBarrierCount,
            diagnostics.bufferOutstandingWriteBarrierCount,
            diagnostics.bufferSuspiciousRepeatedReadBarrierCount,
            diagnostics.bufferOutstandingWriteConsumedCount,
            diagnostics.texturePrepareCalls,
            diagnostics.texturePrepareSubresourceCount,
            diagnostics.textureBarrierCount,
            diagnostics.textureBarrierSubresourceCount,
            diagnostics.textureLayoutBarrierCount,
            diagnostics.textureStageBarrierCount,
            diagnostics.textureAccessBarrierCount,
            diagnostics.textureWriteBarrierCount,
            diagnostics.textureSteadyStateRestoreCalls,
            trackedBufferCount,
            trackedTextureCount,
            finalBufferCount,
            finalTextureCount,
            trackedBufferRangeCount,
            finalBufferCount);
        GVMCpuProbeInstantDetail(
            &queue,
            LogCategory,
            "resource_state.finalize",
            "buffer_prepare_calls={} buffer_barriers={} buffer_pipeline_barrier_calls={} buffer_global_memory_barriers={} buffer_promoted_barriers={} buffer_write_barriers={} buffer_outstanding_write_barriers={} buffer_suspicious_repeated_read_barriers={} buffer_outstanding_write_consumed={} texture_prepare_calls={} texture_prepare_subresources={} texture_barriers={} texture_barrier_subresources={} texture_layout_barriers={} texture_stage_barriers={} texture_access_barriers={} texture_write_barriers={} texture_steady_state_restore_calls={} tracked_buffers={} tracked_textures={} final_buffers={} final_textures={} tracked_buffer_ranges={} final_buffer_ranges={}",
            diagnostics.bufferPrepareCalls,
            diagnostics.bufferBarrierCount,
            diagnostics.bufferPipelineBarrierCallCount,
            diagnostics.bufferGlobalMemoryBarrierCount,
            diagnostics.bufferPromotedBarrierCount,
            diagnostics.bufferWriteBarrierCount,
            diagnostics.bufferOutstandingWriteBarrierCount,
            diagnostics.bufferSuspiciousRepeatedReadBarrierCount,
            diagnostics.bufferOutstandingWriteConsumedCount,
            diagnostics.texturePrepareCalls,
            diagnostics.texturePrepareSubresourceCount,
            diagnostics.textureBarrierCount,
            diagnostics.textureBarrierSubresourceCount,
            diagnostics.textureLayoutBarrierCount,
            diagnostics.textureStageBarrierCount,
            diagnostics.textureAccessBarrierCount,
            diagnostics.textureWriteBarrierCount,
            diagnostics.textureSteadyStateRestoreCalls,
            trackedBufferCount,
            trackedTextureCount,
            finalBufferCount,
            finalTextureCount,
            trackedBufferRangeCount,
            finalBufferCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_barrier_count", diagnostics.bufferBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_pipeline_barrier_call_count", diagnostics.bufferPipelineBarrierCallCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_global_memory_barrier_count", diagnostics.bufferGlobalMemoryBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_promoted_barrier_count", diagnostics.bufferPromotedBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_suspicious_repeated_read_barrier_count", diagnostics.bufferSuspiciousRepeatedReadBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.buffer_outstanding_write_consumed_count", diagnostics.bufferOutstandingWriteConsumedCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.texture_barrier_count", diagnostics.textureBarrierCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.texture_barrier_subresource_count", diagnostics.textureBarrierSubresourceCount);
        GVMCpuProbeValueU64(&queue, LogCategory, "vulkan_resource_state.texture_steady_state_restore_count", diagnostics.textureSteadyStateRestoreCalls);
    }
} // namespace GVM::RHI::Vulkan::TaskDependencyResolverDiagnosticUtils
