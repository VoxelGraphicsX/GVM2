#include "VKComputePassEncoder.hpp"
#include "VKInstance.hpp"

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
#include <GVMRHI/Private/ArgumentValidation.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view ComputePassEncoderLogCategory = "gvmrhi.vulkan.compute_pass_encoder";
    } // namespace

    void VKComputePassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
        {
            throw std::invalid_argument("VKComputePassEncoder::init requires a valid device and command encoder.");
        }

        mDevice = device;
        mDiagnostics.begin(device->getLogger());
        mRecordingOwner = commandEncoder;
        mCommandEncoder = static_cast<VKCommandEncoder *>(commandEncoder.get());
        mLabelName = descriptor.label;
        mTimestampWrites = descriptor.timestampWrites;
        const auto &workgroupLimits = device->getCapabilities().getPhysicalDeviceProperties().limits.maxComputeWorkGroupCount;
        mValidator.init(descriptor.label, Private::ComputePassLimits{
            .maxBindGroups = VKMaxBindGroupCount,
            .maxWorkgroupCount = {workgroupLimits[0], workgroupLimits[1], workgroupLimits[2]},
        });
        static_cast<VKCommandEncoder *>(mCommandEncoder)->writePassTimestamp(
            mTimestampWrites,
            true,
            vk::PipelineStageFlagBits2::eTopOfPipe);
    }

    void VKComputePassEncoder::setPipeline(ComputePipeline pipeline)
    {
        ensureOpen("VKComputePassEncoder::setPipeline");
        const auto *selectedPipeline = static_cast<VKComputePipeline *>(pipeline.get());
        const PipelineLayout selectedLayout = selectedPipeline != nullptr ? selectedPipeline->getPipelineLayoutHandle() : PipelineLayout{};
        const auto *selectedLayoutImpl = static_cast<VKPipelineLayout *>(selectedLayout.get());
        mValidator.setPipeline(pipeline, selectedLayout, selectedLayoutImpl != nullptr ? &selectedLayoutImpl->getDescriptor() : nullptr);

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder);
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
        mCurrentArguments = {};
        mPipelineImpl = pipelineImpl;
        mPipelineLayout = pipelineLayout;
        mNativePipelineLayout = nativePipelineLayout;
        mDiagnostics.recordPipeline(pipelineImpl->getLabelName());
        if (layoutChanged)
        {
            mNativeBoundDescriptorBindings.boundMask = 0u;
        }
    }

    void VKComputePassEncoder::setArguments(Arguments arguments)
    {
        ensureOpen("VKComputePassEncoder::setArguments");
        auto *snapshot = dynamic_cast<VKArguments *>(arguments.get());
        if (snapshot == nullptr || &snapshot->getDevice() != mDevice)
        {
            throw std::invalid_argument("Compute Args snapshot does not belong to the selected pipeline layout.");
        }
        mValidator.checkArguments(snapshot->getLayout());
        mCurrentArguments = mCommandEncoder->allocateArguments(eastl::move(arguments));
        mValidator.setArguments(snapshot->getComputeTextureUses());
    }

    void VKComputePassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("VKComputePassEncoder::setBindGroup");
        mValidator.checkBindGroup(group, groupIndex);
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder);
        commandEncoder->retainBindGroup(group);

        const DescriptorBindingState desiredState = DescriptorBindingState{
            .group = group,
            .layout = nullptr,
            .descriptorSet = vk::DescriptorSet{},
            .descriptorSetOverride = false,
        };
        const DescriptorBindingState *existing = getDescriptorBinding(mDescriptorBindings, groupIndex);
        const bool bindingChanged =
            existing == nullptr ||
            !areDescriptorBindingStatesEquivalent(*existing, desiredState);
        if (bindingChanged)
        {
            setDescriptorBinding(mDescriptorBindings, groupIndex, desiredState);
            mBindingSnapshotsDirty = true;
            mDiagnostics.recordBindGroup(static_cast<VKBindGroup *>(group.get())->getLabelName());
        }
        const auto *bindGroup = static_cast<VKBindGroup *>(group.get());
        mValidator.setBindGroup(group, groupIndex, bindGroup->getLayoutHandle(), bindGroup->getComputeTextureUses());
    }

    void VKComputePassEncoder::dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z)
    {
        ensureOpen("VKComputePassEncoder::dispatchWorkgroups");
        mValidator.checkDispatch(x, y, z);
        mCommands.push_back(DispatchCommand{mCurrentPipeline,
            recordBindingSnapshot(),
            x, y, z, {}, mCurrentArguments});
        mDiagnostics.recordDispatch(x, y, z);
    }

    void VKComputePassEncoder::dispatchWorkgroupsIndirect(BufferRange indirectBuffer)
    {
        ensureOpen("VKComputePassEncoder::dispatchWorkgroupsIndirect");
        mValidator.checkDispatchIndirect(indirectBuffer);
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        recordDispatchIndirect(source, indirectBuffer);
        mCommandEncoder->retainBuffer(indirectBuffer.buffer);
    }

    void VKComputePassEncoder::recordDispatchIndirect(VKBuffer &buffer, BufferRange range)
    {
        ensureOpen("VKComputePassEncoder::dispatchWorkgroupsIndirect");
        if (mCurrentPipeline == nullptr || &buffer.getDevice() != mDevice)
        {
            throw std::invalid_argument("Indirect dispatch requires a pipeline and a native input from the same device.");
        }
        mCommands.push_back(DispatchCommand{mCurrentPipeline,
            recordBindingSnapshot(),
            0, 0, 0, range, mCurrentArguments, &buffer});
        mDiagnostics.recordIndirectDispatch();
    }

    void VKComputePassEncoder::encode(VKCommandEncoder &commandEncoder)
    {
        const vk::CommandBuffer commandBuffer = commandEncoder.getNativeCommandBuffer();
        const char *debugPassLabel = mLabelName.empty() ? "ComputePass" : mLabelName.c_str();
        if (mDevice->getInstance()->supportsDebugUtils())
        {
            VkDebugUtilsLabelEXT label = {};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = debugPassLabel;
            vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(commandBuffer), &label);
        }
        mNativeBoundDescriptorBindings.boundMask = 0u;
        vk::Pipeline boundPipeline = vk::Pipeline{};
        for (const auto &dispatch : mCommands)
        {
            mPipelineImpl = static_cast<VKComputePipeline *>(dispatch.pipeline.get());
            mPipelineLayout = static_cast<VKPipelineLayout *>(mPipelineImpl->getPipelineLayoutHandle().get());
            const auto layout = mPipelineImpl->getNativePipelineLayout();
            if (layout != mNativePipelineLayout)
            {
                mNativeBoundDescriptorBindings.boundMask = 0u;
            }
            mNativePipelineLayout = layout;
            if (boundPipeline != mPipelineImpl->getNativePipeline())
            {
                boundPipeline = mPipelineImpl->getNativePipeline();
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, boundPipeline);
            }
            eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest> mergedBufferSyncRequests;
            eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncUses;
            if (dispatch.arguments.arguments != nullptr)
            {
                const auto argumentBufferUses = dispatch.arguments.arguments->getBufferUses();
                if (dispatch.nativeIndirectBuffer == nullptr)
                {
                    bufferSyncUses = argumentBufferUses;
                }
                else
                {
                    mergedBufferSyncRequests.assign(argumentBufferUses.begin(), argumentBufferUses.end());
                    mergedBufferSyncRequests.push_back({dispatch.nativeIndirectBuffer,
                        dispatch.indirect.offset, sizeof(VkDispatchIndirectCommand),
                        vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
                    bufferSyncUses = eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest>(
                        mergedBufferSyncRequests.data(), mergedBufferSyncRequests.size());
                }
            }
            else if (dispatch.nativeIndirectBuffer != nullptr)
            {
                mergedBufferSyncRequests.push_back({dispatch.nativeIndirectBuffer,
                    dispatch.indirect.offset, sizeof(VkDispatchIndirectCommand),
                    vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
                bufferSyncUses = eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest>(
                    mergedBufferSyncRequests.data(), mergedBufferSyncRequests.size());
            }
            if (dispatch.arguments.arguments != nullptr)
            {
                const auto &snapshot = *dispatch.arguments.arguments;
                if (snapshot.getTextureUses().empty())
                {
                    commandEncoder.getTaskDependencyResolver().synchronizeBufferRanges(commandBuffer,
                        eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest>(bufferSyncUses.begin(), bufferSyncUses.end()));
                }
                else
                {
                    prepareBindGroupBindings(commandBuffer, commandEncoder.getTaskDependencyResolver(), {}, vk::PipelineStageFlagBits2::eComputeShader,
                        nullptr, snapshot.getTextureUses(), bufferSyncUses);
                }
                const auto table = snapshot.getResourceTable();
                if (table)
                {
                    const DescriptorBindingState desired{nullptr, nullptr, table, true};
                    bindCompatibleDescriptorBindingStateIfNeeded(commandBuffer, vk::PipelineBindPoint::eCompute, mNativePipelineLayout,
                        0, desired, mNativeBoundDescriptorBindings);
                }
                if (dispatch.arguments.address != 0)
                {
                    commandBuffer.pushConstants(mNativePipelineLayout, ArgumentShaderStages, 0, 8, &dispatch.arguments.address);
                }
            }
            else
            {
                preparePassBindings(commandBuffer, mBindingSnapshots[dispatch.bindingSnapshot], bufferSyncUses);
            }
            if (dispatch.nativeIndirectBuffer == nullptr)
            {
                commandBuffer.dispatch(dispatch.x, dispatch.y, dispatch.z);
            }
            else
            {
                auto &buffer = *dispatch.nativeIndirectBuffer;
                commandBuffer.dispatchIndirect(buffer.getNativeBuffer(), dispatch.indirect.offset);
            }
        }

        if (mDevice->getInstance()->supportsDebugUtils())
        {
            vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(commandBuffer));
        }
    }

    uint32_t VKComputePassEncoder::recordBindingSnapshot()
    {
        if (mBindingSnapshots.empty() || mBindingSnapshotsDirty)
        {
            mBindingSnapshots.push_back(mDescriptorBindings);
            mBindingSnapshotsDirty = false;
        }
        return static_cast<uint32_t>(mBindingSnapshots.size() - 1);
    }

    void VKComputePassEncoder::end()
    {
        if (mEnded)
        {
            return;
        }
        ensureOpen("VKComputePassEncoder::end");
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder);
        const vk::CommandBuffer commandBuffer = commandEncoder != nullptr ? commandEncoder->getNativeCommandBuffer() : vk::CommandBuffer{};
        commandEncoder->appendPass(ComputePassEncoder(this));
        commandEncoder->writePassTimestamp(
            mTimestampWrites,
            false,
            vk::PipelineStageFlagBits2::eBottomOfPipe);
        mDiagnostics.emitEndOfPassLog(
            mDevice,
            static_cast<void *>(commandEncoder),
            commandBuffer,
            mLabelName,
            countDescriptorBindings(mDescriptorBindings));
        commandEncoder->notePassSummary();
        commandEncoder->notifyPassEnded();
        mEnded = true;
        mRecordingOwner = nullptr;
        mCurrentArguments = {};
    }

    void VKComputePassEncoder::preparePassBindings(vk::CommandBuffer commandBuffer, const DescriptorBindingTable &bindings,
        eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests)
    {
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder);
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();
        eastl::vector<eastl::pair<uint32_t, const DescriptorBindingState *>> compatibleBindings;
        compatibleBindings.reserve(countDescriptorBindings(bindings));
        eastl::vector<const VKBindGroup *> bindGroupsToPrepare;
        bindGroupsToPrepare.reserve(countDescriptorBindings(bindings));

        for (uint32_t groupIndex = 0; groupIndex < VKMaxBindGroupCount; ++groupIndex)
        {
            const DescriptorBindingState *bindingState = getDescriptorBinding(bindings, groupIndex);
            if (bindingState == nullptr ||
                !isDescriptorBindingStateCompatibleWithPipelineLayout(*bindingState, mPipelineLayout, groupIndex))
            {
                continue;
            }

            compatibleBindings.emplace_back(groupIndex, bindingState);
            if (bindingState->descriptorSetOverride || bindingState->group == nullptr)
            {
                continue;
            }

            bindGroupsToPrepare.push_back(static_cast<const VKBindGroup *>(bindingState->group.get()));
        }

        BindGroupPrepareCache bindGroupPrepareCache;
        const PreparedBindGroupBindings preparedBindGroups = prepareBindGroupBindings(
            commandBuffer,
            stateTracker,
            bindGroupsToPrepare,
            vk::PipelineStageFlagBits2::eComputeShader,
            &bindGroupPrepareCache,
            {},
            bufferSyncRequests);
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
        for (const auto &binding : compatibleBindings)
        {
            bindCompatibleDescriptorBindingStateIfNeeded(commandBuffer, vk::PipelineBindPoint::eCompute,
                mNativePipelineLayout, binding.first, *binding.second, mNativeBoundDescriptorBindings);
        }

    }

    void VKComputePassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on an ended compute pass encoder.").c_str());
        }
    }
} // namespace GVM::RHI::Vulkan
