#include "VKSubmissionManager.hpp"

#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "Private/TimeUtils.hpp"

#include <EASTL/shared_ptr.h>

#include <GVMRHI/GVMCpuProbe.hpp>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view SubmissionManagerLogCategory = "gvmrhi.vulkan.submission_manager";
        constexpr double kFrontNotReadyLogThresholdMs = 50.0;
        constexpr double kFrontNotReadyLogIntervalMs = 100.0;
        constexpr uint64_t kHostGpuWaitTimeoutMilliseconds = 2000u;
        constexpr uint64_t kHostGpuWaitTimeoutNanoseconds = 2'000'000'000ull;

        [[noreturn]] void throwQueueResultException(
            vk::Result result,
            const char *apiName,
            const eastl::string &queueName,
            size_t inFlightSubmissionCount)
        {
            eastl::string message = eastl::string{apiName} + " failed for queue='" + queueName +
                                    " inFlight=" + eastl::to_string(inFlightSubmissionCount) +
                                    " vk::Result=" + eastl::to_string(static_cast<int32_t>(result));
            vk::detail::throwResultException(result, message.c_str());
        }

        [[nodiscard]]
        double submissionAgeMilliseconds(const VKSubmissionManager::InFlightSubmission &submission)
        {
            return Internal::elapsedMilliseconds(submission.submittedAt);
        }
        [[nodiscard]]
        vk::Result queryFenceStatus(
            const VKDevice &device,
            vk::Fence fence,
            const char *apiName,
            const eastl::string &queueName,
            size_t inFlightSubmissionCount)
        {
            const vk::Result result = static_cast<vk::Result>(VULKAN_HPP_DEFAULT_DISPATCHER.vkGetFenceStatus(
                static_cast<VkDevice>(device.getNativeDevice()),
                static_cast<VkFence>(fence)));
            if (result != vk::Result::eSuccess && result != vk::Result::eNotReady)
            {
                throwQueueResultException(result, apiName, queueName, inFlightSubmissionCount);
            }
            return result;
        }
    } // namespace

    void VKSubmissionManager::init(VKDevice &device, const eastl::string &queueName)
    {
        mDevice = &device;
        mQueueName = queueName;
        mInFlightSubmissions.clear();
        mFencePool.init(&device);
        mNextSubmissionId = 1u;
        mUserSubmitCount = 0u;
        mBackendSubmitCount = 0u;
        mPresentInternalSubmitCount = 0u;
        mIdleSubmissionCompletion = eastl::make_shared<VKSubmissionCompletionState>();
        mIdleSubmissionCompletion->completed.store(true, std::memory_order_release);
        mMostRecentSubmissionCompletion = mIdleSubmissionCompletion;
        mLastRetiredSubmissionId.store(0u, std::memory_order_release);
        mLastFrontNotReadyLoggedSubmissionId = 0u;
        mLastFrontNotReadyLoggedAgeMs = 0.0;
    }

    void VKSubmissionManager::destroy()
    {
        mInFlightSubmissions.clear();
        mFencePool.clear();
        mMostRecentSubmissionCompletion.reset();
        mIdleSubmissionCompletion.reset();
        mQueueName.clear();
        mDevice = nullptr;
        mNextSubmissionId = 1u;
        mUserSubmitCount = 0u;
        mBackendSubmitCount = 0u;
        mPresentInternalSubmitCount = 0u;
        mLastRetiredSubmissionId.store(0u, std::memory_order_release);
        mLastFrontNotReadyLoggedSubmissionId = 0u;
        mLastFrontNotReadyLoggedAgeMs = 0.0;
    }

    VKSubmissionCompletion VKSubmissionManager::executeTrackedSubmit(
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
        const char *submitKind)
    {
        if (mDevice == nullptr || queue == vk::Queue{})
        {
            throw makeInvalidArgument("VKSubmissionManager::executeTrackedSubmit requires a valid device and queue.");
        }
        if (submitKind == nullptr)
        {
            throw makeInvalidArgument("VKSubmissionManager::executeTrackedSubmit requires a valid submit kind.");
        }
        if (waitSemaphores.size() != waitStageMasks.size())
        {
            throw makeInvalidArgument("VKSubmissionManager::executeTrackedSubmit requires wait semaphores and stage masks to have matching counts.");
        }

        const uint64_t submissionId = mNextSubmissionId++;
        vk::UniqueFence fence = mFencePool.acquire();
        const vk::Fence fenceHandle = fence.get();
        vk::SubmitInfo submitInfo = submitTemplate != nullptr ? *submitTemplate : vk::SubmitInfo{};
        submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        submitInfo.pCommandBuffers = commandBuffers.empty() ? nullptr : commandBuffers.data();
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStageMasks.empty() ? nullptr : waitStageMasks.data();
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.empty() ? nullptr : signalSemaphores.data();

        GVMCpuProbeScopeDetail(
            mDevice,
            SubmissionManagerLogCategory,
            "VKSubmissionManager::executeTrackedSubmit",
            "queue={} submission_id={} command_buffers={} encoders={} wait_semaphores={} signal_semaphores={} internal_present={} user_submit={} submit_kind={}",
            mQueueName.c_str(),
            submissionId,
            commandBuffers.size(),
            encoders.size(),
            waitSemaphores.size(),
            signalSemaphores.size(),
            internalPresentSubmit,
            countUserSubmit,
            submitKind);

        if (countUserSubmit)
        {
            ++mUserSubmitCount;
        }

        const auto submitStartTime = std::chrono::steady_clock::now();
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SubmissionManagerLogCategory,
                "VKSubmissionManager::executeTrackedSubmit.vkQueueSubmit",
                "queue={} submission_id={} command_buffers={} wait_semaphores={} signal_semaphores={} internal_present={} submit_kind={} has_submit_template={}",
                mQueueName.c_str(),
                submissionId,
                commandBuffers.size(),
                waitSemaphores.size(),
                signalSemaphores.size(),
                internalPresentSubmit,
                submitKind,
                submitTemplate != nullptr);
            queue.submit(submitInfo, fenceHandle);
        }

        ++mBackendSubmitCount;
        if (internalPresentSubmit)
        {
            ++mPresentInternalSubmitCount;
        }
        return trackSubmission(
            submissionId,
            fenceHandle,
            eastl::move(fence),
            encoders,
            diagnosticRetainedBufferCount,
            diagnosticRetainedTextureCount,
            submitStartTime,
            internalPresentSubmit);
    }

    size_t VKSubmissionManager::getInFlightCount() const
    {
        return mInFlightSubmissions.size();
    }

    uint64_t VKSubmissionManager::getUserSubmitCount() const
    {
        return mUserSubmitCount;
    }

    uint64_t VKSubmissionManager::getBackendSubmitCount() const
    {
        return mBackendSubmitCount;
    }

    uint64_t VKSubmissionManager::getPresentInternalSubmitCount() const
    {
        return mPresentInternalSubmitCount;
    }

    VKSubmissionCompletion VKSubmissionManager::getMostRecentSubmissionCompletion() const
    {
        return mMostRecentSubmissionCompletion ? mMostRecentSubmissionCompletion : mIdleSubmissionCompletion;
    }

    VKSubmissionCompletion VKSubmissionManager::getOldestSubmissionCompletion() const
    {
        return !mInFlightSubmissions.empty() ? mInFlightSubmissions.front().completion : mIdleSubmissionCompletion;
    }

    uint64_t VKSubmissionManager::getLastRetiredSubmissionId() const
    {
        return mLastRetiredSubmissionId.load(std::memory_order_acquire);
    }

    size_t VKSubmissionManager::pollCompletedSubmissions(const eastl::function<void(InFlightSubmission &submission)> &onRetired)
    {
        size_t retiredSubmissionCount = 0u;
        while (retiredSubmissionCount < mInFlightSubmissions.size())
        {
            InFlightSubmission &submission = mInFlightSubmissions[retiredSubmissionCount];
            const size_t remainingInFlightSubmissionCount = mInFlightSubmissions.size() - retiredSubmissionCount;
            const vk::Result fenceStatus = queryFenceStatus(
                *mDevice,
                submission.fence,
                "VKSubmissionManager::pollCompletedSubmissions.getFenceStatus",
                mQueueName,
                remainingInFlightSubmissionCount);
            if (fenceStatus == vk::Result::eNotReady)
            {
                const double ageMs = submissionAgeMilliseconds(submission);
                const bool shouldLogStall =
                    ageMs >= kFrontNotReadyLogThresholdMs &&
                    (submission.submissionId != mLastFrontNotReadyLoggedSubmissionId ||
                     ageMs >= mLastFrontNotReadyLoggedAgeMs + kFrontNotReadyLogIntervalMs);
                if (shouldLogStall)
                {
                    GVMLogTrace(
                        mDevice, SubmissionManagerLogCategory,
                    "event=submission_front_not_ready queue={} front_submission_id={} in_flight={} age_ms={:.3f} internal_present={} encoders={} retained_buffers={} retained_textures={}",
                    mQueueName.c_str(),
                    submission.submissionId,
                    remainingInFlightSubmissionCount,
                    ageMs,
                    submission.internalPresentSubmit,
                    submission.encoders.size(),
                    submission.diagnosticRetainedBufferCount,
                    submission.diagnosticRetainedTextureCount);
                    mLastFrontNotReadyLoggedSubmissionId = submission.submissionId;
                    mLastFrontNotReadyLoggedAgeMs = ageMs;
                }
                break;
            }
            if (submission.completion)
            {
                submission.completion->completed.store(true, std::memory_order_release);
            }
            if (onRetired)
            {
                onRetired(submission);
            }
            mLastRetiredSubmissionId.store(submission.submissionId, std::memory_order_release);
            if (submission.submissionId == mLastFrontNotReadyLoggedSubmissionId)
            {
                mLastFrontNotReadyLoggedSubmissionId = 0u;
                mLastFrontNotReadyLoggedAgeMs = 0.0;
            }
            mFencePool.recycle(eastl::move(submission.ownedFence));
            ++retiredSubmissionCount;
        }
        if (retiredSubmissionCount > 0u)
        {
            mInFlightSubmissions.erase(mInFlightSubmissions.begin(), mInFlightSubmissions.begin() + retiredSubmissionCount);
        }
        return retiredSubmissionCount;
    }

    void VKSubmissionManager::waitForSubmission(
        const VKSubmissionCompletion &completion,
        const eastl::function<void(InFlightSubmission &submission)> &onRetired)
    {
        const uint64_t targetSubmissionId = submissionDiagnosticId(completion);
        GVMCpuProbeScopeDetail(
            mDevice,
            SubmissionManagerLogCategory,
            "VKSubmissionManager::waitForSubmission",
            "queue={} target_submission_id={} in_flight_before={}",
            mQueueName.c_str(),
            targetSubmissionId,
            mInFlightSubmissions.size());
        if (!completion || completion->completed.load(std::memory_order_acquire))
        {
            return;
        }
        GVMLogDebug(
            mDevice, SubmissionManagerLogCategory,
            "event=submission_wait_begin queue={} target_submission_id={} in_flight_before={}",
            mQueueName.c_str(),
            targetSubmissionId,
            mInFlightSubmissions.size());
        (void)pollCompletedSubmissions(onRetired);
        if (completion->completed.load(std::memory_order_acquire))
        {
            GVMLogDebug(
                mDevice, SubmissionManagerLogCategory,
                "event=submission_wait_short_circuit queue={} target_submission_id={} in_flight_after_poll={}",
                mQueueName.c_str(),
                targetSubmissionId,
                mInFlightSubmissions.size());
            return;
        }
        while (!completion->completed.load(std::memory_order_acquire) && !mInFlightSubmissions.empty())
        {
            InFlightSubmission &submission = mInFlightSubmissions.front();
            GVMCpuProbeScopeDetail(
                mDevice,
                SubmissionManagerLogCategory,
                "VKSubmissionManager::waitForSubmission.waitForFences",
                "queue={} target_submission_id={} waited_submission_id={} in_flight={} waited_submission_age_ms={:.3f}",
                mQueueName.c_str(),
                targetSubmissionId,
                submission.submissionId,
                mInFlightSubmissions.size(),
                submissionAgeMilliseconds(submission));
            const vk::Result waitResult = mDevice->getNativeDevice().waitForFences(1u, &submission.fence, vk::True, kHostGpuWaitTimeoutNanoseconds);
            if (waitResult == vk::Result::eTimeout)
            {
                const double ageMs = submissionAgeMilliseconds(submission);
                GVMLogError(
                    mDevice,
                    SubmissionManagerLogCategory,
                    "event=submission_wait_timeout queue={} target_submission_id={} waited_submission_id={} in_flight={} timeout_ms={} waited_submission_age_ms={:.3f} internal_present={} encoders={} retained_buffers={} retained_textures={}",
                    mQueueName.c_str(),
                    targetSubmissionId,
                    submission.submissionId,
                    mInFlightSubmissions.size(),
                    kHostGpuWaitTimeoutMilliseconds,
                    ageMs,
                    submission.internalPresentSubmit,
                    submission.encoders.size(),
                    submission.diagnosticRetainedBufferCount,
                    submission.diagnosticRetainedTextureCount);
                flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
                throw makeRuntimeError(
                    eastl::string{"VKSubmissionManager::waitForSubmission timed out while waiting for queue='"} +
                    mQueueName +
                    "' targetSubmissionId=" +
                    eastl::to_string(targetSubmissionId) +
                    " waitedSubmissionId=" +
                    eastl::to_string(submission.submissionId) +
                    " timeoutMs=" +
                    eastl::to_string(kHostGpuWaitTimeoutMilliseconds));
            }
            if (waitResult != vk::Result::eSuccess)
            {
                throwQueueResultException(
                    waitResult,
                    "VKSubmissionManager::waitForSubmission.waitForFences",
                    mQueueName,
                    mInFlightSubmissions.size());
            }
            (void)pollCompletedSubmissions(onRetired);
        }
        GVMLogDebug(
            mDevice, SubmissionManagerLogCategory,
            "event=submission_wait_end queue={} target_submission_id={} in_flight_after={}",
            mQueueName.c_str(),
            targetSubmissionId,
            mInFlightSubmissions.size());
    }

    bool VKSubmissionManager::trySampleSubmissionFenceStatus(
        const VKSubmissionCompletion &completion,
        SubmissionFenceStatusSnapshot &outStatus) const
    {
        outStatus = {};
        if (!completion)
        {
            return false;
        }
        outStatus.submissionId = submissionDiagnosticId(completion);
        outStatus.completed = completion->completed.load(std::memory_order_acquire);
        if (outStatus.completed)
        {
            outStatus.signaled = true;
            return true;
        }
        for (const InFlightSubmission &submission : mInFlightSubmissions)
        {
            if (submission.completion != completion)
            {
                continue;
            }
            outStatus.tracked = true;
            outStatus.internalPresentSubmit = submission.internalPresentSubmit;
            outStatus.ageMs = submissionAgeMilliseconds(submission);
            const vk::Result fenceStatus = queryFenceStatus(
                *mDevice,
                submission.fence,
                "VKSubmissionManager::trySampleSubmissionFenceStatus.getFenceStatus",
                mQueueName,
                mInFlightSubmissions.size());
            if (fenceStatus == vk::Result::eSuccess)
            {
                outStatus.signaled = true;
            }
            return true;
        }
        return false;
    }

    VKSubmissionCompletion VKSubmissionManager::trackSubmission(
        uint64_t submissionId,
        vk::Fence fence,
        vk::UniqueFence ownedFence,
        const eastl::vector<CommandEncoder> &encoders,
        size_t diagnosticRetainedBufferCount,
        size_t diagnosticRetainedTextureCount,
        std::chrono::steady_clock::time_point submittedAt,
        bool internalPresentSubmit)
    {
        if (fence == vk::Fence{})
        {
            throw makeInvalidArgument("VKSubmissionManager::trackSubmission requires a valid fence.");
        }

        VKSubmissionCompletion completion = eastl::make_shared<VKSubmissionCompletionState>();
        completion->submissionId.store(submissionId, std::memory_order_release);
        mInFlightSubmissions.push_back({
            .submissionId = submissionId,
            .fence = fence,
            .ownedFence = eastl::move(ownedFence),
            .completion = completion,
            .encoders = encoders,
            .diagnosticRetainedBufferCount = diagnosticRetainedBufferCount,
            .diagnosticRetainedTextureCount = diagnosticRetainedTextureCount,
            .internalPresentSubmit = internalPresentSubmit,
            .submittedAt = submittedAt,
        });
        mMostRecentSubmissionCompletion = completion;
        return completion;
    }
} // namespace GVM::RHI::Vulkan
