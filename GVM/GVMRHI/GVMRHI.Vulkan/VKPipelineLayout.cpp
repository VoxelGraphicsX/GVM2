#include "VKPipelineLayout.hpp"

#include "VKBindGroupLayout.hpp"
#include "VKDevice.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKPipelineLayout::init(VKDevice &device, const PipelineLayoutDescriptor &descriptor)
    {
        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mBindGroupLayouts = descriptor.bindGroupLayouts;

        eastl::vector<vk::DescriptorSetLayout> setLayouts;
        setLayouts.reserve(mBindGroupLayouts.size());
        for (const BindGroupLayout &layout : mBindGroupLayouts)
        {
            if (layout == nullptr)
            {
                if (!mEmptyDescriptorSetLayout)
                {
                    vk::DescriptorSetLayoutCreateInfo emptyLayoutCreateInfo = {};
                    mEmptyDescriptorSetLayout = device.getNativeDevice().createDescriptorSetLayoutUnique(emptyLayoutCreateInfo);
                }
                setLayouts.push_back(mEmptyDescriptorSetLayout.get());
                continue;
            }
            setLayouts.push_back(static_cast<VKBindGroupLayout *>(layout.get())->getNativeDescriptorSetLayout());
        }

        vk::PipelineLayoutCreateInfo createInfo = {};
        createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        createInfo.pSetLayouts = setLayouts.data();

        mLayout = device.getNativeDevice().createPipelineLayoutUnique(createInfo);
    }

    vk::PipelineLayout VKPipelineLayout::getNativePipelineLayout() const
    {
        return mLayout.get();
    }

    const PipelineLayoutDescriptor &VKPipelineLayout::getDescriptor() const
    {
        return mDescriptor;
    }

    uint32_t VKPipelineLayout::getBindGroupLayoutCount() const
    {
        return static_cast<uint32_t>(mBindGroupLayouts.size());
    }

    const BindGroupLayout &VKPipelineLayout::getBindGroupLayoutHandle(uint32_t index) const
    {
        return mBindGroupLayouts[index];
    }
} // namespace GVM::RHI::Vulkan
