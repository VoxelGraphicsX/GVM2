#pragma once

#include "VKCommon.hpp"
#include "VKSubmissionManager.hpp"

namespace GVM::RHI::Vulkan::QueueDiagnostics
{
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
        size_t totalPassCount,
        size_t retainedBufferCount,
        size_t retainedTextureCount,
        size_t inFlightAfter,
        bool internalPresentSubmit);
} // namespace GVM::RHI::Vulkan::QueueDiagnostics
