#include "VKPipelineLayout.hpp"

#include "VKBindGroupLayout.hpp"
#include "VKDevice.hpp"
#include "VKArguments.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKPipelineLayout::init(VKDevice &device, const PipelineLayoutDescriptor &descriptor)
    {
        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mBindGroupLayouts = descriptor.bindGroupLayouts;

        if (!descriptor.arguments.resources.empty())
        {
            for (const auto &resource : descriptor.arguments.resources)
            {
                const auto &entry = resource.entry;
                if (entry.storageTexture.access != StorageTextureAccess::Undefined && !device.getCapabilities().supportsStorageImageUpdateAfterBind())
                {
                    throw std::invalid_argument("Vulkan Args storage-image tables require descriptorBindingStorageImageUpdateAfterBind.");
                }
                const uint32_t expected = entry.sampler.type != SamplerBindingType::Undefined ? VKArgumentTable::SamplerBinding :
                    entry.storageTexture.access != StorageTextureAccess::Undefined ? VKArgumentTable::StorageImageBinding :
                    VKArgumentTable::SampledImageBinding;
                if (entry.binding != expected)
                {
                    throw std::invalid_argument("Vulkan Args resource binding does not match the device-wide table binding for its descriptor kind.");
                }
            }
            mArgumentTable = eastl::intrusive_ptr<VKArgumentTable>(&device.getArgumentResources().getOrCreateTable());
        }
        eastl::vector<vk::DescriptorSetLayout> setLayouts;
        setLayouts.reserve(mBindGroupLayouts.size() + (mArgumentTable ? 1u : 0u));
        if (mArgumentTable)
        {
            setLayouts.push_back(mArgumentTable->getLayout());
        }
        for (const BindGroupLayout &layout : mBindGroupLayouts)
        {
            if (layout == nullptr)
            {
                if (!mEmptyDescriptorSetLayout)
                {
                    vk::DescriptorSetLayoutCreateInfo emptyLayoutCreateInfo = {};
                    mEmptyDescriptorSetLayout = device.getNativeDevice().createDescriptorSetLayoutUnique(emptyLayoutCreateInfo);
                    mDevice->setDebugObjectName(vk::ObjectType::eDescriptorSetLayout, reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(mEmptyDescriptorSetLayout.get())), "GVM empty descriptor set layout");
                }
                setLayouts.push_back(mEmptyDescriptorSetLayout.get());
                continue;
            }
            setLayouts.push_back(static_cast<VKBindGroupLayout *>(layout.get())->getNativeDescriptorSetLayout());
        }

        vk::PipelineLayoutCreateInfo createInfo = {};
        createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        createInfo.pSetLayouts = setLayouts.data();
        const vk::PushConstantRange argumentRoot{ArgumentShaderStages, 0, 8};
        if (descriptor.arguments.byteSize != 0)
        {
            createInfo.pushConstantRangeCount = 1;
            createInfo.pPushConstantRanges = &argumentRoot;
        }

        mLayout = device.getNativeDevice().createPipelineLayoutUnique(createInfo);
        mDevice->setDebugObjectName(vk::ObjectType::ePipelineLayout, reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(mLayout.get())), mLabelName.c_str());
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
