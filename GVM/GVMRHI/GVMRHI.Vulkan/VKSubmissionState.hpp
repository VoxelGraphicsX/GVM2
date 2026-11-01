#pragma once
#include "VKSubmissionTimeline.hpp"
#include <GVMRHI/Private/SubmissionState.hpp>

namespace GVM::RHI::Vulkan
{
    /// Associates one accepted submission value with a retained queue timeline.
    class VKSubmissionState final : public Private::SubmissionState
    {
    public:
        /// Retains the timeline and value of work that the queue accepted.
        VKSubmissionState(eastl::shared_ptr<VKSubmissionTimeline> timeline, uint64_t value);
        /// Queries or waits without borrowing the queue or device wrapper.
        bool wait(uint64_t timeoutNanoseconds) const override;
        /// Returns the native GPU wait for a submission on the same retained device domain.
        vk::SemaphoreSubmitInfo dependency(const eastl::shared_ptr<DeviceLostState> &device) const;

    private:
        eastl::shared_ptr<VKSubmissionTimeline> mTimeline;
        uint64_t mValue;
    };
}
