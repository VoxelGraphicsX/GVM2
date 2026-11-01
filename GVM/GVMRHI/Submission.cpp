#include "Submission.hpp"
#include "Private/SubmissionState.hpp"
#include <stdexcept>

namespace GVM::RHI
{
    Submission::Submission() = default;
    Submission::~Submission() = default;

    Submission::Submission(eastl::shared_ptr<Private::SubmissionState> state)
        : mState(eastl::move(state))
    {
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
}
