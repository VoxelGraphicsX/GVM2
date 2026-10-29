#pragma once

#include "VKCommon.hpp"

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <EASTL/atomic.h>
#include <chrono>

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    namespace QueueDiagnostics
    {
        /** Per-submission diagnostic counters collected for submit logging; empty when GVM_LOGGING_ENABLED=0. */
        struct SubmissionStatistics
        {
#if GVM_LOGGING_ENABLED
            size_t retainedBufferCount = 0u;
            size_t retainedTextureCount = 0u;
            size_t totalPassCount = 0u;
#endif
        };
    } // namespace QueueDiagnostics

    class VKSubmissionManager final
    {
    public:
        struct InFlightSubmission
        {
            uint64_t submissionId = 0u;
            eastl::vector<CommandEncoder> encoders;
            QueueDiagnostics::SubmissionStatistics statistics = {};
            bool internalPresentSubmit = false;
            std::chrono::steady_clock::time_point submittedAt = {};
        };

        struct SubmissionStatusSnapshot
        {
            uint64_t submissionId = 0u;
            double ageMs = 0.0;
            bool tracked = false;
            bool completed = false;
            bool signaled = false;
            bool internalPresentSubmit = false;
        };

        void init(VKDevice &device, const eastl::string &queueName);
        void destroy();

        // Executes one already-assembled queue submit and begins timeline-backed tracking.
        [[nodiscard]]
        VKSubmissionSerial executeTrackedSubmit(
            vk::Queue queue,
            const eastl::vector<vk::CommandBuffer> &commandBuffers,
            const eastl::vector<vk::SemaphoreSubmitInfo> &waitSemaphores,
            const eastl::vector<vk::SemaphoreSubmitInfo> &signalSemaphores,
            const eastl::vector<CommandEncoder> &encoders,
            bool internalPresentSubmit,
            bool countUserSubmit,
            const QueueDiagnostics::SubmissionStatistics &statistics,
            const char *submitKind);

        [[nodiscard]]
        size_t getInFlightCount() const;

        [[nodiscard]]
        uint64_t getUserSubmitCount() const;

        [[nodiscard]]
        uint64_t getBackendSubmitCount() const;

        [[nodiscard]]
        uint64_t getPresentInternalSubmitCount() const;

        [[nodiscard]]
        VKSubmissionSerial getMostRecentSubmissionSerial() const;

        [[nodiscard]]
        VKSubmissionSerial getOldestSubmissionSerial() const;

        [[nodiscard]]
        uint64_t getLastRetiredSubmissionId() const;

        /** Returns whether the submission with the given serial has been retired; serial 0 is always retired. */
        [[nodiscard]]
        bool isRetired(VKSubmissionSerial serial) const;

        [[nodiscard]]
        size_t pollCompletedSubmissions(const eastl::function<void(InFlightSubmission &submission)> &onRetired);

        /** Unlocked body of waitForSubmission; the public method maps vk::DeviceLostError to the device-lost state. */
        void waitForSubmissionImpl(
            VKSubmissionSerial serial,
            const eastl::function<void(InFlightSubmission &submission)> &onRetired);

        void waitForSubmission(
            VKSubmissionSerial serial,
            const eastl::function<void(InFlightSubmission &submission)> &onRetired);

        [[nodiscard]]
        bool trySampleSubmissionStatus(VKSubmissionSerial serial, SubmissionStatusSnapshot &outStatus) const;

    private:
        VKDevice *mDevice = nullptr;
        eastl::string mQueueName;
        eastl::vector<InFlightSubmission> mInFlightSubmissions;
        vk::UniqueSemaphore mTimeline;
        uint64_t mNextSubmissionId = 1u;
        uint64_t mUserSubmitCount = 0u;
        uint64_t mBackendSubmitCount = 0u;
        uint64_t mPresentInternalSubmitCount = 0u;
        VKSubmissionSerial mMostRecentSubmissionSerial = 0u;
        eastl::atomic<uint64_t> mLastRetiredSubmissionId = 0u;
        uint64_t mLastFrontNotReadyLoggedSubmissionId = 0u;
        double mLastFrontNotReadyLoggedAgeMs = 0.0;
    };
} // namespace GVM::RHI::Vulkan
