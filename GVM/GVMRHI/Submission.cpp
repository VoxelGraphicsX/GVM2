#include "Submission.hpp"
#include "Private/SubmissionState.hpp"
#include <stdexcept>

namespace GVM::RHI
{
    HostTimeline::HostTimeline() = default;
    HostTimeline::~HostTimeline() = default;

    HostTimeline::HostTimeline(eastl::shared_ptr<Private::HostTimelineState> state)
        : mState(eastl::move(state))
    {
    }

    bool HostTimeline::isValid() const noexcept
    {
        return static_cast<bool>(mState);
    }

    uint64_t HostTimeline::signal() const noexcept
    {
        return mState ? mState->signal() : 0u;
    }

    uint64_t HostTimeline::currentValue() const noexcept
    {
        return mState ? mState->currentValue() : 0u;
    }

    Submission::Submission() = default;
    Submission::~Submission() = default;

    Submission::Submission(eastl::shared_ptr<Private::SubmissionState> state)
        : mState(eastl::move(state))
    {
    }

    bool Submission::isValid() const noexcept
    {
        return static_cast<bool>(mState);
    }

    uint64_t Submission::timelineValue() const
    {
        if (!mState)
        {
            throw std::logic_error("Submission is not bound to accepted GPU work.");
        }
        return mState->timelineValue();
    }

    bool Submission::isComplete() const
    {
        if (!mState)
        {
            throw std::logic_error("Submission is not bound to accepted GPU work.");
        }
        return mState->wait(0);
    }

    bool Submission::wait(uint64_t timeoutNanoseconds) const
    {
        if (!mState)
        {
            throw std::logic_error("Submission is not bound to accepted GPU work.");
        }
        return mState->wait(timeoutNanoseconds);
    }

    bool Submission::hasNativeCompletionCallback() const noexcept
    {
        return mState && mState->hasNativeCompletionCallback();
    }

    bool Submission::tryAddNativeCompletionCallback(eastl::function<void()> callback) const
    {
        if (!mState)
        {
            throw std::logic_error("Submission is not bound to accepted GPU work.");
        }
        return mState->tryAddNativeCompletionCallback(eastl::move(callback));
    }

    bool waitAny(
        eastl::span<const Submission> submissions,
        const HostTimeline *hostTimeline,
        uint64_t hostTargetValue,
        uint64_t timeoutNanoseconds)
    {
        const Private::HostTimelineState *hostState =
            (hostTimeline != nullptr && hostTimeline->isValid() && hostTargetValue > 0u)
                ? Private::hostTimelineStateOf(*hostTimeline).get()
                : nullptr;
        if (hostState != nullptr && hostState->currentValue() >= hostTargetValue)
        {
            return true;
        }
        if (submissions.empty())
        {
            return hostState != nullptr ? hostState->wait(hostTargetValue, timeoutNanoseconds) : true;
        }
        const auto &first = Private::submissionStateOf(submissions.front());
        if (!first)
        {
            throw std::invalid_argument("RHI::waitAny received an unbound Submission.");
        }
        return first->waitAny(submissions, hostState, hostTargetValue, timeoutNanoseconds);
    }
}
