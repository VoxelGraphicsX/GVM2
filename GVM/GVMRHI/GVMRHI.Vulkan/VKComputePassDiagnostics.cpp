#include "VKComputePassDiagnostics.hpp"

#if GVM_LOGGING_ENABLED

#include "VKDevice.hpp"


namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view ComputePassEncoderLogCategory = "gvmrhi.vulkan.compute_pass_encoder";
    } // namespace

    void VKComputePassDiagnostics::emitEndOfPassLog(
        VKDevice *device,
        const void *commandEncoder,
        vk::CommandBuffer commandBuffer,
        const eastl::string &passLabel,
        size_t bindGroupCount) const
    {
        GVMLogTrace(
            device, ComputePassEncoderLogCategory,
            "event=compute_pass_end command_encoder_ptr={} command_buffer_ptr={} pass_label={} pipeline_switches={} unique_pipelines={} pipeline_labels={} unique_bind_groups={} bind_group_labels={} dispatch_count={} indirect_dispatch_count={} last_dispatch={}x{}x{} last_dispatch_indirect={} bind_group_count_final={}",
            commandEncoder,
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(passLabel),
            mPipelineSwitchCount,
            mEncounteredPipelineLabels.size(),
            buildLogLabelList(mEncounteredPipelineLabels),
            mEncounteredBindGroupLabels.size(),
            buildLogLabelList(mEncounteredBindGroupLabels),
            mDispatchCount,
            mIndirectDispatchCount,
            mHasDispatch ? mLastDispatchX : 0u,
            mHasDispatch ? mLastDispatchY : 0u,
            mHasDispatch ? mLastDispatchZ : 0u,
            mHasDispatch && mLastDispatchWasIndirect,
            bindGroupCount);
    }
} // namespace GVM::RHI::Vulkan

#else

namespace GVM::RHI::Vulkan
{
    void VKComputePassDiagnostics::emitEndOfPassLog(
        VKDevice *device,
        const void *commandEncoder,
        vk::CommandBuffer commandBuffer,
        const eastl::string &passLabel,
        size_t bindGroupCount) const
    {
        (void)device;
        (void)commandEncoder;
        (void)commandBuffer;
        (void)passLabel;
        (void)bindGroupCount;
    }
} // namespace GVM::RHI::Vulkan

#endif
