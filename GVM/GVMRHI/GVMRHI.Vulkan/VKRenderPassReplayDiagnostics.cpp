#include "VKRenderPassReplayDiagnostics.hpp"

#if GVM_LOGGING_ENABLED

#include "VKDevice.hpp"


namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view RenderPassEncoderLogCategory = "gvmrhi.vulkan.render_pass_encoder";
    } // namespace

    void VKRenderPassReplayDiagnostics::emitEndOfReplayLog(
        VKDevice *device,
        const void *commandEncoder,
        vk::CommandBuffer commandBuffer,
        const eastl::string &passLabel,
        vk::RenderPass renderPass,
        vk::Framebuffer framebuffer) const
    {
        GVMLogTrace(
            device, RenderPassEncoderLogCategory,
            "event=render_pass_record_end command_encoder_ptr={} command_buffer_ptr={} pass_label={} render_pass_ptr={} framebuffer_ptr={} pipeline_switches={} unique_pipelines={} pipeline_labels={} bind_group_sets={} unique_bind_groups={} bind_group_labels={} draw_count={} draw_indexed_count={} draw_indirect_count={} draw_indexed_indirect_count={}",
            commandEncoder,
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(passLabel),
            reinterpret_cast<void *>(static_cast<VkRenderPass>(renderPass)),
            reinterpret_cast<void *>(static_cast<VkFramebuffer>(framebuffer)),
            mPipelineSwitchCount,
            mPipelineLabels.size(),
            buildLogLabelList(mPipelineLabels),
            mBindGroupSetCount,
            mBindGroupLabels.size(),
            buildLogLabelList(mBindGroupLabels),
            mDrawCount,
            mDrawIndexedCount,
            mDrawIndirectCount,
            mDrawIndexedIndirectCount);
    }
} // namespace GVM::RHI::Vulkan

#else

namespace GVM::RHI::Vulkan
{
    void VKRenderPassReplayDiagnostics::emitEndOfReplayLog(
        VKDevice *device,
        const void *commandEncoder,
        vk::CommandBuffer commandBuffer,
        const eastl::string &passLabel,
        vk::RenderPass renderPass,
        vk::Framebuffer framebuffer) const
    {
        (void)device;
        (void)commandEncoder;
        (void)commandBuffer;
        (void)passLabel;
        (void)renderPass;
        (void)framebuffer;
    }
} // namespace GVM::RHI::Vulkan

#endif
