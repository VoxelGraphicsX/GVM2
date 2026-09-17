#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKTransientDescriptorAllocator final
    {
    public:
        struct AllocationResult
        {
            vk::DescriptorSet descriptorSet = nullptr;
            bool createdPoolPage = false;
            uint64_t poolCreateNs = 0u;
        };

        void init(VKDevice *device);
        void clear();

        [[nodiscard]]
        AllocationResult allocate(
            vk::DescriptorSetLayout layout,
            const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet);

    private:
        struct PoolPage
        {
            vk::UniqueDescriptorPool pool;
            eastl::vector<vk::DescriptorPoolSize> poolSizes;
        };

        VKDevice *mDevice = nullptr;
        eastl::vector<PoolPage> mPoolPages;
    };
} // namespace GVM::RHI::Vulkan
