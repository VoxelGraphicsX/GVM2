#include "DeviceLostState.hpp"

namespace GVM::RHI
{
    void DeviceLostState::setCallback(eastl::function<void(const DeviceLostInfo &)> callback)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCallback = eastl::move(callback);
    }

    bool DeviceLostState::isLost() const
    {
        return mLost.load(eastl::memory_order_acquire);
    }

    eastl::optional<DeviceLostInfo> DeviceLostState::getInfo() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mInfo;
    }

    void DeviceLostState::throwIfLost(const char *apiName) const
    {
        if (!mLost.load(eastl::memory_order_acquire))
        {
            return;
        }
        const eastl::optional<DeviceLostInfo> info = getInfo();
        if (!info.has_value())
        {
            return;
        }
        throw DeviceLostError((eastl::string(apiName) + ": device lost: " + info->message).c_str());
    }

    void DeviceLostState::markLost(DeviceLostReason reason, eastl::string_view message)
    {
        eastl::function<void(const DeviceLostInfo &)> callback;
        DeviceLostInfo info;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mInfo.has_value())
            {
                return;
            }
            info.reason = reason;
            info.message = message;
            mInfo = info;
            mLost.store(true, eastl::memory_order_release);
            callback = mCallback;
        }
        if (callback)
        {
            callback(info);
        }
    }
} // namespace GVM::RHI
