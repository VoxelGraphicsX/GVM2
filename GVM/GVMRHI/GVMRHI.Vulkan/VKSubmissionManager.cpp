#include "VKSubmissionManager.hpp"

#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "Private/TimeUtils.hpp"

#include <EASTL/atomic.h>

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
    } // namespace

    void VKSubmissionManager::init(VKDevice &device, const eastl::string &queueName)
    {
        mDevice = &device;
        mQueueName = queueName;
        mInFlightSubmissions.clear();
        const vk::SemaphoreTypeCreateInfo type = vk::SemaphoreTypeCreateInfo{}.setSemaphoreType(vk::SemaphoreType::eTimeline);
        mTimeline = device.getNativeDevice().createSemaphoreUnique(vk::SemaphoreCreateInfo{}.setPNext(&type));
        mNextSubmissionId = 1u;
        mUserSubmitCount = 0u;
        mBackendSubmitCount = 0u;
        mPresentInternalSubmitCount = 0u;
        mMostRecentSubmissionSerial = 0u;
        mLastRetiredSubmissionId.store(0u, eastl::memory_order_release);
        mLastFrontNotReadyLoggedSubmissionId = 0u;
        mLastFrontNotReadyLoggedAgeMs = 0.0;
    }

    void VKSubmissionManager::destroy()
    {
        mInFlightSubmissions.clear();
        mTimeline.reset();
        mMostRecentSubmissionSerial = 0u;
        mQueueName.clear();
        mDevice = nullptr;
        mNextSubmissionId = 1u;
        mUserSubmitCount = 0u;
        mBackendSubmitCount = 0u;
        mPresentInternalSubmitCount = 0u;
        mLastRetiredSubmissionId.store(0u, eastl::memory_order_release);
        mLastFrontNotReadyLoggedSubmissionId = 0u;
        mLastFrontNotReadyLoggedAgeMs = 0.0;
    }

    VKSubmissionSerial VKSubmissionManager::executeTrackedSubmit(
        vk::Queue queue,
        const eastl::vector<vk::CommandBuffer> &commandBuffers,
        const eastl::vector<vk::SemaphoreSubmitInfo> &waitSemaphores,
        const eastl::vector<vk::SemaphoreSubmitInfo> &signalSemaphores,
        const eastl::vector<CommandEncoder> &encoders,
        bool internalPresentSubmit,
        bool countUserSubmit,
        const QueueDiagnostics::SubmissionStatistics &statistics,
        const char *submitKind)
    {
        if (mDevice == nullptr || queue == vk::Queue{})
        {
            throw std::invalid_argument("VKSubmissionManager::executeTrackedSubmit requires a valid device and queue.");
        }
        if (submitKind == nullptr)
        {
            throw std::invalid_argument("VKSubmissionManager::executeTrackedSubmit requires a valid submit kind.");
        }
        const uint64_t submissionId = mNextSubmissionId++;
        eastl::vector<vk::CommandBufferSubmitInfo> commands;
        commands.reserve(commandBuffers.size());
        for (vk::CommandBuffer commandBuffer : commandBuffers)
            commands.push_back(vk::CommandBufferSubmitInfo{}.setCommandBuffer(commandBuffer));
        eastl::vector<vk::SemaphoreSubmitInfo> signals = signalSemaphores;
        signals.push_back(vk::SemaphoreSubmitInfo{}.setSemaphore(mTimeline.get()).setValue(submissionId)
            .setStageMask(vk::PipelineStageFlagBits2::eAllCommands));
        vk::SubmitInfo2 submitInfo = {};
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commands.size());
        submitInfo.pCommandBufferInfos = commands.data();
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphoreInfos = waitSemaphores.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
        submitInfo.pSignalSemaphoreInfos = signals.data();

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

        const auto submitStartTime = std::chrono::steady_clock::now();
        mInFlightSubmissions.push_back({
            .submissionId = submissionId, .encoders = encoders,
            .statistics = statistics,
            .internalPresentSubmit = internalPresentSubmit, .submittedAt = submitStartTime,
        });
        try
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SubmissionManagerLogCategory,
                "VKSubmissionManager::executeTrackedSubmit.vkQueueSubmit2",
                "queue={} submission_id={} command_buffers={} wait_semaphores={} signal_semaphores={} internal_present={} submit_kind={}",
                mQueueName.c_str(),
                submissionId,
                commandBuffers.size(),
                waitSemaphores.size(),
                signalSemaphores.size(),
                internalPresentSubmit,
                submitKind);
            queue.submit2(submitInfo);
        }

        catch (...)
        {
            mInFlightSubmissions.pop_back();
            throw;
        }
        mMostRecentSubmissionSerial = submissionId;
        if (countUserSubmit) ++mUserSubmitCount;
        ++mBackendSubmitCount;
        if (internalPresentSubmit)
        {
            ++mPresentInternalSubmitCount;
        }
        return submissionId;
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

    VKSubmissionSerial VKSubmissionManager::getMostRecentSubmissionSerial() const
    {
        return mMostRecentSubmissionSerial;
    }

    VKSubmissionSerial VKSubmissionManager::getOldestSubmissionSerial() const
    {
        return !mInFlightSubmissions.empty() ? mInFlightSubmissions.front().submissionId : 0u;
    }

    uint64_t VKSubmissionManager::getLastRetiredSubmissionId() const
    {
        return mLastRetiredSubmissionId.load(eastl::memory_order_acquire);
    }

    bool VKSubmissionManager::isRetired(VKSubmissionSerial serial) const
    {
        return serial <= mLastRetiredSubmissionId.load(eastl::memory_order_acquire);
    }

    size_t VKSubmissionManager::pollCompletedSubmissions(const eastl::function<void(InFlightSubmission &submission)> &onRetired)
    {
        if (mInFlightSubmissions.empty()) return 0u;
        uint64_t completedValue = 0u;
        try
        {
            completedValue = mDevice->getNativeDevice().getSemaphoreCounterValue(mTimeline.get());
        }
        catch (const vk::DeviceLostError &error)
        {
            mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
        size_t retiredSubmissionCount = 0u;
        while (retiredSubmissionCount < mInFlightSubmissions.size())
        {
            InFlightSubmission &submission = mInFlightSubmissions[retiredSubmissionCount];
            const size_t remainingInFlightSubmissionCount = mInFlightSubmissions.size() - retiredSubmissionCount;
            if (submission.submissionId > completedValue)
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
                    submission.statistics.retainedBufferCount,
                    submission.statistics.retainedTextureCount);
                    mLastFrontNotReadyLoggedSubmissionId = submission.submissionId;
                    mLastFrontNotReadyLoggedAgeMs = ageMs;
                }
                break;
            }
            if (onRetired)
            {
                onRetired(submission);
            }
            mLastRetiredSubmissionId.store(submission.submissionId, eastl::memory_order_release);
            if (submission.submissionId == mLastFrontNotReadyLoggedSubmissionId)
            {
                mLastFrontNotReadyLoggedSubmissionId = 0u;
                mLastFrontNotReadyLoggedAgeMs = 0.0;
            }
            ++retiredSubmissionCount;
        }
        if (retiredSubmissionCount > 0u)
        {
            mInFlightSubmissions.erase(mInFlightSubmissions.begin(), mInFlightSubmissions.begin() + retiredSubmissionCount);
        }
        return retiredSubmissionCount;
    }

    void VKSubmissionManager::waitForSubmission(
        VKSubmissionSerial serial,
        const eastl::function<void(InFlightSubmission &submission)> &onRetired)
    {
        try
        {
            waitForSubmissionImpl(serial, onRetired);
        }
        catch (const vk::DeviceLostError &error)
        {
            mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
    }

    void VKSubmissionManager::waitForSubmissionImpl(
        VKSubmissionSerial serial,
        const eastl::function<void(InFlightSubmission &submission)> &onRetired)
    {
        const uint64_t targetSubmissionId = serial;
        GVMCpuProbeScopeDetail(
            mDevice,
            SubmissionManagerLogCategory,
            "VKSubmissionManager::waitForSubmission",
            "queue={} target_submission_id={} in_flight_before={}",
            mQueueName.c_str(),
            targetSubmissionId,
            mInFlightSubmissions.size());
        if (isRetired(serial))
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
        if (isRetired(serial))
        {
            GVMLogDebug(
                mDevice, SubmissionManagerLogCategory,
                "event=submission_wait_short_circuit queue={} target_submission_id={} in_flight_after_poll={}",
                mQueueName.c_str(),
                targetSubmissionId,
                mInFlightSubmissions.size());
            return;
        }
        while (!isRetired(serial) && !mInFlightSubmissions.empty())
        {
            InFlightSubmission &submission = mInFlightSubmissions.front();
            GVMCpuProbeScopeDetail(
                mDevice,
                SubmissionManagerLogCategory,
                "VKSubmissionManager::waitForSubmission.waitSemaphores",
                "queue={} target_submission_id={} waited_submission_id={} in_flight={} waited_submission_age_ms={:.3f}",
                mQueueName.c_str(),
                targetSubmissionId,
                submission.submissionId,
                mInFlightSubmissions.size(),
                submissionAgeMilliseconds(submission));
            const vk::Semaphore semaphore = mTimeline.get();
            const vk::SemaphoreWaitInfo waitInfo = vk::SemaphoreWaitInfo{}.setSemaphores(semaphore).setValues(targetSubmissionId);
            const vk::Result waitResult = mDevice->getNativeDevice().waitSemaphores(waitInfo, kHostGpuWaitTimeoutNanoseconds);
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
                    submission.statistics.retainedBufferCount,
                    submission.statistics.retainedTextureCount);
                flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
                throw std::runtime_error((
                    eastl::string{"VKSubmissionManager::waitForSubmission timed out while waiting for queue='"} +
                    mQueueName +
                    "' targetSubmissionId=" +
                    eastl::to_string(targetSubmissionId) +
                    " waitedSubmissionId=" +
                    eastl::to_string(submission.submissionId) +
                    " timeoutMs=" +
                    eastl::to_string(kHostGpuWaitTimeoutMilliseconds)).c_str());
            }
            if (waitResult != vk::Result::eSuccess)
            {
                throwQueueResultException(
                    waitResult,
                    "VKSubmissionManager::waitForSubmission.waitSemaphores",
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

    bool VKSubmissionManager::trySampleSubmissionStatus(
        VKSubmissionSerial serial,
        SubmissionStatusSnapshot &outStatus) const
    {
        outStatus = {};
        outStatus.submissionId = serial;
        outStatus.completed = isRetired(serial);
        if (outStatus.completed)
        {
            outStatus.signaled = true;
            return true;
        }
        for (const InFlightSubmission &submission : mInFlightSubmissions)
        {
            if (submission.submissionId != serial)
            {
                continue;
            }
            outStatus.tracked = true;
            outStatus.internalPresentSubmit = submission.internalPresentSubmit;
            outStatus.ageMs = submissionAgeMilliseconds(submission);
            try
            {
                outStatus.signaled = mDevice->getNativeDevice().getSemaphoreCounterValue(mTimeline.get()) >= submission.submissionId;
            }
            catch (const vk::DeviceLostError &error)
            {
                mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
                throw DeviceLostError(error.what());
            }
            return true;
        }
        return false;
    }

} // namespace GVM::RHI::Vulkan
