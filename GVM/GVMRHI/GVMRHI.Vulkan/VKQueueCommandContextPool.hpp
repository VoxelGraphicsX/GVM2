#pragma once

#include "VKCommon.hpp"

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /// Owns an encoder's exclusive pool and its body plus lazily allocated submission barriers until retirement.
    struct VKCommandBufferContext
    {
        vk::UniqueCommandPool commandPool;
        vk::CommandBuffer commandBuffer = nullptr;
        vk::CommandBuffer prologue = nullptr;
        vk::CommandBuffer epilogue = nullptr;
    };

    class VKQueueCommandContextPool final
    {
    public:
        void init(vk::Device device, uint32_t queueFamilyIndex);
        void destroy();

        [[nodiscard]]
        eastl::shared_ptr<VKCommandBufferContext> acquire();

        void recycle(eastl::shared_ptr<VKCommandBufferContext> context);

    private:
        vk::Device mDevice = nullptr;
        uint32_t mQueueFamilyIndex = 0;
        eastl::vector<eastl::shared_ptr<VKCommandBufferContext>> mAvailableCommandContexts;
    };
} // namespace GVM::RHI::Vulkan
