#pragma once

#include "VKCommon.hpp"

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    struct VKCommandBufferContext
    {
        vk::UniqueCommandPool commandPool;
        vk::CommandBuffer commandBuffer = nullptr;
    };

    class VKQueueCommandContextPool final
    {
    public:
        void init(vk::Device device, uint32_t queueFamilyIndex);
        void destroy();

        [[nodiscard]]
        eastl::shared_ptr<VKCommandBufferContext> acquire();

        void recycle(eastl::shared_ptr<VKCommandBufferContext> context);

        [[nodiscard]]
        size_t availableContextCount() const;

    private:
        vk::Device mDevice = nullptr;
        uint32_t mQueueFamilyIndex = 0;
        eastl::vector<eastl::shared_ptr<VKCommandBufferContext>> mAvailableCommandContexts;
    };
} // namespace GVM::RHI::Vulkan
