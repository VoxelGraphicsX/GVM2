#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    class VKComputePipeline final : public ComputePipelineImpl
    {
    public:
        VKComputePipeline() = default;

        void init(VKDevice &device, const ComputePipelineDescriptor &descriptor);

        [[nodiscard]]
        vk::Pipeline getNativePipeline() const;

        [[nodiscard]]
        vk::PipelineLayout getNativePipelineLayout() const;

        [[nodiscard]]
        const PipelineLayout &getPipelineLayoutHandle() const;

        [[nodiscard]]
        const ComputePipelineDescriptor &getDescriptor() const;

    private:
        VKDevice *mDevice = nullptr;
        ComputePipelineDescriptor mDescriptor = {};
        PipelineLayout mPipelineLayout = nullptr;
        ShaderModule mComputeShader = nullptr;
        vk::UniquePipeline mPipeline;
    };
} // namespace GVM::RHI::Vulkan
