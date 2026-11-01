#include "VKSubmissionState.hpp"

namespace GVM::RHI::Vulkan
{
    VKSubmissionState::VKSubmissionState(eastl::shared_ptr<VKSubmissionTimeline> timeline, uint64_t value)
        : mTimeline(eastl::move(timeline)), mValue(value)
    {
    }

    bool VKSubmissionState::wait(uint64_t timeoutNanoseconds) const
    {
        return mTimeline->wait(mValue, timeoutNanoseconds);
    }

    vk::SemaphoreSubmitInfo VKSubmissionState::dependency(const eastl::shared_ptr<DeviceLostState> &device) const
    {
        if (mTimeline->deviceState() != device)
        {
            throw std::invalid_argument("Submission dependency belongs to a different device.");
        }
        device->throwIfLost("Queue::submit");
        return vk::SemaphoreSubmitInfo{}.setSemaphore(mTimeline->nativeSemaphore()).setValue(mValue)
            .setStageMask(vk::PipelineStageFlagBits2::eAllCommands);
    }
}
