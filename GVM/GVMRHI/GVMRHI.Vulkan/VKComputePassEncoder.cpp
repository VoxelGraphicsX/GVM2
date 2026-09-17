#include "VKComputePassEncoder.hpp"

#include "VKBindGroup.hpp"
#include "VKBindingUtils.hpp"
#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKComputePipeline.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKPipelineLayout.hpp"

#include <GVMRHI/GVMCpuProbe.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
        namespace
    {
        constexpr eastl::string_view ComputePassEncoderLogCategory = "gvmrhi.vulkan.compute_pass_encoder";
    } // namespace

namespace
    {
        void appendUniqueLabel(eastl::vector<eastl::string> &labels, const eastl::string &label)
        {
            const eastl::string resolvedLabel = label.empty() ? eastl::string("<unlabeled>") : label;
            if (eastl::find(labels.begin(), labels.end(), resolvedLabel) != labels.end())
            {
                return;
            }
            labels.push_back(resolvedLabel);
        }

        [[nodiscard]]
        eastl::string buildLabelList(const eastl::vector<eastl::string> &labels, size_t limit = 8u)
        {
            if (labels.empty())
            {
                return "<none>";
            }

            eastl::string text;
            const size_t resolvedLimit = eastl::min(limit, labels.size());
            for (size_t index = 0; index < resolvedLimit; ++index)
            {
                if (index != 0u)
                {
                    text += "|";
                }
                text += labels[index];
            }
            if (labels.size() > resolvedLimit)
            {
                text += "|...";
            }
            return text;
        }

        void validateDispatchWithinDeviceLimits(
            VKDevice *device,
            const eastl::string &passLabel,
            const VKComputePipeline *pipeline,
            uint32_t x,
            uint32_t y,
            uint32_t z)
        {
            if (device == nullptr)
            {
                return;
            }

            const vk::PhysicalDeviceProperties properties = device->getPhysicalDevice().getProperties();
            const auto &limits = properties.limits.maxComputeWorkGroupCount;
            if (x <= limits[0] && y <= limits[1] && z <= limits[2])
            {
                return;
            }

            GVMLogError(
                device, ComputePassEncoderLogCategory,
                "event=compute_pass_dispatch_limit_exceeded device_name=\"{}\" pass_label={} pipeline_label={} requested={}x{}x{} limit={}x{}x{}",
                properties.deviceName.data(),
                safeLogLabel(passLabel),
                pipeline != nullptr ? safeLogLabel(pipeline->getLabelName()) : "<no-pipeline>",
                x,
                y,
                z,
                limits[0],
                limits[1],
                limits[2]);
            throw makeOutOfRange(
                "VKComputePassEncoder::dispatchWorkgroups requested " +
                eastl::to_string(x) + "x" +
                eastl::to_string(y) + "x" +
                eastl::to_string(z) +
                " workgroups, exceeding the selected Vulkan device limit of " +
                eastl::to_string(limits[0]) + "x" +
                eastl::to_string(limits[1]) + "x" +
                eastl::to_string(limits[2]) + ".");
        }
    } // namespace

    void VKComputePassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
        {
            throw makeInvalidArgument("VKComputePassEncoder::init requires a valid device and command encoder.");
        }

        mDevice = device;
        mCommandEncoder = commandEncoder;
        mLabelName = descriptor.label;
        mTimestampWrites = descriptor.timestampWrites;
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->writePassTimestamp(
            mTimestampWrites,
            true,
            vk::PipelineStageFlagBits::eTopOfPipe);
    }

    void VKComputePassEncoder::setPipeline(ComputePipeline pipeline)
    {
        ensureOpen("VKComputePassEncoder::setPipeline");
        if (pipeline == nullptr)
        {
            throw makeInvalidArgument("VKComputePassEncoder::setPipeline requires a valid pipeline.");
        }

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainComputePipeline(pipeline);

        if (mCurrentPipeline == pipeline)
        {
            return;
        }

        auto *pipelineImpl = static_cast<VKComputePipeline *>(pipeline.get());
        auto *pipelineLayout = static_cast<VKPipelineLayout *>(pipelineImpl->getPipelineLayoutHandle().get());
        const vk::PipelineLayout nativePipelineLayout = pipelineImpl->getNativePipelineLayout();
        const bool layoutChanged = mNativePipelineLayout != nativePipelineLayout;

        mCurrentPipeline = pipeline;
        mPipelineImpl = pipelineImpl;
        mPipelineLayout = pipelineLayout;
        mNativePipelineLayout = nativePipelineLayout;
        mBindGroupStateDirty = true;
        mPreparedBindingsDirty = true;
        ++mPipelineSwitchCount;
        appendUniqueLabel(mEncounteredPipelineLabels, pipelineImpl != nullptr ? pipelineImpl->getLabelName() : eastl::string{});
        if (layoutChanged)
        {
            mNativeBoundDescriptorBindings.clear();
        }

        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineImpl->getNativePipeline());
        rebindCompatibleDescriptorBindings(commandBuffer);
    }

    void VKComputePassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("VKComputePassEncoder::setBindGroup");
        if (group == nullptr)
        {
            throw makeInvalidArgument("VKComputePassEncoder::setBindGroup requires a valid bind group.");
        }
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainBindGroup(group);

        const DescriptorBindingState desiredState = DescriptorBindingState{
            .group = group,
            .layout = nullptr,
            .descriptorSet = vk::DescriptorSet{},
            .descriptorSetOverride = false,
        };
        const auto existing = mDescriptorBindings.find(groupIndex);
        const bool bindingChanged =
            existing == mDescriptorBindings.end() ||
            !areDescriptorBindingStatesEquivalent(existing->second, desiredState);
        if (bindingChanged)
        {
            mDescriptorBindings[groupIndex] = desiredState;
            mBindGroupStateDirty = true;
            mPreparedBindingsDirty = true;
            appendUniqueLabel(
                mEncounteredBindGroupLabels,
                group != nullptr ? static_cast<VKBindGroup *>(group.get())->getLabelName() : eastl::string{});
        }

        if (bindingChanged && mPipelineImpl != nullptr)
        {
            bindCompatibleDescriptorBindingIfNeeded(groupIndex, desiredState, commandEncoder->getNativeCommandBuffer());
        }
    }

    void VKComputePassEncoder::dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z)
    {
        ensureOpen("VKComputePassEncoder::dispatchWorkgroups");
        if (mCurrentPipeline == nullptr)
        {
            throw makeLogicError("VKComputePassEncoder::dispatchWorkgroups requires a pipeline to be bound first.");
        }

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        validateDispatchWithinDeviceLimits(mDevice, mLabelName, mPipelineImpl, x, y, z);
        ensureValidDispatchState("VKComputePassEncoder::dispatchWorkgroups");
        preparePassBindings(commandBuffer);
        ++mDispatchCount;
        mLastDispatchX = x;
        mLastDispatchY = y;
        mLastDispatchZ = z;
        mLastDispatchWasIndirect = false;
        mHasDispatch = true;
        GVMLogTrace(
            mDevice, ComputePassEncoderLogCategory,
            "event=compute_pass_dispatch command_encoder_ptr={} command_buffer_ptr={} pass_label={} pipeline_label={} x={} y={} z={} indirect=false bind_group_count={} prepared_bindings_dirty={}",
            static_cast<void *>(commandEncoder),
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(mLabelName),
            mPipelineImpl != nullptr ? safeLogLabel(mPipelineImpl->getLabelName()) : "<no-pipeline>",
            x,
            y,
            z,
            mDescriptorBindings.size(),
            mPreparedBindingsDirty);
        commandBuffer.dispatch(x, y, z);
    }

    void VKComputePassEncoder::dispatchWorkgroupsIndirect(BufferRange indirectBuffer)
    {
        ensureOpen("VKComputePassEncoder::dispatchWorkgroupsIndirect");
        if (mCurrentPipeline == nullptr || indirectBuffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKComputePassEncoder::dispatchWorkgroupsIndirect requires a pipeline and valid indirect buffer.");
        }

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        ensureValidDispatchState("VKComputePassEncoder::dispatchWorkgroupsIndirect");
        preparePassBindings(commandBuffer);

        auto *buffer = static_cast<VKBuffer *>(indirectBuffer.buffer.get());
        commandEncoder->retainBuffer(indirectBuffer.buffer);
        commandEncoder->getTaskDependencyResolver().synchronizeBufferRange(
            commandBuffer,
            *buffer,
            indirectBuffer.offset,
            indirectBuffer.size,
            vk::PipelineStageFlagBits::eDrawIndirect,
            vk::AccessFlagBits::eIndirectCommandRead);

        ++mIndirectDispatchCount;
        mLastDispatchX = 0u;
        mLastDispatchY = 0u;
        mLastDispatchZ = 0u;
        mLastDispatchWasIndirect = true;
        mHasDispatch = true;
        GVMLogTrace(
            mDevice, ComputePassEncoderLogCategory,
            "event=compute_pass_dispatch command_encoder_ptr={} command_buffer_ptr={} pass_label={} pipeline_label={} indirect=true indirect_buffer_label={} indirect_offset={} indirect_size={} bind_group_count={} prepared_bindings_dirty={}",
            static_cast<void *>(commandEncoder),
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(mLabelName),
            mPipelineImpl != nullptr ? safeLogLabel(mPipelineImpl->getLabelName()) : "<no-pipeline>",
            safeLogLabel(buffer->getLabelName()),
            indirectBuffer.offset,
            indirectBuffer.size,
            mDescriptorBindings.size(),
            mPreparedBindingsDirty);
        commandBuffer.dispatchIndirect(buffer->getNativeBuffer(), indirectBuffer.offset);
    }

    void VKComputePassEncoder::end()
    {
        if (mEnded)
        {
            return;
        }
        ensureOpen("VKComputePassEncoder::end");
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        const vk::CommandBuffer commandBuffer = commandEncoder != nullptr ? commandEncoder->getNativeCommandBuffer() : vk::CommandBuffer{};
        commandEncoder->writePassTimestamp(
            mTimestampWrites,
            false,
            vk::PipelineStageFlagBits::eBottomOfPipe);
        GVMLogTrace(
            mDevice, ComputePassEncoderLogCategory,
            "event=compute_pass_end command_encoder_ptr={} command_buffer_ptr={} pass_label={} pipeline_switches={} unique_pipelines={} pipeline_labels={} unique_bind_groups={} bind_group_labels={} dispatch_count={} indirect_dispatch_count={} last_dispatch={}x{}x{} last_dispatch_indirect={} bind_group_count_final={}",
            static_cast<void *>(commandEncoder),
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(mLabelName),
            mPipelineSwitchCount,
            mEncounteredPipelineLabels.size(),
            buildLabelList(mEncounteredPipelineLabels),
            mEncounteredBindGroupLabels.size(),
            buildLabelList(mEncounteredBindGroupLabels),
            mDispatchCount,
            mIndirectDispatchCount,
            mHasDispatch ? mLastDispatchX : 0u,
            mHasDispatch ? mLastDispatchY : 0u,
            mHasDispatch ? mLastDispatchZ : 0u,
            mHasDispatch && mLastDispatchWasIndirect,
            mDescriptorBindings.size());
        commandEncoder->notePassSummary();
        commandEncoder->notifyPassEnded();
        mEnded = true;
    }

    VKCommandEncoder *VKComputePassEncoder::getCommandEncoder() const
    {
        return static_cast<VKCommandEncoder *>(mCommandEncoder.get());
    }

    void VKComputePassEncoder::bindCompatibleDescriptorBindingIfNeeded(
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        vk::CommandBuffer commandBuffer)
    {
        if (mPipelineLayout == nullptr)
        {
            return;
        }

        bindDescriptorBindingStateIfNeeded(
            commandBuffer,
            vk::PipelineBindPoint::eCompute,
            mNativePipelineLayout,
            mPipelineLayout,
            groupIndex,
            desiredState,
            mNativeBoundDescriptorBindings);
    }

    void VKComputePassEncoder::rebindCompatibleDescriptorBindings(vk::CommandBuffer commandBuffer)
    {
        if (mPipelineLayout == nullptr)
        {
            return;
        }

        for (const auto &[groupIndex, bindingState] : mDescriptorBindings)
        {
            bindCompatibleDescriptorBindingIfNeeded(groupIndex, bindingState, commandBuffer);
        }
    }

    void VKComputePassEncoder::ensureValidDispatchState(const char *apiName) const
    {
        if (mPipelineImpl == nullptr || mPipelineLayout == nullptr)
        {
            return;
        }

        if (mBindGroupStateDirty)
        {
            auto *self = const_cast<VKComputePassEncoder *>(this);
            self->mBindGroupStateValid = areRequiredDescriptorBindingsSatisfied(mPipelineLayout, mDescriptorBindings);
            self->mBindGroupStateDirty = false;
        }

        if (!mBindGroupStateValid)
        {
            throw makeLogicError(buildMissingDescriptorBindingMessage(
                apiName,
                mPipelineImpl != nullptr ? mPipelineImpl->getLabelName() : eastl::string{},
                mPipelineLayout,
                mDescriptorBindings));
        }
    }

    void VKComputePassEncoder::preparePassBindings(vk::CommandBuffer commandBuffer)
    {
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();
        struct CompatibleDescriptorBinding
        {
            uint32_t groupIndex = 0u;
            DescriptorBindingState bindingState = {};
        };

        eastl::vector<CompatibleDescriptorBinding> compatibleBindings;
        compatibleBindings.reserve(mDescriptorBindings.size());
        eastl::vector<const VKBindGroup *> bindGroupsToPrepare;
        bindGroupsToPrepare.reserve(mDescriptorBindings.size());

        for (const auto &[groupIndex, bindingState] : mDescriptorBindings)
        {
            if (!isDescriptorBindingStateCompatibleWithPipelineLayout(bindingState, mPipelineLayout, groupIndex))
            {
                continue;
            }

            compatibleBindings.push_back(CompatibleDescriptorBinding{
                .groupIndex = groupIndex,
                .bindingState = bindingState,
            });
            if (bindingState.descriptorSetOverride || bindingState.group == nullptr)
            {
                continue;
            }

            bindGroupsToPrepare.push_back(static_cast<const VKBindGroup *>(bindingState.group.get()));
        }

        BindGroupPrepareCache bindGroupPrepareCache;
        const PreparedBindGroupBindings preparedBindGroups = prepareBindGroupBindings(
            commandBuffer,
            stateTracker,
            bindGroupsToPrepare,
            vk::PipelineStageFlagBits::eComputeShader,
            nullptr,
            eastl::vector<Texture>{},
            &bindGroupPrepareCache);
        GVMLogDebug(
            mDevice, ComputePassEncoderLogCategory,
            "event=compute_pass_prepare_bindings pass_label={} compatible_bindings={} bind_groups_to_prepare={} prepared_bind_groups={} texture_prepare_keys={} buffer_prepare_keys={}",
            safeLogLabel(mLabelName),
            compatibleBindings.size(),
            bindGroupsToPrepare.size(),
            preparedBindGroups.size(),
            bindGroupPrepareCache.textureCount,
            bindGroupPrepareCache.bufferCount);
        GVMCpuProbeInstantDetail(
            mDevice,
            ComputePassEncoderLogCategory,
            "compute_pass.prepare_bindings",
            "pass_label={} compatible_bindings={} bind_groups_to_prepare={} prepared_bind_groups={} texture_prepare_keys={} buffer_prepare_keys={}",
            safeLogLabel(mLabelName),
            compatibleBindings.size(),
            bindGroupsToPrepare.size(),
            preparedBindGroups.size(),
            bindGroupPrepareCache.textureCount,
            bindGroupPrepareCache.bufferCount);
        for (const CompatibleDescriptorBinding &compatibleBinding : compatibleBindings)
        {
            bindCompatibleDescriptorBindingIfNeeded(compatibleBinding.groupIndex, compatibleBinding.bindingState, commandBuffer);
        }

        mPreparedBindingsDirty = false;
    }

    void VKComputePassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on an ended Vulkan compute pass encoder.");
        }
    }
} // namespace GVM::RHI::Vulkan
