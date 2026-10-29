#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/atomic.h>
#include <EASTL/functional.h>

#include <mutex>

namespace GVM::RHI
{
    /** Owns the per-device lost state. The first markLost wins; the registered callback fires once,
        outside the state's own mutex. Queues, swapchains, and readbacks reach the shared state
        through their owning device. */
    class DeviceLostState final
    {
    public:
        /** Creates the state with no callback; setCallback copies it before any GPU work. */
        DeviceLostState() = default;
        /** Stores the callback copied from the owning instance's descriptor; call before any GPU work. */
        void setCallback(eastl::function<void(const DeviceLostInfo &)> callback);

        /** Returns true once a loss was reported. */
        [[nodiscard]] bool isLost() const;
        /** Returns the recorded loss info; empty while the device is healthy. */
        [[nodiscard]] eastl::optional<DeviceLostInfo> getInfo() const;
        /** Throws DeviceLostError("<apiName>: device lost: <message>") when the device is lost. */
        void throwIfLost(const char *apiName) const;
        /** Records a loss; the first call wins and fires the stored callback once. */
        void markLost(DeviceLostReason reason, eastl::string_view message);

    private:
        mutable std::mutex mMutex;
        eastl::optional<DeviceLostInfo> mInfo;
        eastl::function<void(const DeviceLostInfo &)> mCallback;
        eastl::atomic<bool> mLost{false};
    };
} // namespace GVM::RHI
