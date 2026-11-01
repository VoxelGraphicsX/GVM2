#pragma once
#include <cstdint>

namespace GVM::RHI::Private
{
    /// Provides backend completion for an accepted submission, including retained-handle lifetime.
    class SubmissionState
    {
    public:
        /// Releases completion storage after the last public handle and dependent submission retire.
        virtual ~SubmissionState();
        /// Queries or waits for successful completion; zero never blocks and failures throw.
        virtual bool wait(uint64_t timeoutNanoseconds) const = 0;
    };
}
