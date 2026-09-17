#include "VKFencePool.hpp"

#include "VKDevice.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKFencePool::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKFencePool::init requires a valid Vulkan device.");
        }

        mDevice = device;
        mFences.clear();
    }

    vk::UniqueFence VKFencePool::acquire()
    {
        if (mDevice == nullptr)
        {
            throw makeRuntimeError("VKFencePool::acquire was called before init.");
        }

        if (!mFences.empty())
        {
            vk::UniqueFence fence = eastl::move(mFences.back());
            mFences.pop_back();
            mDevice->getNativeDevice().resetFences(fence.get());
            return fence;
        }

        vk::FenceCreateInfo fenceCreateInfo = {};
        return mDevice->getNativeDevice().createFenceUnique(fenceCreateInfo);
    }

    void VKFencePool::recycle(vk::UniqueFence fence)
    {
        if (fence)
        {
            mFences.push_back(eastl::move(fence));
        }
    }

    void VKFencePool::clear()
    {
        mFences.clear();
    }
} // namespace GVM::RHI::Vulkan
