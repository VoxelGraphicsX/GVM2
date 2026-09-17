#include "VKQueueCommandContextPool.hpp"

namespace GVM::RHI::Vulkan
{
    void VKQueueCommandContextPool::init(vk::Device device, uint32_t queueFamilyIndex)
    {
        mDevice = device;
        mQueueFamilyIndex = queueFamilyIndex;
        mAvailableCommandContexts.clear();
    }

    void VKQueueCommandContextPool::destroy()
    {
        mAvailableCommandContexts.clear();
        mQueueFamilyIndex = 0;
        mDevice = nullptr;
    }

    eastl::shared_ptr<VKCommandBufferContext> VKQueueCommandContextPool::acquire()
    {
        if (!mDevice)
        {
            throw makeRuntimeError("VKQueueCommandContextPool::acquire was called before init.");
        }

        if (!mAvailableCommandContexts.empty())
        {
            eastl::shared_ptr<VKCommandBufferContext> context = eastl::move(mAvailableCommandContexts.back());
            mAvailableCommandContexts.pop_back();
            if (context && context->commandPool)
            {
                mDevice.resetCommandPool(context->commandPool.get());
            }
            return context;
        }

        auto context = eastl::make_shared<VKCommandBufferContext>();
        vk::CommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolCreateInfo.queueFamilyIndex = mQueueFamilyIndex;
        context->commandPool = mDevice.createCommandPoolUnique(poolCreateInfo);

        vk::CommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.commandPool = context->commandPool.get();
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;
        context->commandBuffer = mDevice.allocateCommandBuffers(allocateInfo).front();
        return context;
    }

    void VKQueueCommandContextPool::recycle(eastl::shared_ptr<VKCommandBufferContext> context)
    {
        if (!context || !mDevice)
        {
            return;
        }
        mAvailableCommandContexts.push_back(eastl::move(context));
    }

    size_t VKQueueCommandContextPool::availableContextCount() const
    {
        return mAvailableCommandContexts.size();
    }
} // namespace GVM::RHI::Vulkan
