#include <GVMRHI/GVMRHI.hpp>
#include "SubmissionState.hpp"
#include <EASTL/algorithm.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace GVM::RHI
{
    namespace
    {
        class HostCvTimelineState final : public Private::HostTimelineState
        {
        public:
            uint64_t signal() noexcept override
            {
                uint64_t next = 0;
                eastl::vector<eastl::function<void()>> ready;
                {
                    std::lock_guard lock(mMutex);
                    next = mValue.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                    for (size_t i = 0; i < mListeners.size();)
                    {
                        if (mListeners[i].target <= next)
                        {
                            ready.push_back(eastl::move(mListeners[i].callback));
                            mListeners[i] = eastl::move(mListeners.back());
                            mListeners.pop_back();
                        }
                        else
                        {
                            ++i;
                        }
                    }
                }
                mCondition.notify_all();
                for (auto &cb : ready)
                {
                    if (cb)
                    {
                        cb();
                    }
                }
                return next;
            }

            uint64_t currentValue() const noexcept override
            {
                return mValue.load(std::memory_order_acquire);
            }

            bool wait(uint64_t targetValue, uint64_t timeoutNanoseconds) const override
            {
                if (currentValue() >= targetValue)
                {
                    return true;
                }
                if (timeoutNanoseconds == 0u)
                {
                    return false;
                }
                std::unique_lock lock(mMutex);
                if (timeoutNanoseconds == std::numeric_limits<uint64_t>::max())
                {
                    mCondition.wait(lock, [this, targetValue] { return currentValue() >= targetValue; });
                    return true;
                }
                const uint64_t bounded = eastl::min(
                    timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
                return mCondition.wait_for(lock, std::chrono::nanoseconds(bounded),
                    [this, targetValue] { return currentValue() >= targetValue; });
            }

            uint64_t onTargetValue(uint64_t targetValue, eastl::function<void()> callback) const override
            {
                if (!callback)
                {
                    return 0u;
                }
                bool fireNow = false;
                uint64_t token = 0u;
                {
                    std::lock_guard lock(mMutex);
                    if (currentValue() >= targetValue)
                    {
                        fireNow = true;
                    }
                    else
                    {
                        token = mNextToken++;
                        mListeners.push_back({token, targetValue, eastl::move(callback)});
                    }
                }
                if (fireNow)
                {
                    callback();
                }
                return token;
            }

            void removeListener(uint64_t token) const noexcept override
            {
                if (token == 0u)
                {
                    return;
                }
                eastl::function<void()> removed;
                {
                    std::lock_guard lock(mMutex);
                    for (size_t i = 0; i < mListeners.size(); ++i)
                    {
                        if (mListeners[i].token == token)
                        {
                            removed = eastl::move(mListeners[i].callback);
                            mListeners[i] = eastl::move(mListeners.back());
                            mListeners.pop_back();
                            break;
                        }
                    }
                }
            }

        private:
            struct Listener
            {
                uint64_t token = 0;
                uint64_t target = 0;
                eastl::function<void()> callback;
            };
            mutable std::mutex mMutex;
            mutable std::condition_variable mCondition;
            mutable eastl::vector<Listener> mListeners;
            mutable uint64_t mNextToken = 1u;
            std::atomic<uint64_t> mValue{0};
        };
    } // namespace

    Queue DeviceImpl::getQueue(const QueueDescriptor &descriptor)
    {
        switch (descriptor.prefer)
        {
        case QueuePreference::Graphics:
        case QueuePreference::Compute:
        case QueuePreference::Transfer:
            return getMainQueue();
        default:
            throw std::invalid_argument("Device::getQueue requires a valid queue preference.");
        }
    }

    PassCounterQuerySupport DeviceImpl::getPassCounterQuerySupport() const
    {
        PassCounterQuerySupport support = {};
        support.supported = False;
        support.backendName = "unknown";
        support.unsupportedReason = "Pass counter queries are not implemented by this device.";
        return support;
    }

    HostTimeline DeviceImpl::createHostTimeline()
    {
        return makeHostTimeline(eastl::make_shared<HostCvTimelineState>());
    }

    HostTimeline DeviceImpl::makeHostTimeline(eastl::shared_ptr<Private::HostTimelineState> state)
    {
        return HostTimeline(eastl::move(state));
    }

    DeviceImpl::~DeviceImpl() = default;
}
