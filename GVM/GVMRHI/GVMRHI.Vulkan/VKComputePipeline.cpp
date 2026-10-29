#include "VKComputePipeline.hpp"

#include "VKDevice.hpp"
#include "VKPipelineLayout.hpp"
#include "VKShaderModule.hpp"
#include "VKShaderReflection.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKComputePipeline::init(VKDevice &device, const ComputePipelineDescriptor &descriptor)
    {
        if (descriptor.layout == nullptr || descriptor.compute.module == nullptr)
        {
            throw std::invalid_argument("VKComputePipeline::init requires a valid pipeline layout and compute shader.");
        }

        const auto *pipelineLayout = static_cast<VKPipelineLayout *>(descriptor.layout.get());
        const auto *computeShader = static_cast<VKShaderModule *>(descriptor.compute.module.get());
        const Detail::ReflectedEntryPoint &computeEntryPoint = Detail::requireShaderEntryPoint(
            computeShader->getReflection(),
            descriptor.compute.entryPoint,
            vk::ShaderStageFlagBits::eCompute,
            "VKComputePipeline::init");
        Detail::validateArgumentAbiAgainstEntryPoint(*pipelineLayout, computeEntryPoint, "VKComputePipeline::init");
        if (descriptor.compute.workgroupX != 0u && descriptor.compute.workgroupX != computeEntryPoint.localSizeX)
        {
            throw std::invalid_argument("VKComputePipeline::init received workgroupX that does not match the reflected local size.");
        }
        if (descriptor.compute.workgroupY != 0u && descriptor.compute.workgroupY != computeEntryPoint.localSizeY)
        {
            throw std::invalid_argument("VKComputePipeline::init received workgroupY that does not match the reflected local size.");
        }
        if (descriptor.compute.workgroupZ != 0u && descriptor.compute.workgroupZ != computeEntryPoint.localSizeZ)
        {
            throw std::invalid_argument("VKComputePipeline::init received workgroupZ that does not match the reflected local size.");
        }

        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mPipelineLayout = descriptor.layout;
        mComputeShader = descriptor.compute.module;

        vk::PipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStage.module = computeShader->getNativeShaderModule();
        shaderStage.pName = descriptor.compute.entryPoint.c_str();

        vk::ComputePipelineCreateInfo createInfo = {};
        createInfo.stage = shaderStage;
        createInfo.layout = pipelineLayout->getNativePipelineLayout();

        mPipeline = device.getNativeDevice().createComputePipelineUnique(device.getNativePipelineCache(), createInfo).value;
        mDevice->setDebugObjectName(vk::ObjectType::ePipeline, reinterpret_cast<uint64_t>(static_cast<VkPipeline>(mPipeline.get())), mLabelName.c_str());
    }

    vk::Pipeline VKComputePipeline::getNativePipeline() const
    {
        return mPipeline.get();
    }

    vk::PipelineLayout VKComputePipeline::getNativePipelineLayout() const
    {
        return static_cast<const VKPipelineLayout *>(mPipelineLayout.get())->getNativePipelineLayout();
    }

    const PipelineLayout &VKComputePipeline::getPipelineLayoutHandle() const
    {
        return mPipelineLayout;
    }

    const ComputePipelineDescriptor &VKComputePipeline::getDescriptor() const
    {
        return mDescriptor;
    }
} // namespace GVM::RHI::Vulkan
