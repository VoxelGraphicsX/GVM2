#pragma once

#include "VKBindingUtils.hpp"
#include "VKArguments.hpp"
#include "VKCommon.hpp"
#include "VKComputePassDiagnostics.hpp"
#include "VKDefines.hpp"

#include <GVMRHI/Private/ComputePassValidator.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKComputePassEncoder final : public ComputePassEncoderImpl
    {
    public:
        VKComputePassEncoder() = default;

        void init(VKDevice *device, CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor);

        void setPipeline(ComputePipeline pipeline) override;
        /** Records immutable Args for the current pipeline and rejects foreign-device snapshots. */
        void setArguments(Arguments arguments) override;
        void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) override;
        void dispatchWorkgroupsIndirect(BufferRange indirectBuffer) override;
        void end() override;
        /// Emits the closed pass after its queue submission order has been determined.
        void encode(VKCommandEncoder &commandEncoder);

    private:
        /** Records a retained native dispatch input without resolving its public buffer handle. */
        void recordDispatchIndirect(VKBuffer &buffer, BufferRange range);

        /// Captures one dispatch and its immutable pipeline/binding selection.
        struct DispatchCommand
        {
            ComputePipeline pipeline;
            uint32_t bindingSnapshot = 0;
            uint32_t x = 0, y = 0, z = 0;
            BufferRange indirect;
            VKArgumentAllocation arguments;
            VKBuffer *nativeIndirectBuffer = nullptr;
        };
        eastl::vector<DispatchCommand> mCommands;
        /** Prepares one dispatch's descriptor and indirect buffer uses before binding its immutable state. */
        void preparePassBindings(vk::CommandBuffer commandBuffer, const DescriptorBindingTable &bindings,
            eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests);
        /** Returns the snapshot index for the current bindings, appending a copy only after they changed. */
        uint32_t recordBindingSnapshot();
        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mRecordingOwner = nullptr;
        VKCommandEncoder *mCommandEncoder = nullptr;
        PassTimestampWrites mTimestampWrites = {};
        ComputePipeline mCurrentPipeline = nullptr;
        VKArgumentAllocation mCurrentArguments;
        const VKComputePipeline *mPipelineImpl = nullptr;
        const VKPipelineLayout *mPipelineLayout = nullptr;
        vk::PipelineLayout mNativePipelineLayout = nullptr;
        DescriptorBindingTable mDescriptorBindings;
        DescriptorBindingTable mNativeBoundDescriptorBindings;
        eastl::vector<DescriptorBindingTable> mBindingSnapshots;
        bool mBindingSnapshotsDirty = true;
        Private::ComputePassValidator mValidator;
        VKComputePassDiagnostics mDiagnostics = {};
        bool mEnded = false;
    };
} // namespace GVM::RHI::Vulkan
