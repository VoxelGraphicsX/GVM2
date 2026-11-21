#include "VKSubmissionTimeline.hpp"
#include "VKDevice.hpp"
#include <EASTL/fixed_vector.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    VKHostTimelineState::VKHostTimelineState(vk::Device device, eastl::shared_ptr<DeviceLostState> lostState)
        : mDevice(device), mLostState(eastl::move(lostState))
    {
        const auto typeInfo = vk::SemaphoreTypeCreateInfo{}
            .setSemaphoreType(vk::SemaphoreType::eTimeline)
            .setInitialValue(0);
        mSemaphore = mDevice.createSemaphoreUnique(vk::SemaphoreCreateInfo{}.setPNext(&typeInfo));
    }

    VKHostTimelineState::~VKHostTimelineState()
    {
        close();
    }

    namespace
    {
        constexpr uint64_t kMaxSemaphoreStep = 0x3FFFFFFFull;
    }

    void VKHostTimelineState::noteWaitTarget(uint64_t targetValue) const noexcept
    {
        uint64_t observed = mMaxWaitTarget.load(std::memory_order_seq_cst);
        while (observed < targetValue &&
               !mMaxWaitTarget.compare_exchange_weak(
                   observed, targetValue, std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
        }
    }

    uint64_t VKHostTimelineState::signal() noexcept
    {
        std::shared_lock lifetimeLock(mLifetimeMutex);
        std::lock_guard signalLock(mSignalMutex);
        if (mClosed.load(std::memory_order_seq_cst))
        {
            return mCurrentValue.load(std::memory_order_acquire);
        }
        const uint64_t next = mCurrentValue.fetch_add(1u, std::memory_order_acq_rel) + 1u;
        if (!mDevice || !mSemaphore)
        {
            return next;
        }
        try
        {
            const auto signalInfo = vk::SemaphoreSignalInfo{}
                .setSemaphore(mSemaphore.get())
                .setValue(next);
            mDevice.signalSemaphore(signalInfo);
        }
        catch (const vk::DeviceLostError &error)
        {
            if (mLostState)
            {
                mLostState->markLost(DeviceLostReason::GpuFault, error.what());
            }
        }
        catch (...)
        {
        }
        return next;
    }

    uint64_t VKHostTimelineState::currentValue() const noexcept
    {
        return mCurrentValue.load(std::memory_order_acquire);
    }

    bool VKHostTimelineState::wait(uint64_t targetValue, uint64_t timeoutNanoseconds) const
    {
        if (currentValue() >= targetValue || mClosed.load(std::memory_order_acquire))
        {
            return true;
        }
        if (timeoutNanoseconds == 0u)
        {
            return false;
        }
        const bool infinite = timeoutNanoseconds == std::numeric_limits<uint64_t>::max();
        const auto bounded = eastl::min(timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        const auto deadline = infinite
            ? std::chrono::steady_clock::time_point::max()
            : (std::chrono::steady_clock::now() + std::chrono::nanoseconds(bounded));

        std::shared_lock lifetimeLock(mLifetimeMutex);
        for (;;)
        {
            const uint64_t current = currentValue();
            if (!mSemaphore || mClosed.load(std::memory_order_acquire) || current >= targetValue)
            {
                return true;
            }
            if (mLostState)
            {
                mLostState->throwIfLost("HostTimeline::wait");
            }
            uint64_t sliceNs = std::numeric_limits<uint64_t>::max();
            if (!infinite)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                {
                    return false;
                }
                sliceNs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count());
            }
            const uint64_t stepTarget =
                (targetValue - current > kMaxSemaphoreStep) ? (current + kMaxSemaphoreStep) : targetValue;
            noteWaitTarget(stepTarget);
            if (mClosed.load(std::memory_order_seq_cst) || currentValue() >= targetValue)
            {
                return true;
            }
            try
            {
                const vk::Semaphore semaphore = mSemaphore.get();
                const auto waitInfo = vk::SemaphoreWaitInfo{}.setSemaphores(semaphore).setValues(stepTarget);
                const auto waitResult = mDevice.waitSemaphores(waitInfo, sliceNs);
                if (mClosed.load(std::memory_order_acquire) || currentValue() >= targetValue)
                {
                    return true;
                }
                if (waitResult != vk::Result::eSuccess)
                {
                    return false;
                }
            }
            catch (const vk::DeviceLostError &error)
            {
                lifetimeLock.unlock();
                if (mLostState)
                {
                    mLostState->markLost(DeviceLostReason::GpuFault, error.what());
                }
                throw DeviceLostError(error.what());
            }
        }
    }

    void VKHostTimelineState::close() noexcept
    {
        if (mClosed.exchange(true, std::memory_order_seq_cst))
        {
            return;
        }
        {
            std::shared_lock lifetimeLock(mLifetimeMutex);
            std::lock_guard signalLock(mSignalMutex);
            const uint64_t current = mCurrentValue.load(std::memory_order_seq_cst);
            const uint64_t maxWait = mMaxWaitTarget.load(std::memory_order_seq_cst);
            const uint64_t wakeTarget = eastl::max(current + 1u, maxWait);
            mCurrentValue.store(wakeTarget, std::memory_order_release);
            if (mDevice && mSemaphore)
            {
                try
                {
                    uint64_t signaled = mDevice.getSemaphoreCounterValue(mSemaphore.get());
                    while (signaled < wakeTarget)
                    {
                        const uint64_t step = eastl::min(wakeTarget - signaled, kMaxSemaphoreStep);
                        signaled += step;
                        const auto signalInfo = vk::SemaphoreSignalInfo{}
                            .setSemaphore(mSemaphore.get())
                            .setValue(signaled);
                        mDevice.signalSemaphore(signalInfo);
                    }
                }
                catch (const vk::DeviceLostError &error)
                {
                    if (mLostState)
                    {
                        mLostState->markLost(DeviceLostReason::GpuFault, error.what());
                    }
                }
                catch (...)
                {
                }
            }
        }
        std::unique_lock lifetimeLock(mLifetimeMutex);
        mSemaphore.reset();
        mDevice = nullptr;
    }

    VKSubmissionTimeline::VKSubmissionTimeline(VKDevice &device)
        : mDevice(device.getNativeDevice()), mLostState(device.getLostStateHandle())
    {
        const auto type = vk::SemaphoreTypeCreateInfo{}.setSemaphoreType(vk::SemaphoreType::eTimeline);
        mSemaphore = mDevice.createSemaphoreUnique(vk::SemaphoreCreateInfo{}.setPNext(&type));
    }

    VKSubmissionTimeline::~VKSubmissionTimeline() = default;

    vk::Semaphore VKSubmissionTimeline::nativeSemaphore() const
    {
        return mSemaphore.get();
    }

    const eastl::shared_ptr<DeviceLostState> &VKSubmissionTimeline::deviceState() const
    {
        return mLostState;
    }

    bool VKSubmissionTimeline::wait(uint64_t value, uint64_t timeoutNanoseconds) const
    {
        std::shared_lock lock(mMutex);
        mLostState->throwIfLost("Submission::wait");
        if (!mSemaphore)
        {
            if (value > mCompletedAtClose)
            {
                throw DeviceLostError("Queue closed before its accepted submission completed.");
            }
            return true;
        }
        try
        {
            if (timeoutNanoseconds == 0u)
            {
                return mDevice.getSemaphoreCounterValue(mSemaphore.get()) >= value;
            }
            const auto semaphore = mSemaphore.get();
            const auto waitInfo = vk::SemaphoreWaitInfo{}.setSemaphores(semaphore).setValues(value);
            if (mDevice.waitSemaphores(waitInfo, timeoutNanoseconds) == vk::Result::eSuccess)
            {
                mLostState->throwIfLost("Submission::wait");
                return true;
            }
            mLostState->throwIfLost("Submission::wait");
            return false;
        }
        catch (const vk::DeviceLostError &error)
        {
            lock.unlock();
            mLostState->markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
    }

    bool VKSubmissionTimeline::waitAny(
        eastl::span<const WaitTarget> sortedTargets,
        const VKHostTimelineState *hostTimelineState,
        uint64_t hostTargetValue,
        uint64_t timeoutNanoseconds)
    {
        if (sortedTargets.empty())
        {
            return hostTimelineState != nullptr ? hostTimelineState->wait(hostTargetValue, timeoutNanoseconds) : true;
        }

        eastl::fixed_vector<std::shared_lock<std::shared_mutex>, 4, true> timelineLocks;
        timelineLocks.reserve(sortedTargets.size());
        for (const auto &target : sortedTargets)
        {
            timelineLocks.emplace_back(target.timeline->mMutex);
        }

        std::shared_lock<std::shared_mutex> hostTimelineLock;
        if (hostTimelineState != nullptr && hostTargetValue > 0u)
        {
            if (hostTimelineState->currentValue() >= hostTargetValue ||
                hostTimelineState->mClosed.load(std::memory_order_acquire))
            {
                return true;
            }
            hostTimelineLock = std::shared_lock<std::shared_mutex>(hostTimelineState->mLifetimeMutex);
            if (!hostTimelineState->mSemaphore || hostTimelineState->mClosed.load(std::memory_order_acquire) ||
                hostTimelineState->currentValue() >= hostTargetValue)
            {
                return true;
            }
        }

        const auto &lostState = sortedTargets.front().timeline->mLostState;
        if (hostTimelineState != nullptr && hostTargetValue > 0u && hostTimelineState->mLostState != lostState)
        {
            throw std::invalid_argument("RHI::waitAny HostTimeline must belong to the same device.");
        }
        lostState->throwIfLost("RHI::waitAny");

        vk::Device nativeDevice = nullptr;
        eastl::fixed_vector<vk::Semaphore, 5, true> semaphores;
        eastl::fixed_vector<uint64_t, 5, true> values;
        for (const auto &target : sortedTargets)
        {
            if (target.timeline->mLostState != lostState)
            {
                throw std::invalid_argument("RHI::waitAny submissions must belong to the same device.");
            }
            if (!target.timeline->mSemaphore)
            {
                if (target.value > target.timeline->mCompletedAtClose)
                {
                    throw DeviceLostError("Queue closed before its accepted submission completed.");
                }
                return true;
            }
            nativeDevice = target.timeline->mDevice;
            semaphores.push_back(target.timeline->mSemaphore.get());
            values.push_back(target.value);
        }

        try
        {
            if (timeoutNanoseconds == 0u)
            {
                for (size_t i = 0; i < semaphores.size(); ++i)
                {
                    if (nativeDevice.getSemaphoreCounterValue(semaphores[i]) >= values[i])
                    {
                        return true;
                    }
                }
                return false;
            }

            const bool infinite = timeoutNanoseconds == std::numeric_limits<uint64_t>::max();
            const auto bounded = eastl::min(timeoutNanoseconds, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
            const auto deadline = infinite
                ? std::chrono::steady_clock::time_point::max()
                : (std::chrono::steady_clock::now() + std::chrono::nanoseconds(bounded));
            const size_t gpuCount = semaphores.size();

            for (;;)
            {
                semaphores.resize(gpuCount);
                values.resize(gpuCount);
                if (hostTimelineState != nullptr && hostTargetValue > 0u)
                {
                    const uint64_t currentHost = hostTimelineState->currentValue();
                    if (hostTimelineState->mClosed.load(std::memory_order_acquire) || currentHost >= hostTargetValue)
                    {
                        return true;
                    }
                    if (hostTimelineState->mSemaphore)
                    {
                        const uint64_t stepTarget =
                            (hostTargetValue - currentHost > kMaxSemaphoreStep)
                                ? (currentHost + kMaxSemaphoreStep)
                                : hostTargetValue;
                        hostTimelineState->noteWaitTarget(stepTarget);
                        if (hostTimelineState->mClosed.load(std::memory_order_seq_cst) ||
                            hostTimelineState->currentValue() >= hostTargetValue)
                        {
                            return true;
                        }
                        semaphores.push_back(hostTimelineState->mSemaphore.get());
                        values.push_back(stepTarget);
                    }
                }

                uint64_t sliceNs = std::numeric_limits<uint64_t>::max();
                if (!infinite)
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= deadline)
                    {
                        return false;
                    }
                    sliceNs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count());
                }

                const auto waitInfo = vk::SemaphoreWaitInfo{}
                    .setFlags(vk::SemaphoreWaitFlagBits::eAny)
                    .setSemaphores(semaphores)
                    .setValues(values);
                if (nativeDevice.waitSemaphores(waitInfo, sliceNs) == vk::Result::eSuccess)
                {
                    lostState->throwIfLost("RHI::waitAny");
                    if (semaphores.size() == gpuCount ||
                        hostTimelineState->mClosed.load(std::memory_order_acquire) ||
                        hostTimelineState->currentValue() >= hostTargetValue)
                    {
                        return true;
                    }
                    for (size_t i = 0; i < gpuCount; ++i)
                    {
                        if (nativeDevice.getSemaphoreCounterValue(semaphores[i]) >= values[i])
                        {
                            return true;
                        }
                    }
                    continue;
                }
                lostState->throwIfLost("RHI::waitAny");
                return false;
            }
        }
        catch (const vk::DeviceLostError &error)
        {
            hostTimelineLock = std::shared_lock<std::shared_mutex>{};
            timelineLocks.clear();
            lostState->markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
    }

    void VKSubmissionTimeline::close()
    {
        eastl::string failure;
        {
            std::lock_guard lock(mMutex);
            if (mSemaphore)
            {
                if (!mLostState->isLost())
                {
                    try
                    {
                        mCompletedAtClose = mDevice.getSemaphoreCounterValue(mSemaphore.get());
                    }
                    catch (const vk::DeviceLostError &error)
                    {
                        failure = error.what();
                    }
                }
                mSemaphore.reset();
                mDevice = nullptr;
            }
        }
        if (!failure.empty())
        {
            mLostState->markLost(DeviceLostReason::GpuFault, failure);
        }
    }
}
