#include "MSubmissionState.hpp"
#include <EASTL/algorithm.h>
#include <chrono>

namespace GVM::RHI::Metal
{
    MSubmissionState::MSubmissionState(eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers,
        NS::SharedPtr<MTL::Event> event, eastl::shared_ptr<DeviceLostState> device)
        : mBuffers(eastl::move(buffers)), mEvent(eastl::move(event)), mLostState(eastl::move(device))
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
        {
            std::lock_guard lock(mMutex);
            ++mNotifications;
        }
        mCompleted.notify_all();
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
        const auto started = std::chrono::steady_clock::now();
        for (;;)
        {
            uint64_t notifications;
            {
                std::lock_guard lock(mMutex);
                notifications = mNotifications;
            }
            if (checkComplete())
            {
                return true;
            }
            const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
            if (elapsed >= timeoutNanoseconds)
            {
                return false;
            }
            const auto interval = eastl::min(timeoutNanoseconds - elapsed, uint64_t{50000000});
            std::unique_lock lock(mMutex);
            if (notifications == mNotifications)
            {
                mCompleted.wait_for(lock, std::chrono::nanoseconds(interval));
            }
        }
    }

    MTL::Event *MSubmissionState::dependency(const eastl::shared_ptr<DeviceLostState> &device) const
    {
        if (device != mLostState)
        {
            throw std::invalid_argument("Submission dependency belongs to a different device.");
        }
        device->throwIfLost("Queue::submit");
        return mEvent.get();
    }
}
