#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKBindGroupLayout final : public BindGroupLayoutImpl
    {
    public:
        VKBindGroupLayout() = default;

        void init(VKDevice &device, const BindGroupLayoutDescriptor &descriptor);

        [[nodiscard]]
        vk::DescriptorSetLayout getNativeDescriptorSetLayout() const;

        [[nodiscard]]
        const BindGroupLayoutDescriptor &getDescriptor() const;

        [[nodiscard]]
        const eastl::vector<vk::DescriptorSetLayoutBinding> &getNativeBindings() const;

        [[nodiscard]]
        const eastl::vector<vk::DescriptorPoolSize> &getPoolSizesPerSet() const;

    private:
        VKDevice *mDevice = nullptr;
        BindGroupLayoutDescriptor mDescriptor = {};
        eastl::vector<vk::DescriptorSetLayoutBinding> mBindings;
        eastl::vector<vk::DescriptorPoolSize> mPoolSizesPerSet;
        vk::UniqueDescriptorSetLayout mLayout;
    };
} // namespace GVM::RHI::Vulkan
