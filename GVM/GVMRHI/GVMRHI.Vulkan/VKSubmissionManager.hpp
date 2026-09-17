#pragma once

#include "VKCommon.hpp"
#include "VKFencePool.hpp"

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <atomic>
#include <chrono>

namespace GVM::RHI::Vulkan
{
    class VKSubmissionManager final
    {
    public:
        struct InFlightSubmission
        {
            uint64_t submissionId = 0u;
            vk::Fence fence = nullptr;
            vk::UniqueFence ownedFence;
            VKSubmissionCompletion completion;
            eastl::vector<CommandEncoder> encoders;
            size_t diagnosticRetainedBufferCount = 0u;
            size_t diagnosticRetainedTextureCount = 0u;
            bool internalPresentSubmit = false;
            std::chrono::steady_clock::time_point submittedAt = {};
        };

        struct SubmissionFenceStatusSnapshot
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

        // Executes one already-assembled queue submit and begins fence-backed tracking.
        [[nodiscard]]
        VKSubmissionCompletion executeTrackedSubmit(
            vk::Queue queue,
            const eastl::vector<vk::CommandBuffer> &commandBuffers,
            const vk::SubmitInfo *submitTemplate,
            const eastl::vector<vk::Semaphore> &waitSemaphores,
            const eastl::vector<vk::PipelineStageFlags> &waitStageMasks,
            const eastl::vector<vk::Semaphore> &signalSemaphores,
            const eastl::vector<CommandEncoder> &encoders,
            bool internalPresentSubmit,
            bool countUserSubmit,
            size_t diagnosticRetainedBufferCount,
            size_t diagnosticRetainedTextureCount,
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
        VKSubmissionCompletion getMostRecentSubmissionCompletion() const;

        [[nodiscard]]
        VKSubmissionCompletion getOldestSubmissionCompletion() const;

        [[nodiscard]]
        uint64_t getLastRetiredSubmissionId() const;

        [[nodiscard]]
        size_t pollCompletedSubmissions(const eastl::function<void(InFlightSubmission &submission)> &onRetired);

        void waitForSubmission(
            const VKSubmissionCompletion &completion,
            const eastl::function<void(InFlightSubmission &submission)> &onRetired);

        [[nodiscard]]
        bool trySampleSubmissionFenceStatus(const VKSubmissionCompletion &completion, SubmissionFenceStatusSnapshot &outStatus) const;

    private:
        [[nodiscard]]
        VKSubmissionCompletion trackSubmission(
            uint64_t submissionId,
            vk::Fence fence,
            vk::UniqueFence ownedFence,
            const eastl::vector<CommandEncoder> &encoders,
            size_t diagnosticRetainedBufferCount,
            size_t diagnosticRetainedTextureCount,
            std::chrono::steady_clock::time_point submittedAt,
            bool internalPresentSubmit);
        VKDevice *mDevice = nullptr;
        eastl::string mQueueName;
        eastl::vector<InFlightSubmission> mInFlightSubmissions;
        VKFencePool mFencePool;
        uint64_t mNextSubmissionId = 1u;
        uint64_t mUserSubmitCount = 0u;
        uint64_t mBackendSubmitCount = 0u;
        uint64_t mPresentInternalSubmitCount = 0u;
        VKSubmissionCompletion mIdleSubmissionCompletion;
        VKSubmissionCompletion mMostRecentSubmissionCompletion;
        std::atomic<uint64_t> mLastRetiredSubmissionId = 0u;
        uint64_t mLastFrontNotReadyLoggedSubmissionId = 0u;
        double mLastFrontNotReadyLoggedAgeMs = 0.0;
    };
} // namespace GVM::RHI::Vulkan
