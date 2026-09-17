#pragma once

#include "VKBindGroup.hpp"
#include "VKCommon.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    class VKDescriptorManager final
    {
    public:
        struct BindGroupWriteResult
        {
            size_t writeCount = 0u;
        };

        [[nodiscard]]
        static BindGroupWriteResult writeBindGroupDescriptorSet(
            VKDevice *device,
            vk::DescriptorSet descriptorSet,
            const eastl::vector<VKBindGroup::ResolvedBinding> &bindings);
    };
} // namespace GVM::RHI::Vulkan
