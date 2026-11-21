#include "VKSubmissionState.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/fixed_vector.h>
#include <functional>

namespace GVM::RHI::Vulkan
{
    VKSubmissionState::VKSubmissionState(eastl::shared_ptr<VKSubmissionTimeline> timeline, uint64_t value)
        : mTimeline(eastl::move(timeline)), mValue(value)
    {
    }

    uint64_t VKSubmissionState::timelineValue() const noexcept
    {
        return mValue;
    }

    bool VKSubmissionState::wait(uint64_t timeoutNanoseconds) const
    {
        return mTimeline->wait(mValue, timeoutNanoseconds);
    }

    bool VKSubmissionState::waitAny(
        eastl::span<const Submission> submissions,
        const Private::HostTimelineState *hostTimelineState,
        uint64_t hostTargetValue,
        uint64_t timeoutNanoseconds) const
    {
        const VKHostTimelineState *vkHostTimeline = nullptr;
        if (hostTimelineState != nullptr && hostTargetValue > 0u)
        {
            vkHostTimeline = dynamic_cast<const VKHostTimelineState *>(hostTimelineState);
            if (vkHostTimeline == nullptr)
            {
                throw std::invalid_argument("RHI::waitAny HostTimeline belongs to a different backend.");
            }
        }

        eastl::fixed_vector<VKSubmissionTimeline::WaitTarget, 4, true> targets;
        for (const auto &sub : submissions)
        {
            const auto &peer = Private::submissionStateOf(sub);
            const auto *vkPeer = dynamic_cast<const VKSubmissionState *>(peer.get());
            if (vkPeer == nullptr)
            {
                throw std::invalid_argument("RHI::waitAny submissions must belong to the same backend.");
            }
            const VKSubmissionTimeline *tl = vkPeer->mTimeline.get();
            bool merged = false;
            for (auto &existing : targets)
            {
                if (existing.timeline == tl)
                {
                    existing.value = eastl::min(existing.value, vkPeer->mValue);
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                targets.push_back({tl, vkPeer->mValue});
            }
        }

        eastl::sort(targets.begin(), targets.end(),
            [](const VKSubmissionTimeline::WaitTarget &a, const VKSubmissionTimeline::WaitTarget &b)
            {
                return std::less<const VKSubmissionTimeline *>{}(a.timeline, b.timeline);
            });

        return VKSubmissionTimeline::waitAny(
            eastl::span<const VKSubmissionTimeline::WaitTarget>(targets.data(), targets.size()),
            vkHostTimeline,
            hostTargetValue,
            timeoutNanoseconds);
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
