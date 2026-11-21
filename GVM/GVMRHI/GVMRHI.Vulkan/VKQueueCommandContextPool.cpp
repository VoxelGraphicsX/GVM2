#include "VKQueueCommandContextPool.hpp"

namespace GVM::RHI::Vulkan
{
    void VKQueueCommandContextPool::init(vk::Device device, uint32_t queueFamilyIndex)
    {
        std::lock_guard lock(mMutex);
        mDevice = device;
        mQueueFamilyIndex = queueFamilyIndex;
        mAvailableCommandContexts.clear();
    }

    void VKQueueCommandContextPool::destroy()
    {
        eastl::vector<eastl::shared_ptr<VKCommandBufferContext>> toDestroy;
        {
            std::lock_guard lock(mMutex);
            toDestroy.swap(mAvailableCommandContexts);
            mQueueFamilyIndex = 0;
            mDevice = nullptr;
        }
        toDestroy.clear();
    }

    eastl::shared_ptr<VKCommandBufferContext> VKQueueCommandContextPool::tryAcquire()
    {
        vk::Device device = nullptr;
        eastl::shared_ptr<VKCommandBufferContext> context;
        {
            std::lock_guard lock(mMutex);
            if (!mDevice)
            {
                throw std::runtime_error("VKQueueCommandContextPool::acquire was called before init.");
            }
            if (mAvailableCommandContexts.empty())
            {
                return nullptr;
            }
            device = mDevice;
            context = eastl::move(mAvailableCommandContexts.back());
            mAvailableCommandContexts.pop_back();
        }
        if (context && context->commandPool)
        {
            device.resetCommandPool(context->commandPool.get());
        }
        return context;
    }

    eastl::shared_ptr<VKCommandBufferContext> VKQueueCommandContextPool::acquire()
    {
        if (auto reused = tryAcquire())
        {
            return reused;
        }

        vk::Device device = nullptr;
        uint32_t queueFamilyIndex = 0;
        {
            std::lock_guard lock(mMutex);
            if (!mDevice)
            {
                throw std::runtime_error("VKQueueCommandContextPool::acquire was called before init.");
            }
            device = mDevice;
            queueFamilyIndex = mQueueFamilyIndex;
        }

        auto context = eastl::make_shared<VKCommandBufferContext>();
        vk::CommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolCreateInfo.queueFamilyIndex = queueFamilyIndex;
        context->commandPool = device.createCommandPoolUnique(poolCreateInfo);

        vk::CommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.commandPool = context->commandPool.get();
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;
        context->commandBuffer = device.allocateCommandBuffers(allocateInfo).front();
        return context;
    }

    void VKQueueCommandContextPool::recycle(eastl::shared_ptr<VKCommandBufferContext> context)
    {
        if (!context)
        {
            return;
        }
        std::lock_guard lock(mMutex);
        if (!mDevice)
        {
            return;
        }
        mAvailableCommandContexts.push_back(eastl::move(context));
    }
} // namespace GVM::RHI::Vulkan
