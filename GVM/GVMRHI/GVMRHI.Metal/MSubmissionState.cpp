#include "MSubmissionState.hpp"
#include <EASTL/algorithm.h>
#include <chrono>

namespace GVM::RHI::Metal
{
    MSubmissionState::MSubmissionState(eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers,
        NS::SharedPtr<MTL::Event> event, uint64_t eventValue, eastl::shared_ptr<DeviceLostState> device)
        : mBuffers(eastl::move(buffers)), mEvent(eastl::move(event)), mEventValue(eventValue),
          mLostState(eastl::move(device)), mRemainingBuffers(mBuffers.size()), mCallbacksFired(mBuffers.empty())
    {
    }

    void MSubmissionState::arm()
    {
        const eastl::weak_ptr<MSubmissionState> weak = shared_from_this();
        for (const auto &buffer : mBuffers)
        {
            buffer->addCompletedHandler([weak](MTL::CommandBuffer *)
            {
                if (const auto state = weak.lock())
                {
                    state->notifyCompletion();
                }
            });
        }
    }

    void MSubmissionState::notifyCompletion()
    {
        eastl::shared_ptr<MSubmissionState> releaseSelf;
        eastl::vector<CallbackEntry> callbacks;
        {
            std::lock_guard lock(mMutex);
            ++mNotifications;
            if (mRemainingBuffers > 0 && --mRemainingBuffers == 0)
            {
                mCallbacksFired = true;
                callbacks.swap(mCallbacks);
                releaseSelf = eastl::move(mCallbackSelfKeepAlive);
            }
        }
        mCompleted.notify_all();
        for (auto &entry : callbacks)
        {
            if (entry.callback)
            {
                entry.callback();
            }
        }
    }

    uint64_t MSubmissionState::timelineValue() const noexcept
    {
        return mEventValue;
    }

    uint64_t MSubmissionState::addWaiterCallback(eastl::function<void()> callback)
    {
        if (!callback)
        {
            return 0u;
        }
        bool fireInline = false;
        uint64_t token = 0u;
        {
            std::lock_guard lock(mMutex);
            if (mCallbacksFired)
            {
                fireInline = true;
            }
            else
            {
                token = mNextCallbackToken++;
                mCallbacks.push_back({token, eastl::move(callback)});
                if (!mCallbackSelfKeepAlive)
                {
                    mCallbackSelfKeepAlive = shared_from_this();
                }
            }
        }
        if (fireInline)
        {
            callback();
        }
        return token;
    }

    void MSubmissionState::removeWaiterCallback(uint64_t token) noexcept
    {
        if (token == 0u)
        {
            return;
        }
        eastl::shared_ptr<MSubmissionState> releaseSelf;
        eastl::function<void()> removed;
        {
            std::lock_guard lock(mMutex);
            for (size_t i = 0; i < mCallbacks.size(); ++i)
            {
                if (mCallbacks[i].token == token)
                {
                    removed = eastl::move(mCallbacks[i].callback);
                    mCallbacks[i] = eastl::move(mCallbacks.back());
                    mCallbacks.pop_back();
                    break;
                }
            }
            if (mCallbacks.empty())
            {
                releaseSelf = eastl::move(mCallbackSelfKeepAlive);
            }
        }
    }

    bool MSubmissionState::tryAddNativeCompletionCallback(eastl::function<void()> callback)
    {
        (void)addWaiterCallback(eastl::move(callback));
        return true;
    }

    bool MSubmissionState::checkComplete() const
    {
        mLostState->throwIfLost("Submission::wait");
        bool complete = true;
        for (const auto &buffer : mBuffers)
        {
            const auto status = buffer->status();
            if (status == MTL::CommandBufferStatusError)
            {
                const auto *error = buffer->error();
                const char *message = error != nullptr && error->localizedDescription() != nullptr
                    ? error->localizedDescription()->cString(NS::UTF8StringEncoding) : "Metal submission failed.";
                mLostState->markLost(DeviceLostReason::GpuFault, message != nullptr ? message : "Metal submission failed.");
                mLostState->throwIfLost("Submission::wait");
            }
            complete = complete && status == MTL::CommandBufferStatusCompleted;
        }
        return complete;
    }

    bool MSubmissionState::wait(uint64_t timeoutNanoseconds) const
    {
        if (timeoutNanoseconds == 0u)
        {
            return checkComplete();
        }
        const bool infinite = timeoutNanoseconds == std::numeric_limits<uint64_t>::max();
        const auto bounded = eastl::min(timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        const auto deadline = infinite
            ? std::chrono::steady_clock::time_point::max()
            : (std::chrono::steady_clock::now() + std::chrono::nanoseconds(bounded));
        for (;;)
        {
            uint64_t notifications = 0;
            bool callbacksFired = false;
            {
                std::lock_guard lock(mMutex);
                notifications = mNotifications;
                callbacksFired = mCallbacksFired;
            }
            if (checkComplete())
            {
                return true;
            }
            if (!infinite && std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::unique_lock lock(mMutex);
            if (notifications == mNotifications && (!mCallbacksFired || !callbacksFired))
            {
                if (infinite)
                {
                    mCompleted.wait(lock);
                }
                else
                {
                    mCompleted.wait_until(lock, deadline);
                }
            }
        }
    }

    bool MSubmissionState::waitAny(
        eastl::span<const Submission> submissions,
        const Private::HostTimelineState *hostTimelineState,
        uint64_t hostTargetValue,
        uint64_t timeoutNanoseconds) const
    {
        auto checkAnySatisfied = [&]() -> bool
        {
            if (hostTimelineState != nullptr && hostTimelineState->currentValue() >= hostTargetValue)
            {
                return true;
            }
            for (const auto &sub : submissions)
            {
                const auto &peer = Private::submissionStateOf(sub);
                const auto *metalPeer = dynamic_cast<const MSubmissionState *>(peer.get());
                if (metalPeer == nullptr)
                {
                    throw std::invalid_argument("RHI::waitAny submissions must belong to the same backend.");
                }
                if (metalPeer->checkComplete())
                {
                    return true;
                }
            }
            return false;
        };

        if (checkAnySatisfied())
        {
            return true;
        }
        if (timeoutNanoseconds == 0u)
        {
            return false;
        }

        struct SharedGate
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool triggered = false;
            void notify()
            {
                {
                    std::lock_guard lock(mutex);
                    triggered = true;
                }
                cv.notify_all();
            }
        };
        auto gate = eastl::make_shared<SharedGate>();

        struct ListenerGuard
        {
            eastl::vector<eastl::pair<MSubmissionState *, uint64_t>> peerTokens;
            const Private::HostTimelineState *hostTimeline = nullptr;
            uint64_t hostToken = 0u;
            ~ListenerGuard()
            {
                if (hostTimeline != nullptr && hostToken != 0u)
                {
                    hostTimeline->removeListener(hostToken);
                }
                for (const auto &entry : peerTokens)
                {
                    if (entry.first != nullptr && entry.second != 0u)
                    {
                        entry.first->removeWaiterCallback(entry.second);
                    }
                }
            }
        } guard;
        guard.peerTokens.reserve(submissions.size());
        guard.hostTimeline = hostTimelineState;

        for (const auto &sub : submissions)
        {
            auto *metalPeer = static_cast<MSubmissionState *>(Private::submissionStateOf(sub).get());
            const uint64_t token = metalPeer->addWaiterCallback([gate]() { gate->notify(); });
            if (token != 0u)
            {
                guard.peerTokens.emplace_back(metalPeer, token);
            }
        }
        if (hostTimelineState != nullptr && hostTargetValue > 0u)
        {
            guard.hostToken = hostTimelineState->onTargetValue(hostTargetValue, [gate]() { gate->notify(); });
        }
        if (checkAnySatisfied())
        {
            return true;
        }
        std::unique_lock lock(gate->mutex);
        if (timeoutNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            gate->cv.wait(lock, [&gate] { return gate->triggered; });
        }
        else
        {
            const uint64_t bounded = eastl::min(
                timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
            (void)gate->cv.wait_for(lock, std::chrono::nanoseconds(bounded), [&gate] { return gate->triggered; });
        }
        lock.unlock();
        return checkAnySatisfied();
    }

    MSubmissionDependency MSubmissionState::dependency(const eastl::shared_ptr<DeviceLostState> &device) const
    {
        if (device != mLostState)
        {
            throw std::invalid_argument("Submission dependency belongs to a different device.");
        }
        device->throwIfLost("Queue::submit");
        return {mEvent.get(), mEventValue};
    }
}
