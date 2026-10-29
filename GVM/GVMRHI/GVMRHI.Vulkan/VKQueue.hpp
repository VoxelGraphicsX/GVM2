#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKPendingReadbackTracker.hpp"
#include "VKQueueCommandContextPool.hpp"
#include "VKSubmissionManager.hpp"
#include "VKResourceStateDB.hpp"
#include "VKUploadAllocator.hpp"

#include <GVMRHI/Private/BufferCopyMultipleRegionExecutor.hpp>
#include "Private/GVMRHIDefines.hpp"

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKQueue final : public QueueImpl
    {
    public:
        VKQueue() = default;

        void init(VKDevice *device, uint32_t queueFamilyIndex, vk::Queue queue, const eastl::string &queueName);

        CommandEncoder createCommandEncoder() override;
        /// Publishes an unmapped host write without racing queue state compilation.
        void recordHostWrite(VKBuffer &buffer);
        void writeBuffer(BufferRange buffer, void const *data, uint64_t size) override;
        void readBuffer(BufferRange buffer, void *data, uint64_t size) override;
        /// Queues a buffer readback executed by the next submission; the Readback becomes ready once that submission completes.
        Readback readBufferAsync(BufferRange source) override;
        /// Queues a texture readback executed by the next submission; the Readback becomes ready once that submission completes.
        Readback readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
        void writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize) override;
        void readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
        void uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
        void uploadTexture(Texture destination, BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
        void copyBufferToBuffer(BufferRange source, BufferRange destination) override;
        void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) override;
        void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) override;
        void copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount) override;
        void fillBuffer(BufferRange source, uint8_t value) override;
        void submit(const eastl::vector<CommandEncoder> &encoders) override;
        void destroy() override;
        void waitForSubmission(VKSubmissionSerial serial, const char *waitSource = "unspecified");
        /**
         * Waits for the oldest pending submission and runs its completed-submission cleanup;
         * returns false when nothing was pending after the initial poll.
         */
        bool waitForOldestPendingSubmission();
        VKSubmissionSerial submitTracked(const vk::SubmitInfo2 &submitInfo, bool internalPresentSubmit = false);
        using SubmissionStatusSnapshot = VKSubmissionManager::SubmissionStatusSnapshot;

        [[nodiscard]]
        bool trySampleSubmissionStatus(VKSubmissionSerial serial, SubmissionStatusSnapshot &outStatus) const;
        /** Returns whether the submission with the given serial has been retired; serial 0 is always retired. */
        [[nodiscard]]
        bool isRetired(VKSubmissionSerial serial) const;
        void recycleCommandBufferContext(eastl::shared_ptr<VKCommandBufferContext> context);

        [[nodiscard]] vk::Queue getNativeQueue() const;
        [[nodiscard]] uint32_t getQueueFamilyIndex() const;
        [[nodiscard]] uint64_t getUserSubmitCount() const;
        [[nodiscard]] uint64_t getBackendSubmitCount() const;
        [[nodiscard]] uint64_t getPresentInternalSubmitCount() const;
        [[nodiscard]] VKSubmissionSerial getMostRecentSubmissionSerial() const;
        [[nodiscard]] uint64_t getLastRetiredSubmissionId() const;
        [[nodiscard]] VKDevice *getDevice() const;
        [[nodiscard]] const eastl::string &getQueueName() const;
        [[nodiscard]] VKResourceStateDB &getResourceStateDB();
        [[nodiscard]] const VKResourceStateDB &getResourceStateDB() const;
        Logger getLogger() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

    private:
        using InFlightSubmission = VKSubmissionManager::InFlightSubmission;
        void ensureAlive(const char *apiName) const;
        /** Throws once the queue was marked lost by a failed native submission; waits and polling stay usable. */
        void ensureNotLost() const;
        CommandEncoder createCommandEncoderLocked();
        void pollCompletedSubmissions();
        void enforceTrackedSubmissionLimitLocked();
        void handleRetiredSubmissionLocked(InFlightSubmission &submission);
        eastl::shared_ptr<VKCommandBufferContext> acquireCommandBufferContextLocked();
        void recycleCommandBufferContextLocked(eastl::shared_ptr<VKCommandBufferContext> context);
        [[nodiscard]] CommandEncoder getOrCreateInternalCopyCommandEncoderLocked();
        [[nodiscard]] VKBlitPassEncoder &getOrCreateInternalBlitCopyPassLocked();
        [[nodiscard]] ComputePassEncoder getOrCreateInternalComputeCopyPassEncoderLocked();
        void endInternalCopyPassLocked();
        [[nodiscard]] CommandEncoder getOrCreateInternalReadbackCommandEncoderLocked();
        [[nodiscard]] VKBlitPassEncoder &getOrCreateInternalReadbackBlitPassLocked();
        void endInternalReadbackPassLocked();
        void appendInternalPendingCommandsLocked(
            eastl::vector<vk::CommandBuffer> &commandBuffers,
            eastl::vector<CommandEncoder> &encoders,
            bool &containsDeferredCopies);
        void appendInternalReadbackPendingCommandsLocked(
            eastl::vector<vk::CommandBuffer> &commandBuffers,
            eastl::vector<CommandEncoder> &encoders,
            bool &containsDeferredCopies);
        void destroyInternalPendingCommandsLocked();
        [[nodiscard]] bool hasPendingInternalCommands() const;
        [[nodiscard]]
        bool assignPendingReadbackCompletionLocked(
            VKSubmissionSerial submissionSerial,
            VKSubmissionSerial &outAssignedSerial);
        void processCompletedSubmissionCleanupLocked();
        void waitTrackedSubmissionLocked(VKSubmissionSerial serial);
        [[nodiscard]]
        VKSubmissionSerial submitRecordedCommandBuffersLocked(
            const eastl::vector<vk::CommandBuffer> &commandBuffers,
            const eastl::vector<CommandEncoder> &encoders,
            bool internalPresentSubmit,
            bool noteUserSubmit,
            const vk::SubmitInfo2 *submitTemplate,
            const char *submitKind);

        VKDevice *mDevice = nullptr;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        eastl::string mQueueName;
        vk::Queue mQueue;
        uint32_t mQueueFamilyIndex = 0;
        VKQueueCommandContextPool mCommandContextPool;
        CommandEncoder mInternalCopyCommandEncoder;
        BlitPassEncoder mInternalBlitCopyPassEncoder;
        ComputePassEncoder mInternalComputeCopyPassEncoder;
        CommandEncoder mInternalReadbackCommandEncoder;
        BlitPassEncoder mInternalReadbackBlitPassEncoder;
        Private::BufferCopyMultipleRegionExecutor mBufferCopyMultipleRegionExecutor;
        mutable Mutex mSubmitMutex;
        VKSubmissionManager mSubmissionManager;
        VKResourceStateDB mStateDB;
        VKUploadAllocator mUploadAllocator;
        VKPendingReadbackTracker mPendingReadbackTracker;
        uint64_t mReadbackSequence = 0;
        bool mDestroyed = false;
        /** True after an exception escaped native lowering or vkQueueSubmit2; new GPU work is rejected. */
        /** Reason captured from the exception that lost the queue. */
    };
} // namespace GVM::RHI::Vulkan
