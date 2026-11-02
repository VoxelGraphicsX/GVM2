#include "GraphDevice.hpp"
#include <EASTL/vector.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    GraphDevice::GraphDevice(RHI::DeviceImpl &device)
        : mDevice(device), mQueue(device.getQueue({.prefer = RHI::QueuePreference::Graphics}))
    {
        if (mQueue == nullptr)
        {
            throw std::invalid_argument("GPU graph runtime requires a graphics-capable queue.");
        }
    }

    eastl::shared_ptr<GraphDevice> GraphDevice::acquire(RHI::DeviceImpl &device)
    {
        static std::mutex registryMutex;
        static eastl::vector<eastl::weak_ptr<GraphDevice>> devices;
        std::lock_guard lock(registryMutex);
        for (auto current = devices.begin(); current != devices.end();)
        {
            if (const auto retained = current->lock())
            {
                if (&retained->mDevice == &device)
                {
                    return retained;
                }
                ++current;
            }
            else
            {
                current = devices.erase(current);
            }
        }
        auto result = eastl::shared_ptr<GraphDevice>(new GraphDevice(device));
        devices.push_back(result);
        return result;
    }

    RHI::DeviceImpl &GraphDevice::device() const
    {
        return mDevice;
    }
    RHI::Queue GraphDevice::queue() const
    {
        return mQueue;
    }
    std::unique_lock<std::recursive_mutex> GraphDevice::lock()
    {
        return std::unique_lock<std::recursive_mutex>(mMutex);
    }
}
