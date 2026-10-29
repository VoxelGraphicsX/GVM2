#include "VKBindGroupLayout.hpp"

#include "VKBindingUtils.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"

#include <EASTL/unordered_map.h>

#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKBindGroupLayout::init(VKDevice &device, const BindGroupLayoutDescriptor &descriptor)
    {
        mDevice = &device;
        mDescriptor = descriptor;
        eastl::sort(mDescriptor.entries.begin(), mDescriptor.entries.end(), [](const BindGroupLayoutEntry &lhs, const BindGroupLayoutEntry &rhs)
        {
            return lhs.binding < rhs.binding;
        });
        for (size_t index = 1; index < mDescriptor.entries.size(); ++index)
        {
            if (mDescriptor.entries[index - 1].binding == mDescriptor.entries[index].binding)
            {
                throw std::invalid_argument("VKBindGroupLayout::init encountered duplicate binding numbers.");
            }
        }
        mLabelName = descriptor.label;
        mBindings.clear();
        mPoolSizesPerSet.clear();

        eastl::unordered_map<vk::DescriptorType, uint32_t> descriptorCounts;
        descriptorCounts.reserve(mDescriptor.entries.size());

        mBindings.reserve(mDescriptor.entries.size());
        eastl::vector<vk::DescriptorBindingFlags> bindingFlags;
        bindingFlags.reserve(mDescriptor.entries.size());
        mPoolFlags = {};
        for (const BindGroupLayoutEntry &entry : mDescriptor.entries)
        {
            const auto entryKind = Detail::getBindGroupLayoutEntryKind(entry);
            if (entryKind == Detail::BindGroupEntryKind::Undefined)
            {
                throw std::invalid_argument("VKBindGroupLayout::init encountered an undefined binding kind.");
            }

            vk::DescriptorSetLayoutBinding binding = {};
            binding.binding = entry.binding;
            binding.descriptorType = translateDescriptorType(entry);
            binding.descriptorCount = entry.maxCount;
            binding.stageFlags = translateShaderStages(entry.visibility);
            if (binding.stageFlags == vk::ShaderStageFlags{})
            {
                throw std::invalid_argument("VKBindGroupLayout::init requires non-empty shader visibility.");
            }
            mBindings.push_back(binding);
            vk::DescriptorBindingFlags flags = {};
            if (binding.descriptorType == vk::DescriptorType::eSampledImage ||
                binding.descriptorType == vk::DescriptorType::eStorageBuffer)
            {
                flags |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                mPoolFlags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
            }
            if (binding.descriptorCount > 1u)
            {
                flags |= vk::DescriptorBindingFlagBits::ePartiallyBound;
            }
            bindingFlags.push_back(flags);

            descriptorCounts[binding.descriptorType] += binding.descriptorCount;
        }

        for (const auto &[type, descriptorCount] : descriptorCounts)
        {
            mPoolSizesPerSet.push_back(vk::DescriptorPoolSize{type, descriptorCount});
        }
        eastl::sort(mPoolSizesPerSet.begin(), mPoolSizesPerSet.end(), [](const vk::DescriptorPoolSize &lhs, const vk::DescriptorPoolSize &rhs)
        {
            return static_cast<uint32_t>(lhs.type) < static_cast<uint32_t>(rhs.type);
        });

        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();
        vk::DescriptorSetLayoutCreateInfo createInfo = {};
        createInfo.pNext = &flagsInfo;
        if (mPoolFlags != vk::DescriptorPoolCreateFlags{})
        {
            createInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        }
        createInfo.bindingCount = static_cast<uint32_t>(mBindings.size());
        createInfo.pBindings = mBindings.data();

        mLayout = device.getNativeDevice().createDescriptorSetLayoutUnique(createInfo);
        mDevice->setDebugObjectName(vk::ObjectType::eDescriptorSetLayout, reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(mLayout.get())), mLabelName.c_str());
    }

    vk::DescriptorSetLayout VKBindGroupLayout::getNativeDescriptorSetLayout() const
    {
        return mLayout.get();
    }

    const BindGroupLayoutDescriptor &VKBindGroupLayout::getDescriptor() const
    {
        return mDescriptor;
    }

    const eastl::vector<vk::DescriptorSetLayoutBinding> &VKBindGroupLayout::getNativeBindings() const
    {
        return mBindings;
    }

    const eastl::vector<vk::DescriptorPoolSize> &VKBindGroupLayout::getPoolSizesPerSet() const
    {
        return mPoolSizesPerSet;
    }
} // namespace GVM::RHI::Vulkan
