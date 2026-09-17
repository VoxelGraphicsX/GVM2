#pragma once

#include "VKBindingUtils.hpp"
#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKComputePassEncoder final : public ComputePassEncoderImpl
    {
    public:
        VKComputePassEncoder() = default;

        void init(VKDevice *device, CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor);

        void setPipeline(ComputePipeline pipeline) override;
        void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) override;
        void dispatchWorkgroupsIndirect(BufferRange indirectBuffer) override;
        void end() override;

        [[nodiscard]]
        VKCommandEncoder *getCommandEncoder() const;

    private:
        void bindCompatibleDescriptorBindingIfNeeded(uint32_t groupIndex, const DescriptorBindingState &desiredState, vk::CommandBuffer commandBuffer);
        void rebindCompatibleDescriptorBindings(vk::CommandBuffer commandBuffer);
        void ensureValidDispatchState(const char *apiName) const;
        void preparePassBindings(vk::CommandBuffer commandBuffer);
        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mCommandEncoder = nullptr;
        PassTimestampWrites mTimestampWrites = {};
        ComputePipeline mCurrentPipeline = nullptr;
        const VKComputePipeline *mPipelineImpl = nullptr;
        const VKPipelineLayout *mPipelineLayout = nullptr;
        vk::PipelineLayout mNativePipelineLayout = nullptr;
        eastl::unordered_map<uint32_t, DescriptorBindingState> mDescriptorBindings;
        eastl::unordered_map<uint32_t, DescriptorBindingState> mNativeBoundDescriptorBindings;
        eastl::vector<eastl::string> mEncounteredPipelineLabels;
        eastl::vector<eastl::string> mEncounteredBindGroupLabels;
        bool mBindGroupStateDirty = true;
        bool mBindGroupStateValid = false;
        bool mPreparedBindingsDirty = true;
        uint32_t mPipelineSwitchCount = 0u;
        uint32_t mDispatchCount = 0u;
        uint32_t mIndirectDispatchCount = 0u;
        uint32_t mLastDispatchX = 0u;
        uint32_t mLastDispatchY = 0u;
        uint32_t mLastDispatchZ = 0u;
        bool mLastDispatchWasIndirect = false;
        bool mHasDispatch = false;
        bool mEnded = false;
    };
} // namespace GVM::RHI::Vulkan
