#pragma once

#include "VKCommon.hpp"
#include <condition_variable>
#include <thread>

namespace GVM::RHI::Vulkan::Testing
{
    /// Gates real Vulkan command-buffer recording in serial tests; construct after device creation and destroy after all workers drain.
    class VKNativeRecordingProbe final
    {
    public:
        /// Observes the first expected recordings and optionally rejects the first observed native end call.
        explicit VKNativeRecordingProbe(uint32_t expected, bool rejectEnd = false);
        /// Restores the native dispatcher; every recording and submission must already have completed.
        ~VKNativeRecordingProbe();
        /// Keeps exclusive ownership of the temporary dispatcher interception.
        VKNativeRecordingProbe(const VKNativeRecordingProbe &) = delete;
        /// Prevents reassignment while native workers may reference this probe.
        VKNativeRecordingProbe &operator=(const VKNativeRecordingProbe &) = delete;
        /// Waits at most three seconds for all expected native command buffers to enter recording.
        bool waitForRecordings();
        /// Releases every gated native recording, including a serial implementation that cannot reach the expected overlap.
        void release();
        /// Returns the largest number of native command buffers simultaneously in recording state.
        uint32_t maximumActive() const;
        /// Returns the number of distinct host threads that entered the observed native recordings.
        uint32_t threadCount() const;
    private:
        /// Forwards the real begin call before gating only the bounded set of observed command buffers.
        static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *info);
        /// Ends or deliberately rejects one observed recording without affecting unrelated command buffers.
        static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer commandBuffer);
        static VKNativeRecordingProbe *sActive;
        PFN_vkBeginCommandBuffer mBegin;
        PFN_vkEndCommandBuffer mEnd;
        mutable std::mutex mMutex;
        std::condition_variable mChanged;
        eastl::vector<VkCommandBuffer> mCommands;
        eastl::vector<std::thread::id> mThreads;
        uint32_t mExpected;
        uint32_t mStarted = 0;
        uint32_t mActive = 0;
        uint32_t mMaximumActive = 0;
        bool mReleased = false;
        bool mRejectEnd;
    };
}
