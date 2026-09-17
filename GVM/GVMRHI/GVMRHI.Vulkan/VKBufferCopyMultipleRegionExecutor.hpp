#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    class VKBufferCopyMultipleRegionExecutorImpl final : public RefCountedObject
    {
    public:
        ~VKBufferCopyMultipleRegionExecutorImpl() override;

        void init(VKDevice *device);
        void validateCopyRegions(Buffer srcBuffer, Buffer dstBuffer, Buffer copyRegionBuffer, uint32_t copyRegionCount) const;
        void encode(ComputePassEncoder encoder, Buffer srcBuffer, Buffer dstBuffer, Buffer copyRegionBuffer, uint32_t copyRegionCount);

    private:
        void ensureComputeFallbackInitialized();

        VKDevice *mDevice = nullptr;
        eastl::vector<vk::DescriptorSetLayoutBinding> mDescriptorBindings;
        eastl::vector<vk::DescriptorPoolSize> mPoolSizesPerSet;
        vk::UniqueDescriptorSetLayout mDescriptorSetLayout;
        vk::UniquePipelineLayout mPipelineLayout;
        vk::UniqueShaderModule mShaderModule;
        vk::UniquePipeline mPipeline;
    };

    using VKBufferCopyMultipleRegionExecutor = eastl::intrusive_ptr<VKBufferCopyMultipleRegionExecutorImpl>;
} // namespace GVM::RHI::Vulkan
