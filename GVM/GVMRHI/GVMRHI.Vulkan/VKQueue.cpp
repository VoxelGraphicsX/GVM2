#include "VKQueue.hpp"

#include "Private/TimeUtils.hpp"
#include "VKBlitPassEncoder.hpp"
#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKPresentManager.hpp"
#include "VKQueue.Diagnostics.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/make_intrusive.h>
#include <EASTL/utility.h>

#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/Private/RHIBufferContracts.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view QueueLogCategory = "gvmrhi.vulkan.queue";
    } // namespace

    vk::Queue VKQueue::getNativeQueue() const
    {
        return mQueue;
    }

    uint32_t VKQueue::getQueueFamilyIndex() const
    {
        return mQueueFamilyIndex;
    }

    uint64_t VKQueue::getUserSubmitCount() const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.getUserSubmitCount();
    }

    uint64_t VKQueue::getBackendSubmitCount() const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.getBackendSubmitCount();
    }

    uint64_t VKQueue::getPresentInternalSubmitCount() const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.getPresentInternalSubmitCount();
    }

    VKSubmissionCompletion VKQueue::getMostRecentSubmissionCompletion() const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.getMostRecentSubmissionCompletion();
    }

    uint64_t VKQueue::getLastRetiredSubmissionId() const
    {
        return mSubmissionManager.getLastRetiredSubmissionId();
    }

    VKDevice *VKQueue::getDevice() const
    {
        return mDevice;
    }

    const eastl::string &VKQueue::getQueueName() const
    {
        return mQueueName;
    }

    VKResourceStateDB &VKQueue::getResourceStateDB()
    {
        return mStateDB;
    }

    const VKResourceStateDB &VKQueue::getResourceStateDB() const
    {
        return mStateDB;
    }


    namespace
    {
        // This is an emergency high-water mark for tracked fences, not the
        // CPU/GPU inflight-frame count. Queue-internal commands can legitimately
        // expand one frame into multiple same-queue submissions.
        constexpr size_t kMaxTrackedSubmissions = 64u;

        uint64_t resolveBufferTransferSize(const BufferRange &range, uint64_t requestedSize, const char *apiName)
        {
            const uint64_t resolvedSize = requestedSize == WholeSize ? range.size : requestedSize;
            if (resolvedSize > range.size)
            {
                throw makeOutOfRange(eastl::string(apiName) + " requested more bytes than the supplied BufferRange.");
            }
            return resolvedSize;
        }

        [[nodiscard]]
        double durationMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return Internal::elapsedMilliseconds(startTime);
        }

        [[nodiscard]]
        Buffer createQueueReadbackBuffer(VKDevice &device, const eastl::string &queueName, const char *labelSuffix, uint64_t readbackId, uint64_t size)
        {
            return device.createBuffer({
                .label = queueName + labelSuffix + eastl::to_string(readbackId),
                .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
                .size = size,
            });
        }

        [[nodiscard]]
        bool appendUniqueSemaphore(eastl::vector<vk::Semaphore> &semaphores, vk::Semaphore semaphore)
        {
            if (!semaphore)
            {
                return false;
            }

            if (eastl::find(semaphores.begin(), semaphores.end(), semaphore) == semaphores.end())
            {
                semaphores.push_back(semaphore);
                return true;
            }
            return false;
        }

        [[nodiscard]]
        const char *selectCollectedSubmitKind(size_t commandBufferCount, size_t internalCommandBufferCount, bool includedDeferredCopies, bool internalPresentSubmit)
        {
            if (commandBufferCount <= internalCommandBufferCount)
            {
                if (includedDeferredCopies)
                {
                    return "internal_deferred_copy";
                }
                return "internal_batch";
            }

            if (internalCommandBufferCount == 0u)
            {
                if (internalPresentSubmit)
                {
                    return "present_tracked";
                }
                return "user_render";
            }

            if (includedDeferredCopies)
            {
                if (internalPresentSubmit)
                {
                    return "present_tracked_with_internal_deferred_copy";
                }
                return "user_render_with_internal_deferred_copy";
            }

            if (internalPresentSubmit)
            {
                return "present_tracked_with_internal_batch";
            }
            return "user_render_with_internal_batch";
        }

        void collectEncoderSubmitStatistics(VKQueue *queue, const eastl::vector<vk::CommandBuffer> &commandBuffers, const eastl::vector<CommandEncoder> &encoders, size_t &outRetainedBufferCount, size_t &outRetainedTextureCount, size_t &outTotalPassCount, VKPresentManager::DirectPresentSubmitSync &outDirectPresentSync)
        {
            const VKPresentManager *presentManager = nullptr;
            if (const VKDevice *device = queue->getDevice(); device != nullptr)
            {
                presentManager = &device->getPresentManager();
            }

            for (size_t encoderIndex = 0; encoderIndex < encoders.size(); ++encoderIndex)
            {
                auto *vulkanEncoder = static_cast<VKCommandEncoder *>(encoders[encoderIndex].get());
                const vk::CommandBuffer encoderCommandBuffer = encoderIndex < commandBuffers.size() ? commandBuffers[encoderIndex] : vk::CommandBuffer{};
                if (vulkanEncoder != nullptr)
                {
                    outRetainedBufferCount += vulkanEncoder->getRetainedBufferCount();
                    outRetainedTextureCount += vulkanEncoder->getRetainedTextureCount();
                    outTotalPassCount += vulkanEncoder->getPassSummaryCount();
                    if (presentManager != nullptr)
                    {
                        VKPresentManager::DirectPresentSubmitSync encoderDirectPresentSync = {};
                        if (presentManager->tryFindPendingDirectPresentSubmit(vulkanEncoder->getRetainedTextures(), encoderDirectPresentSync))
                        {
                            if (outDirectPresentSync.texture != nullptr && outDirectPresentSync.texture != encoderDirectPresentSync.texture)
                            {
                                throw makeLogicError("VKQueue::submitRecordedCommandBuffersLocked found multiple pending direct-present swapchain textures in a single submission.");
                            }

                            outDirectPresentSync = encoderDirectPresentSync;
                        }
                    }
                }
                GVMLogTrace(queue,
                            QueueLogCategory,
                            "event=queue_submit_encoder queue={} encoder_index={} command_encoder_ptr={} command_buffer_ptr={} encoder_ended={} encoder_matches_submit_buffer={}",
                            queue->getQueueName().c_str(),
                            encoderIndex,
                            static_cast<void *>(vulkanEncoder),
                            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(encoderCommandBuffer)),
                            vulkanEncoder != nullptr ? vulkanEncoder->isEnded() : false,
                            vulkanEncoder != nullptr && vulkanEncoder->getNativeCommandBuffer() == encoderCommandBuffer);
            }
        }

        void prepareSubmitInfoSynchronization(const vk::SubmitInfo *submitTemplate, const VKPresentManager::DirectPresentSubmitSync *directPresentSync, eastl::vector<vk::Semaphore> &outWaitSemaphores, eastl::vector<vk::PipelineStageFlags> &outWaitStageMasks, eastl::vector<vk::Semaphore> &outSignalSemaphores)
        {
            if (submitTemplate != nullptr)
            {
                if (submitTemplate->waitSemaphoreCount > 0u)
                {
                    outWaitSemaphores.assign(submitTemplate->pWaitSemaphores, submitTemplate->pWaitSemaphores + submitTemplate->waitSemaphoreCount);
                    outWaitStageMasks.assign(submitTemplate->pWaitDstStageMask, submitTemplate->pWaitDstStageMask + submitTemplate->waitSemaphoreCount);
                }
                if (submitTemplate->signalSemaphoreCount > 0u)
                {
                    outSignalSemaphores.assign(submitTemplate->pSignalSemaphores, submitTemplate->pSignalSemaphores + submitTemplate->signalSemaphoreCount);
                }
            }

            if (directPresentSync != nullptr && directPresentSync->texture != nullptr)
            {
                if (appendUniqueSemaphore(outWaitSemaphores, directPresentSync->acquireSemaphore))
                {
                    outWaitStageMasks.push_back(directPresentSync->acquireStageMask == vk::PipelineStageFlags{} ? vk::PipelineStageFlagBits::eColorAttachmentOutput : directPresentSync->acquireStageMask);
                }
                static_cast<void>(appendUniqueSemaphore(outSignalSemaphores, directPresentSync->renderCompleteSemaphore));
            }
        }

    } // namespace

    void VKQueue::init(VKDevice *device, uint32_t queueFamilyIndex, vk::Queue queue, const eastl::string &queueName)
    {
        if (device == nullptr || queue == vk::Queue{})
        {
            throw makeInvalidArgument("VKQueue::init requires a valid device and queue.");
        }

        mDevice = device;
        mLogger = device->getLogger();
        mLogContext = device->getLogContext();
        mQueueFamilyIndex = queueFamilyIndex;
        mQueue = queue;
        mQueueName = queueName;

        mCommandContextPool.init(mDevice->getNativeDevice(), queueFamilyIndex);
        mSubmissionManager.init(*mDevice, queueName);
        mUploadAllocator.init(mDevice);
        mPendingReadbackTracker.init(this);
        mInternalCopyCommandEncoder = nullptr;
        mInternalBlitCopyPassEncoder = nullptr;
        mInternalComputeCopyPassEncoder = nullptr;
        mInternalCopyNeedsCompletionBarrier = false;
        mStateDB.reset();
        mBufferCopyMultipleRegionExecutor = eastl::make_intrusive<VKBufferCopyMultipleRegionExecutorImpl>();
        mBufferCopyMultipleRegionExecutor->init(mDevice);
        const eastl::vector<vk::QueueFamilyProperties> queueFamilies = toEastlVector(mDevice->getPhysicalDevice().getQueueFamilyProperties());
        const uint32_t timestampValidBits = mQueueFamilyIndex < queueFamilies.size() ? queueFamilies[mQueueFamilyIndex].timestampValidBits : 0u;
        GVMLogInfo(this, QueueLogCategory, "event=queue_init queue={} queue_family_index={} deferred_copy_mode=per_submission_encoder crash_breadcrumbs_macro={} timestamp_valid_bits={}", mQueueName.c_str(), mQueueFamilyIndex, false, timestampValidBits);
    }

    const eastl::shared_ptr<Internal::LogContext> &VKQueue::getLogContext() const
    {
        return mLogContext;
    }

    Logger VKQueue::getLogger() const
    {
        return mLogger;
    }

    CommandEncoder VKQueue::getOrCreateInternalCopyCommandEncoderLocked()
    {
        if (mInternalCopyCommandEncoder == nullptr)
        {
            mInternalCopyCommandEncoder = createCommandEncoderLocked();
        }
        return mInternalCopyCommandEncoder;
    }

    VKBlitPassEncoder &VKQueue::getOrCreateInternalBlitCopyPassLocked()
    {
        if (mInternalComputeCopyPassEncoder != nullptr)
        {
            mInternalComputeCopyPassEncoder->end();
            mInternalComputeCopyPassEncoder = nullptr;
        }
        if (mInternalBlitCopyPassEncoder == nullptr)
        {
            mInternalBlitCopyPassEncoder = getOrCreateInternalCopyCommandEncoderLocked()->beginBlitPass({});
        }
        return *static_cast<VKBlitPassEncoder *>(mInternalBlitCopyPassEncoder.get());
    }

    void VKQueue::endInternalCopyPassLocked()
    {
        if (mInternalBlitCopyPassEncoder != nullptr)
        {
            mInternalBlitCopyPassEncoder->end();
            mInternalBlitCopyPassEncoder = nullptr;
        }
        if (mInternalComputeCopyPassEncoder != nullptr)
        {
            mInternalComputeCopyPassEncoder->end();
            mInternalComputeCopyPassEncoder = nullptr;
        }
    }

    ComputePassEncoder VKQueue::getOrCreateInternalComputeCopyPassEncoderLocked()
    {
        if (mInternalBlitCopyPassEncoder != nullptr)
        {
            mInternalBlitCopyPassEncoder->end();
            mInternalBlitCopyPassEncoder = nullptr;
        }
        if (mInternalComputeCopyPassEncoder == nullptr)
        {
            mInternalComputeCopyPassEncoder = getOrCreateInternalCopyCommandEncoderLocked()->beginComputePass({});
            mInternalCopyNeedsCompletionBarrier = true;
        }
        return mInternalComputeCopyPassEncoder;
    }

    void VKQueue::appendInternalPendingCommandsLocked(eastl::vector<vk::CommandBuffer> &commandBuffers, eastl::vector<CommandEncoder> &encoders, bool &containsDeferredCopies)
    {
        if (!hasPendingInternalCommands())
        {
            return;
        }

        endInternalCopyPassLocked();
        if (mInternalCopyCommandEncoder != nullptr)
        {
            auto *encoder = static_cast<VKCommandEncoder *>(mInternalCopyCommandEncoder.get());
            if (!encoder->isEnded())
            {
                if (mInternalCopyNeedsCompletionBarrier)
                {
                    encoder->getTaskDependencyResolver().recordTrackedBufferWriteCompletionBarriers(encoder->getNativeCommandBuffer());
                }
                encoder->end();
            }
            encoder->markStatePendingSubmit();
            containsDeferredCopies = true;
            commandBuffers.push_back(encoder->getNativeCommandBuffer());
            encoders.push_back(mInternalCopyCommandEncoder);
            mInternalCopyCommandEncoder = nullptr;
            mInternalCopyNeedsCompletionBarrier = false;
        }
    }

    void VKQueue::destroyInternalPendingCommandsLocked()
    {
        endInternalCopyPassLocked();

        if (mInternalCopyCommandEncoder != nullptr)
        {
            auto *encoder = static_cast<VKCommandEncoder *>(mInternalCopyCommandEncoder.get());
            if (!encoder->isEnded())
            {
                encoder->end();
            }
            recycleCommandBufferContextLocked(encoder->takeCommandBufferContext());
            mInternalCopyCommandEncoder = nullptr;
        }
        mInternalCopyNeedsCompletionBarrier = false;
    }

    bool VKQueue::hasPendingInternalCommands() const
    {
        return mInternalCopyCommandEncoder != nullptr;
    }

    void VKQueue::recycleCommandBufferContext(eastl::shared_ptr<VKCommandBufferContext> context)
    {
        if (!context)
        {
            return;
        }

        ScopedLock lock(mSubmitMutex);
        recycleCommandBufferContextLocked(eastl::move(context));
    }

    CommandEncoder VKQueue::createCommandEncoder()
    {
        ensureAlive("VKQueue::createCommandEncoder");
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        return createCommandEncoderLocked();
    }


    void VKQueue::writeBuffer(BufferRange buffer, void const *data, uint64_t size)
    {
        ensureAlive("VKQueue::writeBuffer");
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKQueue::writeBuffer requires a valid destination buffer and source data.");
        }
        if (buffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKQueue::writeBuffer requires a valid destination buffer and source data.");
        }

        const uint64_t bytesToWrite = resolveBufferTransferSize(buffer, size, "VKQueue::writeBuffer");
        if (bytesToWrite == 0)
        {
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKUploadSlice slice = mUploadAllocator.allocateAndWrite(data, bytesToWrite);
        BufferRange destination = buffer;
        destination.size = bytesToWrite;
        mInternalCopyNeedsCompletionBarrier = true;
        getOrCreateInternalBlitCopyPassLocked().copyNativeBufferToBuffer(slice.buffer, slice.offset, destination);
    }

    void VKQueue::readBuffer(BufferRange buffer, void *data, uint64_t size)
    {
        ensureAlive("VKQueue::readBuffer");
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKQueue::readBuffer requires a valid source buffer and destination pointer.");
        }
        if (buffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKQueue::readBuffer requires a valid source buffer and destination pointer.");
        }

        const uint64_t bytesToRead = resolveBufferTransferSize(buffer, size, "VKQueue::readBuffer");
        if (bytesToRead == 0)
        {
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        auto *sourceBuffer = static_cast<VKBuffer *>(buffer.buffer.get());
        if (sourceBuffer->isHostVisible() && sourceBuffer->supportsCpuRead() && sourceBuffer->getPersistentMappedData() != nullptr)
        {
            const uint64_t readbackId = mReadbackSequence++;
            mDevice->retainPendingCommandBufferReference(buffer.buffer);
            mPendingReadbackTracker.enqueueDirect(readbackId, BufferRange(buffer.buffer, buffer.offset, bytesToRead), data, bytesToRead);
            return;
        }

        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackBuffer_", readbackId, bytesToRead);
        getOrCreateInternalBlitCopyPassLocked().copyBufferToBuffer(BufferRange(buffer.buffer, buffer.offset, bytesToRead), BufferRange(stagingBuffer, 0u, bytesToRead));
        mPendingReadbackTracker.enqueueStaging(readbackId, stagingBuffer, data, bytesToRead);
    }

    void VKQueue::writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize)
    {
        ensureAlive("VKQueue::writeTexture");
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKQueue::writeTexture requires a valid destination texture and source data.");
        }
        if (destination.texture.isNull())
        {
            throw makeInvalidArgument("VKQueue::writeTexture requires a valid destination texture and source data.");
        }
        const uint64_t requiredBytes = calculateRequiredTextureCopyBytes(destination.texture->getFormat(), dataLayout, writeSize);
        if (dataSize < requiredBytes)
        {
            throw makeInvalidArgument("VKQueue::writeTexture dataSize is smaller than the required upload footprint.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKUploadSlice slice = mUploadAllocator.allocateAndWrite(data, requiredBytes);
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTexture({}, slice.buffer, slice.offset, requiredBytes, dataLayout, destination, writeSize);
    }

    void VKQueue::readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
        ensureAlive("VKQueue::readTexture");
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKQueue::readTexture requires a valid source texture and destination pointer.");
        }
        if (source.texture.isNull())
        {
            throw makeInvalidArgument("VKQueue::readTexture requires a valid source texture and destination pointer.");
        }
        const uint64_t requiredBytes = calculateRequiredTextureCopyBytes(source.texture->getFormat(), dataLayout, readSize);
        if (dataSize < requiredBytes)
        {
            throw makeInvalidArgument("VKQueue::readTexture dataSize is smaller than the required readback footprint.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackTextureBuffer_", readbackId, dataSize);
        getOrCreateInternalBlitCopyPassLocked().copyTextureToBuffer(source, stagingBuffer, static_cast<VKBuffer *>(stagingBuffer.get())->getNativeBuffer(), 0u, requiredBytes, dataLayout, readSize);
        mPendingReadbackTracker.enqueueStaging(readbackId, stagingBuffer, data, dataSize);
    }

    void VKQueue::uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureAlive("VKQueue::uploadTexture");
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKQueue::uploadTexture requires a valid destination texture and source data.");
        }
        if (destination.isNull())
        {
            throw makeInvalidArgument("VKQueue::uploadTexture requires a valid destination texture and source data.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw makeInvalidArgument("VKQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKUploadSlice slice = mUploadAllocator.allocateAndWrite(data, dataStorageBytes);
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTextureMipChain({}, slice.buffer, slice.offset, dataStorageBytes, destination, mipmapOffsetBytes);
    }

    void VKQueue::uploadTexture(Texture destination, BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureAlive("VKQueue::uploadTexture");
        if (destination.isNull() || bufferRange.buffer.isNull())
        {
            throw makeInvalidArgument("VKQueue::uploadTexture requires a valid destination texture and source buffer.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw makeInvalidArgument("VKQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTextureMipChain(bufferRange, destination, mipmapOffsetBytes);
    }

    void VKQueue::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        ensureAlive("VKQueue::copyBufferToBuffer");
        {
            ScopedLock lock(mSubmitMutex);
            pollCompletedSubmissions();
            mInternalCopyNeedsCompletionBarrier = true;
            getOrCreateInternalBlitCopyPassLocked().copyBufferToBuffer(source, destination);
        }
    }

    void VKQueue::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureAlive("VKQueue::copyBufferToTexture");
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        auto *sourceBuffer = source.buffer.isNull() ? nullptr : static_cast<VKBuffer *>(source.buffer.get());
        const vk::Buffer nativeSourceBuffer = sourceBuffer != nullptr ? sourceBuffer->getNativeBuffer() : vk::Buffer{};
        uint64_t sourceSize = 0u;
        if (!destination.texture.isNull())
        {
            sourceSize = calculateRequiredTextureCopyBytes(destination.texture->getFormat(), source.layout, copySize);
        }
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTexture(source.buffer, nativeSourceBuffer, 0u, sourceSize, source.layout, destination, copySize);
    }

    void VKQueue::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        ensureAlive("VKQueue::copyTextureToBuffer");
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        mInternalCopyNeedsCompletionBarrier = true;
        auto *destinationBuffer = destination.buffer.isNull() ? nullptr : static_cast<VKBuffer *>(destination.buffer.get());
        const vk::Buffer nativeDestinationBuffer = destinationBuffer != nullptr ? destinationBuffer->getNativeBuffer() : vk::Buffer{};
        uint64_t destinationSize = 0u;
        if (!source.texture.isNull())
        {
            destinationSize = calculateRequiredTextureCopyBytes(source.texture->getFormat(), destination.layout, copySize);
        }
        getOrCreateInternalBlitCopyPassLocked().copyTextureToBuffer(source, destination.buffer, nativeDestinationBuffer, 0u, destinationSize, destination.layout, copySize);
    }

    void VKQueue::copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount)
    {
        ensureAlive("VKQueue::copyBufferToBufferMultipleRegion");
        if (regionCount == 0u)
        {
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKBufferCopyMultipleRegionExecutor executor = mBufferCopyMultipleRegionExecutor;
        executor->validateCopyRegions(source, destination, regions, regionCount);
        executor->encode(getOrCreateInternalComputeCopyPassEncoderLocked(), source, destination, regions, regionCount);
    }

    void VKQueue::fillBuffer(BufferRange source, uint32_t data)
    {
        ensureAlive("VKQueue::fillBuffer");
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        mInternalCopyNeedsCompletionBarrier = true;
        getOrCreateInternalBlitCopyPassLocked().fillBuffer(source, data);
    }

    void VKQueue::submit(const eastl::vector<CommandEncoder> &encoders)
    {
        ensureAlive("VKQueue::submit");
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submit", "queue={} requested_encoders={} pending_readbacks_before={}", mQueueName.c_str(), encoders.size(), mPendingReadbackTracker.pendingCount());
        VKSubmissionCompletion readbackWaitCompletion;
        bool assignedPendingReadbacks = false;

        {
            ScopedLock lock(mSubmitMutex);
            pollCompletedSubmissions();
            eastl::vector<vk::CommandBuffer> commandBuffers;
            eastl::vector<CommandEncoder> submittedEncoders;
            commandBuffers.reserve(encoders.size() + 2u);
            submittedEncoders.reserve(encoders.size() + 2u);

            bool internalContainsDeferredCopies = false;
            appendInternalPendingCommandsLocked(commandBuffers, submittedEncoders, internalContainsDeferredCopies);
            const size_t internalCommandBufferCount = commandBuffers.size();

            for (const CommandEncoder &encoder : encoders)
            {
                if (encoder == nullptr)
                {
                    continue;
                }

                auto *vulkanEncoder = static_cast<VKCommandEncoder *>(encoder.get());
                if (!vulkanEncoder->isEnded())
                {
                    vulkanEncoder->end();
                }
                vulkanEncoder->markStatePendingSubmit();
                commandBuffers.push_back(vulkanEncoder->getNativeCommandBuffer());
                submittedEncoders.push_back(encoder);
            }

            VKSubmissionCompletion completion;
            if (!commandBuffers.empty())
            {
                const bool includesUserCommandBuffers = commandBuffers.size() > internalCommandBufferCount;
                completion = submitRecordedCommandBuffersLocked(
                    commandBuffers,
                    submittedEncoders,
                    false,
                    includesUserCommandBuffers,
                    nullptr,
                    selectCollectedSubmitKind(commandBuffers.size(), internalCommandBufferCount, internalContainsDeferredCopies, false));
            }

            assignedPendingReadbacks = assignPendingReadbackCompletionLocked(completion, readbackWaitCompletion);
            if (!completion && !assignedPendingReadbacks)
            {
                GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.noop", "queue={}", mQueueName.c_str());
                GVMLogDebug(this, QueueLogCategory, "event=queue_submit_noop queue={}", mQueueName.c_str());
            }

            processCompletedSubmissionCleanupLocked();
        }

        if (!assignedPendingReadbacks)
        {
            return;
        }

        if (readbackWaitCompletion && !readbackWaitCompletion->completed.load(std::memory_order_acquire))
        {
            waitForSubmission(readbackWaitCompletion, "readback_resolve");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
    }

    VKSubmissionCompletion VKQueue::submitTracked(const vk::SubmitInfo &submitInfo, bool internalPresentSubmit)
    {
        const char *submitKind = internalPresentSubmit ? "tracked_internal_present" : "tracked_external";
        ensureAlive("VKQueue::submitTracked");
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitTracked", "queue={} command_buffers={} internal_present={} submit_kind={}", mQueueName.c_str(), submitInfo.commandBufferCount, internalPresentSubmit, submitKind);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.submit_tracked.command_buffer_count", submitInfo.commandBufferCount);

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        if (hasPendingInternalCommands() || mPendingReadbackTracker.hasPending())
        {
            throw makeLogicError("VKQueue::submitTracked cannot bypass explicit user submit while queue-internal commands or pending readbacks still exist.");
        }

        eastl::vector<vk::CommandBuffer> commandBuffers;
        commandBuffers.reserve(submitInfo.commandBufferCount);
        commandBuffers.insert(commandBuffers.end(), submitInfo.pCommandBuffers, submitInfo.pCommandBuffers + submitInfo.commandBufferCount);

        const eastl::vector<CommandEncoder> noEncoders;
        const VKSubmissionCompletion completion = submitRecordedCommandBuffersLocked(commandBuffers, noEncoders, internalPresentSubmit, false, &submitInfo, submitKind);
        if (!completion)
        {
            GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.noop", "queue={}", mQueueName.c_str());
            GVMLogDebug(this, QueueLogCategory, "event=queue_submit_noop queue={}", mQueueName.c_str());
        }
        processCompletedSubmissionCleanupLocked();
        return completion;
    }

    VKSubmissionCompletion VKQueue::submitPresentTracked(const vk::SubmitInfo &submitInfo)
    {
        ensureAlive("VKQueue::submitPresentTracked");

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        eastl::vector<vk::CommandBuffer> commandBuffers;
        eastl::vector<CommandEncoder> submittedEncoders;
        commandBuffers.reserve(submitInfo.commandBufferCount + 2u);
        submittedEncoders.reserve(2u);

        bool internalContainsDeferredCopies = false;
        appendInternalPendingCommandsLocked(commandBuffers, submittedEncoders, internalContainsDeferredCopies);
        const size_t internalCommandBufferCount = commandBuffers.size();

        commandBuffers.insert(commandBuffers.end(), submitInfo.pCommandBuffers, submitInfo.pCommandBuffers + submitInfo.commandBufferCount);

        VKSubmissionCompletion completion;
        if (!commandBuffers.empty())
        {
            const bool includesUserCommandBuffers = commandBuffers.size() > internalCommandBufferCount;
            completion = submitRecordedCommandBuffersLocked(
                commandBuffers,
                submittedEncoders,
                includesUserCommandBuffers,
                false,
                includesUserCommandBuffers ? &submitInfo : nullptr,
                selectCollectedSubmitKind(commandBuffers.size(), internalCommandBufferCount, internalContainsDeferredCopies, true));
        }

        if (!completion)
        {
            completion = mSubmissionManager.getMostRecentSubmissionCompletion();
        }

        VKSubmissionCompletion readbackCompletion;
        const bool assignedPendingReadbacks = assignPendingReadbackCompletionLocked(completion, readbackCompletion);
        static_cast<void>(readbackCompletion);
        if (!completion && !assignedPendingReadbacks)
        {
            GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.noop", "queue={}", mQueueName.c_str());
            GVMLogDebug(this, QueueLogCategory, "event=queue_submit_noop queue={}", mQueueName.c_str());
        }

        processCompletedSubmissionCleanupLocked();
        return completion;
    }

    void VKQueue::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        size_t inFlightCount = 0u;
        size_t pendingReadbackCount = 0u;
        bool hasPendingDeferredCopies = false;
        {
            ScopedLock lock(mSubmitMutex);
            inFlightCount = mSubmissionManager.getInFlightCount();
            pendingReadbackCount = mPendingReadbackTracker.pendingCount();
            hasPendingDeferredCopies = mInternalCopyCommandEncoder != nullptr;
        }
        GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_begin queue={} in_flight={} pending_readbacks={} has_pending_deferred_copy={}", mQueueName.c_str(), inFlightCount, pendingReadbackCount, hasPendingDeferredCopies);

        if (mDevice != nullptr && mDevice->getNativeDevice())
        {
            const auto waitIdleStartTime = std::chrono::steady_clock::now();
            GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_wait_idle_begin queue={}", mQueueName.c_str());
            mDevice->getNativeDevice().waitIdle();
            GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_wait_idle_end queue={} duration_ms={:.3f}", mQueueName.c_str(), durationMilliseconds(waitIdleStartTime));
        }

        {
            ScopedLock lock(mSubmitMutex);
            pollCompletedSubmissions();
            destroyInternalPendingCommandsLocked();
            mPendingReadbackTracker.destroy(mDevice);
            mSubmissionManager.destroy();
            mCommandContextPool.destroy();
        }
        mStateDB.reset();

        mUploadAllocator.destroy();
        if (mDevice != nullptr)
        {
            mDevice->collectReleasedResources();
        }
        mBufferCopyMultipleRegionExecutor = nullptr;
        mQueue = nullptr;
        mDestroyed = true;
        GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_end queue={}", mQueueName.c_str());
        if (mLogger != nullptr)
        {
            mLogger->flush();
        }
        mLogger = nullptr;
        mLogContext.reset();
    }

    void VKQueue::waitForSubmission(const VKSubmissionCompletion &completion, const char *waitSource)
    {
        ensureAlive("VKQueue::waitForSubmission");
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::waitForSubmission", "queue={} target_submission_id={} pending_readbacks={} wait_source={}", mQueueName.c_str(), submissionDiagnosticId(completion), mPendingReadbackTracker.pendingCount(), waitSource != nullptr ? waitSource : "unspecified");
        if (!completion || completion->completed.load(std::memory_order_acquire))
        {
            ScopedLock lock(mSubmitMutex);
            processCompletedSubmissionCleanupLocked();
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        if (completion->completed.load(std::memory_order_acquire))
        {
            return;
        }

        waitTrackedSubmissionLocked(completion);
    }

    bool VKQueue::trySampleSubmissionFenceStatus(const VKSubmissionCompletion &completion, SubmissionFenceStatusSnapshot &outStatus) const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.trySampleSubmissionFenceStatus(completion, outStatus);
    }

    void VKQueue::ensureAlive(const char *apiName) const
    {
        if (mDestroyed || mDevice == nullptr)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on a destroyed Vulkan queue.");
        }
    }

    CommandEncoder VKQueue::createCommandEncoderLocked()
    {
        eastl::shared_ptr<VKCommandBufferContext> commandContext = acquireCommandBufferContextLocked();
        CommandEncoder encoder = new VKCommandEncoder();
        static_cast<VKCommandEncoder *>(encoder.get())->init(*this, eastl::move(commandContext));
        return encoder;
    }

    void VKQueue::pollCompletedSubmissions()
    {
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::pollCompletedSubmissions", "queue={} in_flight_before={} pending_readbacks_before={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), mPendingReadbackTracker.pendingCount());
        const size_t retiredSubmissionCount = mSubmissionManager.pollCompletedSubmissions(std::bind(&VKQueue::handleRetiredSubmissionLocked, this, std::placeholders::_1));
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.poll_completed.retired_submission_count", retiredSubmissionCount);
        processCompletedSubmissionCleanupLocked();
        if (retiredSubmissionCount > 0u)
        {
            GVMLogDebug(this, QueueLogCategory, "event=queue_poll_completed queue={} retired_submission_count={} in_flight_after={} pending_readbacks_after={}", mQueueName.c_str(), retiredSubmissionCount, mSubmissionManager.getInFlightCount(), mPendingReadbackTracker.pendingCount());
        }
    }

    void VKQueue::enforceTrackedSubmissionLimitLocked()
    {
        while (mSubmissionManager.getInFlightCount() >= kMaxTrackedSubmissions)
        {
            const VKSubmissionCompletion oldestCompletion = mSubmissionManager.getOldestSubmissionCompletion();
            if (!oldestCompletion)
            {
                break;
            }

            GVMLogWarn(this, QueueLogCategory, "event=queue_submission_limit_wait_begin queue={} in_flight={} limit={} oldest_submission_id={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, submissionDiagnosticId(oldestCompletion));
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::enforceTrackedSubmissionLimitLocked.waitForSubmission", "queue={} target_submission_id={} in_flight_before={} limit={} wait_source={}", mQueueName.c_str(), submissionDiagnosticId(oldestCompletion), mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, "tracked_submission_limit");
            waitTrackedSubmissionLocked(oldestCompletion);
            GVMLogWarn(this, QueueLogCategory, "event=queue_submission_limit_wait_end queue={} in_flight_after={} limit={} oldest_submission_id={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, submissionDiagnosticId(oldestCompletion));
        }
    }

    void VKQueue::waitTrackedSubmissionLocked(const VKSubmissionCompletion &completion)
    {
        mSubmissionManager.waitForSubmission(completion, std::bind(&VKQueue::handleRetiredSubmissionLocked, this, std::placeholders::_1));
        processCompletedSubmissionCleanupLocked();
    }

    void VKQueue::handleRetiredSubmissionLocked(InFlightSubmission &submission)
    {
        mStateDB.retireSubmission(submission.submissionId);
        GVMLogDebug(this, QueueLogCategory, "{}", QueueDiagnostics::buildRetiredSubmissionLogMessage(mQueueName, submission, durationMilliseconds(submission.submittedAt)).c_str());
        for (const CommandEncoder &encoder : submission.encoders)
        {
            if (auto *vulkanEncoder = static_cast<VKCommandEncoder *>(encoder.get()); vulkanEncoder != nullptr)
            {
                vulkanEncoder->onExecutionCompleted();
                recycleCommandBufferContextLocked(vulkanEncoder->takeCommandBufferContext());
            }
        }
    }

    eastl::shared_ptr<VKCommandBufferContext> VKQueue::acquireCommandBufferContextLocked()
    {
        return mCommandContextPool.acquire();
    }

    void VKQueue::recycleCommandBufferContextLocked(eastl::shared_ptr<VKCommandBufferContext> context)
    {
        if (mDestroyed)
        {
            return;
        }
        mCommandContextPool.recycle(eastl::move(context));
    }

    bool VKQueue::assignPendingReadbackCompletionLocked(const VKSubmissionCompletion &submissionCompletion, VKSubmissionCompletion &outAssignedCompletion)
    {
        if (!mPendingReadbackTracker.hasPending())
        {
            return false;
        }

        const VKSubmissionCompletion readbackCompletion = submissionCompletion ? submissionCompletion : mSubmissionManager.getMostRecentSubmissionCompletion();
        mPendingReadbackTracker.assignUnassignedCompletion(readbackCompletion);
        outAssignedCompletion = mPendingReadbackTracker.getLastAssignedCompletion();
        return true;
    }

    void VKQueue::processCompletedSubmissionCleanupLocked()
    {
        mUploadAllocator.onCompleted();
        mPendingReadbackTracker.resolveCompleted(mDevice);
        if (mDevice != nullptr)
        {
            mDevice->collectReleasedResources();
        }
    }

    VKSubmissionCompletion VKQueue::submitRecordedCommandBuffersLocked(const eastl::vector<vk::CommandBuffer> &commandBuffers, const eastl::vector<CommandEncoder> &encoders, bool internalPresentSubmit, bool noteUserSubmit, const vk::SubmitInfo *submitTemplate, const char *submitKind)
    {
        const char *effectiveSubmitKind = submitKind;
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked", "queue={} command_buffers={} encoders={} internal_present={} submit_kind={}", mQueueName.c_str(), commandBuffers.size(), encoders.size(), internalPresentSubmit, effectiveSubmitKind);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.submit_recorded.command_buffer_count", commandBuffers.size());

        {
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.enforceTrackedSubmissionLimitLocked", "queue={} in_flight_before={} command_buffers={} internal_present={} submit_kind={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), commandBuffers.size(), internalPresentSubmit, effectiveSubmitKind);
            enforceTrackedSubmissionLimitLocked();
        }
        size_t totalPassCount = 0u;
        size_t diagnosticRetainedBufferCount = 0u;
        size_t diagnosticRetainedTextureCount = 0u;
        VKPresentManager::DirectPresentSubmitSync directPresentSync = {};
        eastl::vector<vk::Semaphore> submitWaitSemaphores;
        eastl::vector<vk::PipelineStageFlags> submitWaitStageMasks;
        eastl::vector<vk::Semaphore> submitSignalSemaphores;
        const uint32_t templateWaitSemaphoreCount = submitTemplate != nullptr ? submitTemplate->waitSemaphoreCount : 0u;
        const uint32_t templateSignalSemaphoreCount = submitTemplate != nullptr ? submitTemplate->signalSemaphoreCount : 0u;
        {
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.prepareSubmitInfo", "queue={} command_buffers={} encoders={} internal_present={} submit_kind={} has_submit_template={} template_wait_semaphores={} template_signal_semaphores={}", mQueueName.c_str(), commandBuffers.size(), encoders.size(), internalPresentSubmit, effectiveSubmitKind, submitTemplate != nullptr, templateWaitSemaphoreCount, templateSignalSemaphoreCount);
            collectEncoderSubmitStatistics(this, commandBuffers, encoders, diagnosticRetainedBufferCount, diagnosticRetainedTextureCount, totalPassCount, directPresentSync);
            prepareSubmitInfoSynchronization(submitTemplate, &directPresentSync, submitWaitSemaphores, submitWaitStageMasks, submitSignalSemaphores);
            const bool hasDirectPresentSync = directPresentSync.texture != nullptr;
            GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.prepare_submit_info_end", "queue={} submit_kind={} direct_present_texture={} wait_semaphores={} signal_semaphores={} retained_buffers={} retained_textures={}", mQueueName.c_str(), effectiveSubmitKind, hasDirectPresentSync, submitWaitSemaphores.size(), submitSignalSemaphores.size(), diagnosticRetainedBufferCount, diagnosticRetainedTextureCount);
        }

        const bool hasDirectPresentSync = directPresentSync.texture != nullptr;
        const VKSubmissionCompletion completion = mSubmissionManager.executeTrackedSubmit(mQueue, commandBuffers, submitTemplate, submitWaitSemaphores, submitWaitStageMasks, submitSignalSemaphores, encoders, internalPresentSubmit, noteUserSubmit, diagnosticRetainedBufferCount, diagnosticRetainedTextureCount, effectiveSubmitKind);
        const uint64_t submissionId = submissionDiagnosticId(completion);
        if (hasDirectPresentSync)
        {
            mDevice->getPresentManager().setDirectPresentSubmissionArmed(directPresentSync.texture);
        }

        eastl::vector<const VKCommandEncoder *> submittedEncoders;
        submittedEncoders.reserve(encoders.size());
        for (const CommandEncoder &encoder : encoders)
        {
            if (const auto *vulkanEncoder = static_cast<const VKCommandEncoder *>(encoder.get()); vulkanEncoder != nullptr)
            {
                submittedEncoders.push_back(vulkanEncoder);
            }
        }
        mStateDB.markCommandEncoderStatesSubmitted(submissionId, submittedEncoders);
        const size_t inFlightAfter = mSubmissionManager.getInFlightCount() + 1u;
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.trackSubmission", "queue={} submission_id={} encoders={} internal_present={} submit_kind={}", mQueueName.c_str(), submissionId, encoders.size(), internalPresentSubmit, effectiveSubmitKind);
        GVMLogDebug(this, QueueLogCategory, "{}", QueueDiagnostics::buildTrackedSubmissionLogMessage(mQueueName, submissionId, encoders.size(), totalPassCount, diagnosticRetainedBufferCount, diagnosticRetainedTextureCount, inFlightAfter, internalPresentSubmit).c_str());
        for (const CommandEncoder &encoder : encoders)
        {
            if (auto *vulkanEncoder = static_cast<VKCommandEncoder *>(encoder.get()); vulkanEncoder != nullptr)
            {
                vulkanEncoder->onSubmitted();
            }
        }
        mUploadAllocator.onSubmission(completion);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.track_submission.total_pass_count", totalPassCount);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.track_submission.in_flight_after", mSubmissionManager.getInFlightCount());
        return completion;
    }
} // namespace GVM::RHI::Vulkan
