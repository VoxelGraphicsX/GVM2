#include "VKNativeRecordingProbe.hpp"
#include <EASTL/algorithm.h>
#include <chrono>

namespace GVM::RHI::Vulkan::Testing
{
    VKNativeRecordingProbe *VKNativeRecordingProbe::sActive = nullptr;

    VKNativeRecordingProbe::VKNativeRecordingProbe(uint32_t expected, bool rejectEnd)
        : mBegin(VULKAN_HPP_DEFAULT_DISPATCHER.vkBeginCommandBuffer),
          mEnd(VULKAN_HPP_DEFAULT_DISPATCHER.vkEndCommandBuffer), mExpected(expected), mRejectEnd(rejectEnd)
    {
        if (sActive != nullptr || expected == 0 || mBegin == nullptr || mEnd == nullptr)
        {
            throw std::logic_error("Native recording probes require an initialized device and exclusive test ownership.");
        }
        mCommands.reserve(expected);
        mThreads.reserve(expected);
        sActive = this;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkBeginCommandBuffer = &begin;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkEndCommandBuffer = &end;
    }

    VKNativeRecordingProbe::~VKNativeRecordingProbe()
    {
        VULKAN_HPP_DEFAULT_DISPATCHER.vkBeginCommandBuffer = mBegin;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkEndCommandBuffer = mEnd;
        sActive = nullptr;
    }

    bool VKNativeRecordingProbe::waitForRecordings()
    {
        std::unique_lock lock(mMutex);
        return mChanged.wait_for(lock, std::chrono::seconds(3), [this] { return mStarted == mExpected; });
    }

    void VKNativeRecordingProbe::release()
    {
        std::lock_guard lock(mMutex);
        mReleased = true;
        mChanged.notify_all();
    }

    uint32_t VKNativeRecordingProbe::maximumActive() const
    {
        std::lock_guard lock(mMutex);
        return mMaximumActive;
    }

    uint32_t VKNativeRecordingProbe::threadCount() const
    {
        std::lock_guard lock(mMutex);
        return static_cast<uint32_t>(mThreads.size());
    }

    VKAPI_ATTR VkResult VKAPI_CALL VKNativeRecordingProbe::begin(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *info)
    {
        auto &probe = *sActive;
        const auto result = probe.mBegin(commandBuffer, info);
        if (result != VK_SUCCESS)
        {
            return result;
        }
        std::unique_lock lock(probe.mMutex);
        if (probe.mStarted == probe.mExpected)
        {
            return result;
        }
        probe.mCommands.push_back(commandBuffer);
        ++probe.mStarted;
        const auto thread = std::this_thread::get_id();
        if (eastl::find(probe.mThreads.begin(), probe.mThreads.end(), thread) == probe.mThreads.end())
        {
            probe.mThreads.push_back(thread);
        }
        probe.mMaximumActive = eastl::max(probe.mMaximumActive, ++probe.mActive);
        probe.mChanged.notify_all();
        probe.mChanged.wait_for(lock, std::chrono::seconds(5), [&probe] { return probe.mReleased; });
        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL VKNativeRecordingProbe::end(VkCommandBuffer commandBuffer)
    {
        auto &probe = *sActive;
        std::unique_lock lock(probe.mMutex);
        const auto position = eastl::find(probe.mCommands.begin(), probe.mCommands.end(), commandBuffer);
        const bool observed = position != probe.mCommands.end();
        const bool reject = observed && probe.mRejectEnd;
        if (observed)
        {
            --probe.mActive;
            probe.mCommands.erase(position);
            probe.mRejectEnd = false;
        }
        lock.unlock();
        return reject ? VK_ERROR_OUT_OF_HOST_MEMORY : probe.mEnd(commandBuffer);
    }
}
