#include "VKSubmissionTimeline.hpp"
#include "VKDevice.hpp"

namespace GVM::RHI::Vulkan
{
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
            if (timeoutNanoseconds == 0)
            {
                return mDevice.getSemaphoreCounterValue(mSemaphore.get()) >= value;
            }
            const auto semaphore = mSemaphore.get();
            const auto waitInfo = vk::SemaphoreWaitInfo{}.setSemaphores(semaphore).setValues(value);
            return mDevice.waitSemaphores(waitInfo, timeoutNanoseconds) == vk::Result::eSuccess;
        }
        catch (const vk::DeviceLostError &error)
        {
            lock.unlock();
            mLostState->markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
    }

    void VKSubmissionTimeline::close()
    {
        eastl::string failure;
        {
            std::lock_guard lock(mMutex);
            if (!mSemaphore)
            {
                return;
            }
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
        if (!failure.empty())
        {
            mLostState->markLost(DeviceLostReason::GpuFault, failure);
        }
    }
}
