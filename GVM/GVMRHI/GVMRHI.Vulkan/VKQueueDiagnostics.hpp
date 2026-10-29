#pragma once

#include "VKCommon.hpp"
#include "VKSubmissionManager.hpp"

#include <GVMRHI/GVMLogging.hpp>

namespace GVM::RHI::Vulkan
{
    class VKCommandEncoder;
}

namespace GVM::RHI::Vulkan::QueueDiagnostics
{
    /** Accumulates the diagnostic counters of one submitted encoder; no-op when GVM_LOGGING_ENABLED=0. */
    inline void collectEncoderStatistics(SubmissionStatistics &statistics, const VKCommandEncoder *encoder);

    [[nodiscard]]
    eastl::string buildRetiredSubmissionLogMessage(
        const eastl::string &queueName,
        const VKSubmissionManager::InFlightSubmission &submission,
        double lifetimeMs);

    [[nodiscard]]
    eastl::string buildTrackedSubmissionLogMessage(
        const eastl::string &queueName,
        uint64_t submissionId,
        size_t encoderCount,
        const SubmissionStatistics &statistics,
        size_t inFlightAfter,
        bool internalPresentSubmit);
} // namespace GVM::RHI::Vulkan::QueueDiagnostics

#include "VKCommandEncoder.hpp"

namespace GVM::RHI::Vulkan::QueueDiagnostics
{
    inline void collectEncoderStatistics(SubmissionStatistics &statistics, const VKCommandEncoder *encoder)
    {
#if GVM_LOGGING_ENABLED
        if (encoder != nullptr)
        {
            statistics.retainedBufferCount += encoder->getRetainedBufferCount();
            statistics.retainedTextureCount += encoder->getRetainedTextureCount();
            statistics.totalPassCount += encoder->getPassSummaryCount();
        }
#else
        (void)statistics;
        (void)encoder;
#endif
    }
} // namespace GVM::RHI::Vulkan::QueueDiagnostics
