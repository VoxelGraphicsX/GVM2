#include "GraphDevice.hpp"
#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    GraphDevice::GraphDevice(RHI::DeviceImpl &device)
        : mDevice(device)
    {
        mQueues.push_back(device.getQueue({.prefer = RHI::QueuePreference::Graphics}));
        if (mQueues.front() == nullptr)
        {
            throw std::invalid_argument("GPU graph runtime requires a graphics-capable queue.");
        }
        for (const auto preference : {RHI::QueuePreference::Compute, RHI::QueuePreference::Transfer})
        {
            auto *candidate = device.getQueue({.prefer = preference});
            if (candidate == nullptr)
            {
                throw std::invalid_argument("GPU graph requires a compatible queue for each pass kind.");
            }
            const auto found = eastl::find(mQueues.begin(), mQueues.end(), candidate);
            const auto index = static_cast<uint32_t>(found - mQueues.begin());
            if (found == mQueues.end())
            {
                mQueues.push_back(candidate);
            }
            (preference == RHI::QueuePreference::Compute ? mCandidates.compute : mCandidates.transfer) = index;
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
    RHI::Queue GraphDevice::queue(uint32_t index) const
    {
        return mQueues.at(index);
    }
    PassQueues GraphDevice::candidates() const
    {
        return mCandidates;
    }
    std::unique_lock<std::recursive_mutex> GraphDevice::lock()
    {
        return std::unique_lock<std::recursive_mutex>(mMutex);
    }
}
