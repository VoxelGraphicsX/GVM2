#include <GVMRHI/GVMRHI.hpp>
#include "SubmissionState.hpp"

namespace GVM::RHI
{
    QueueImpl::~QueueImpl() = default;

    Submission QueueImpl::submit(const eastl::vector<CommandEncoder> &, const eastl::vector<Submission> &)
    {
        throw std::logic_error("This queue does not implement completion-aware submission.");
    }

    Submission QueueImpl::makeSubmission(eastl::shared_ptr<Private::SubmissionState> state)
    {
        return Submission(eastl::move(state));
    }

    const eastl::shared_ptr<Private::SubmissionState> &QueueImpl::getSubmissionState(const Submission &submission)
    {
        if (!submission.mState)
        {
            throw std::invalid_argument("Queue dependency is an unbound Submission.");
        }
        return submission.mState;
    }
}
