#include "VKQueue.hpp"
#include "VKReadback.hpp"

#include "Private/TimeUtils.hpp"
#include "VKBlitPassEncoder.hpp"
#include "VKBuffer.hpp"
#include "VKBufferCopyMultipleRegionSpirv.hpp"
#include "VKCommandEncoder.hpp"
#include "VKDevice.hpp"
#include "VKPresentManager.hpp"
#include "VKQueueDiagnostics.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/make_intrusive.h>
#include <EASTL/unordered_set.h>
#include <EASTL/utility.h>

#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/Private/RHIBufferContracts.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>

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

    VKSubmissionSerial VKQueue::getMostRecentSubmissionSerial() const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.getMostRecentSubmissionSerial();
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
        // This is an emergency high-water mark for tracked submissions, not the
        // CPU/GPU inflight-frame count. Queue-internal commands can legitimately
        // expand one frame into multiple same-queue submissions.
        constexpr size_t kMaxTrackedSubmissions = 64u;

        uint64_t resolveBufferTransferSize(const BufferRange &range, uint64_t requestedSize, const char *apiName)
        {
            const uint64_t resolvedSize = requestedSize == WholeSize ? range.size : requestedSize;
            if (resolvedSize > range.size)
            {
                throw std::out_of_range((eastl::string(apiName) + " requested more bytes than the supplied BufferRange.").c_str());
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

        /** Adds a binary WSI semaphore once and merges its required pipeline stages. */
        void appendPresentSemaphore(eastl::vector<vk::SemaphoreSubmitInfo> &infos,
            vk::Semaphore semaphore, vk::PipelineStageFlags2 stageMask)
        {
            for (auto &info : infos)
            {
                if (info.semaphore == semaphore)
                {
                    info.stageMask |= stageMask;
                    return;
                }
            }
            infos.push_back(vk::SemaphoreSubmitInfo{}.setSemaphore(semaphore).setStageMask(stageMask));
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

        void collectEncoderSubmitStatistics(VKQueue *queue, const eastl::vector<vk::CommandBuffer> &commandBuffers, const eastl::vector<CommandEncoder> &encoders, QueueDiagnostics::SubmissionStatistics &outStatistics, VKPresentManager::DirectPresentSubmitSync &outDirectPresentSync)
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
                    QueueDiagnostics::collectEncoderStatistics(outStatistics, vulkanEncoder);
                    if (presentManager != nullptr)
                    {
                        VKPresentManager::DirectPresentSubmitSync encoderDirectPresentSync = {};
                        if (presentManager->tryFindPendingDirectPresentSubmit(vulkanEncoder->getRetainedTextures(), encoderDirectPresentSync))
                        {
                            if (outDirectPresentSync.texture != nullptr && outDirectPresentSync.texture != encoderDirectPresentSync.texture)
                            {
                                throw std::logic_error("VKQueue::submitRecordedCommandBuffersLocked found multiple pending direct-present swapchain textures in a single submission.");
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

        /** Collects binary WSI waits/signals without changing caller-owned submit data. */
        void prepareSubmitInfoSynchronization(const vk::SubmitInfo2 *submitTemplate,
            const VKPresentManager::DirectPresentSubmitSync &directPresentSync,
            eastl::vector<vk::SemaphoreSubmitInfo> &waits,
            eastl::vector<vk::SemaphoreSubmitInfo> &signals)
        {
            if (submitTemplate != nullptr)
            {
                if (submitTemplate->waitSemaphoreInfoCount != 0u)
                    waits.assign(submitTemplate->pWaitSemaphoreInfos, submitTemplate->pWaitSemaphoreInfos + submitTemplate->waitSemaphoreInfoCount);
                if (submitTemplate->signalSemaphoreInfoCount != 0u)
                    signals.assign(submitTemplate->pSignalSemaphoreInfos, submitTemplate->pSignalSemaphoreInfos + submitTemplate->signalSemaphoreInfoCount);
            }
            if (directPresentSync.texture != nullptr)
            {
                appendPresentSemaphore(waits, directPresentSync.acquireSemaphore, directPresentSync.acquireStageMask);
                appendPresentSemaphore(signals, directPresentSync.renderCompleteSemaphore, vk::PipelineStageFlagBits2::eAllCommands);
            }
        }

    } // namespace

    void VKQueue::init(VKDevice *device, uint32_t queueFamilyIndex, vk::Queue queue, const eastl::string &queueName)
    {
        if (device == nullptr || queue == vk::Queue{})
        {
            throw std::invalid_argument("VKQueue::init requires a valid device and queue.");
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
        mInternalReadbackCommandEncoder = nullptr;
        mInternalReadbackBlitPassEncoder = nullptr;
        mStateDB.reset();
        const ShaderModule copyShader = mDevice->createShaderModule({
            .label = "BufferCopyMultipleRegionShader",
            .spirv = eastl::vector<uint32_t>(Detail::CopyBufferToBufferMultipleRegionSpirvWords,
                Detail::CopyBufferToBufferMultipleRegionSpirvWords + Detail::CopyBufferToBufferMultipleRegionSpirvWordCount),
        });
        mBufferCopyMultipleRegionExecutor.init(*mDevice, {.module = copyShader, .entryPoint = "main"});
        const auto &queueFamilies = mDevice->getCapabilities().getQueueFamilyProperties();
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
        }
        return mInternalComputeCopyPassEncoder;
    }

    CommandEncoder VKQueue::getOrCreateInternalReadbackCommandEncoderLocked()
    {
        if (mInternalReadbackCommandEncoder == nullptr)
        {
            mInternalReadbackCommandEncoder = createCommandEncoderLocked();
        }
        return mInternalReadbackCommandEncoder;
    }

    VKBlitPassEncoder &VKQueue::getOrCreateInternalReadbackBlitPassLocked()
    {
        if (mInternalReadbackBlitPassEncoder == nullptr)
        {
            mInternalReadbackBlitPassEncoder = getOrCreateInternalReadbackCommandEncoderLocked()->beginBlitPass({});
        }
        return *static_cast<VKBlitPassEncoder *>(mInternalReadbackBlitPassEncoder.get());
    }

    void VKQueue::endInternalReadbackPassLocked()
    {
        if (mInternalReadbackBlitPassEncoder != nullptr)
        {
            mInternalReadbackBlitPassEncoder->end();
            mInternalReadbackBlitPassEncoder = nullptr;
        }
    }

    void VKQueue::appendInternalPendingCommandsLocked(eastl::vector<vk::CommandBuffer> &commandBuffers, eastl::vector<CommandEncoder> &encoders, bool &containsDeferredCopies)
    {
        if (mInternalCopyCommandEncoder == nullptr)
        {
            return;
        }

        endInternalCopyPassLocked();
        auto *encoder = static_cast<VKCommandEncoder *>(mInternalCopyCommandEncoder.get());
        if (!encoder->isEnded())
        {
            encoder->end();
        }
        containsDeferredCopies = true;
        commandBuffers.push_back(encoder->getNativeCommandBuffer());
        encoders.push_back(mInternalCopyCommandEncoder);
    }

    void VKQueue::appendInternalReadbackPendingCommandsLocked(eastl::vector<vk::CommandBuffer> &commandBuffers, eastl::vector<CommandEncoder> &encoders, bool &containsDeferredCopies)
    {
        if (mInternalReadbackCommandEncoder == nullptr)
        {
            return;
        }

        endInternalReadbackPassLocked();
        auto *encoder = static_cast<VKCommandEncoder *>(mInternalReadbackCommandEncoder.get());
        if (!encoder->isEnded())
        {
            encoder->end();
        }
        containsDeferredCopies = true;
        commandBuffers.push_back(encoder->getNativeCommandBuffer());
        encoders.push_back(mInternalReadbackCommandEncoder);
    }

    void VKQueue::destroyInternalPendingCommandsLocked()
    {
        endInternalCopyPassLocked();
        endInternalReadbackPassLocked();

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
        if (mInternalReadbackCommandEncoder != nullptr)
        {
            auto *encoder = static_cast<VKCommandEncoder *>(mInternalReadbackCommandEncoder.get());
            if (!encoder->isEnded())
            {
                encoder->end();
            }
            recycleCommandBufferContextLocked(encoder->takeCommandBufferContext());
            mInternalReadbackCommandEncoder = nullptr;
        }
    }

    bool VKQueue::hasPendingInternalCommands() const
    {
        return mInternalCopyCommandEncoder != nullptr || mInternalReadbackCommandEncoder != nullptr;
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
        ensureNotLost();
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        return createCommandEncoderLocked();
    }


    void VKQueue::recordHostWrite(VKBuffer &buffer)
    {
        ScopedLock lock(mSubmitMutex);
        mStateDB.recordExternalBufferUsage(buffer, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite);
    }

    void VKQueue::writeBuffer(BufferRange buffer, void const *data, uint64_t size)
    {
        ensureAlive("VKQueue::writeBuffer");
        ensureNotLost();
        if (data == nullptr)
        {
            throw std::invalid_argument("VKQueue::writeBuffer requires a valid destination buffer and source data.");
        }
        if (buffer.buffer.isNull())
        {
            throw std::invalid_argument("VKQueue::writeBuffer requires a valid destination buffer and source data.");
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
        getOrCreateInternalBlitCopyPassLocked().copyNativeBufferToBuffer(slice.buffer, slice.offset, destination);
    }

    void VKQueue::readBuffer(BufferRange buffer, void *data, uint64_t size)
    {
        ensureAlive("VKQueue::readBuffer");
        ensureNotLost();
        if (data == nullptr)
        {
            throw std::invalid_argument("VKQueue::readBuffer requires a valid source buffer and destination pointer.");
        }
        if (buffer.buffer.isNull())
        {
            throw std::invalid_argument("VKQueue::readBuffer requires a valid source buffer and destination pointer.");
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
            sourceBuffer->retainReference();
            mPendingReadbackTracker.enqueueDirect(readbackId, BufferRange(buffer.buffer, buffer.offset, bytesToRead), data, bytesToRead);
            return;
        }

        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackBuffer_", readbackId, bytesToRead);
        getOrCreateInternalReadbackBlitPassLocked().copyBufferToBuffer(BufferRange(buffer.buffer, buffer.offset, bytesToRead), BufferRange(stagingBuffer, 0u, bytesToRead));
        mPendingReadbackTracker.enqueueStaging(readbackId, stagingBuffer, data, bytesToRead);
    }

    Readback VKQueue::readBufferAsync(BufferRange source)
    {
        ensureAlive("VKQueue::readBufferAsync");
        ensureNotLost();
        if (source.buffer.isNull())
        {
            throw std::invalid_argument("VKQueue::readBufferAsync requires a valid source buffer.");
        }
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        auto *sourceBuffer = static_cast<VKBuffer *>(source.buffer.get());
        const uint64_t bytesToRead = source.size == WholeSize ? sourceBuffer->getStorageSize() - source.offset : source.size;
        auto readback = VKReadback(new VKReadbackImpl());
        if (sourceBuffer->isHostVisible() && sourceBuffer->supportsCpuRead() && sourceBuffer->getPersistentMappedData() != nullptr)
        {
            const uint64_t readbackId = mReadbackSequence++;
            sourceBuffer->retainReference();
            readback->initDirect(this, source.buffer, source.offset, bytesToRead);
            mPendingReadbackTracker.enqueueAsyncDirect(readbackId, readback, BufferRange(source.buffer, source.offset, bytesToRead));
            return readback;
        }
        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackBuffer_", readbackId, bytesToRead);
        getOrCreateInternalReadbackBlitPassLocked().copyBufferToBuffer(BufferRange(source.buffer, source.offset, bytesToRead), BufferRange(stagingBuffer, 0u, bytesToRead));
        readback->initStaging(this, stagingBuffer, bytesToRead);
        mPendingReadbackTracker.enqueueAsyncStaging(readbackId, readback, stagingBuffer);
        return readback;
    }

    Readback VKQueue::readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
        ensureAlive("VKQueue::readTextureAsync");
        ensureNotLost();
        if (source.texture.isNull())
        {
            throw std::invalid_argument("VKQueue::readTextureAsync requires a valid source texture.");
        }
        const uint64_t requiredBytes = Private::computeTextureCopyFootprint(
            "VKQueue::readTextureAsync", source.texture->getFormat(), dataLayout, readSize, 1u).minimumDataBytes;
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackTextureBuffer_", readbackId, requiredBytes);
        getOrCreateInternalReadbackBlitPassLocked().copyTextureToBuffer(source, stagingBuffer, static_cast<VKBuffer *>(stagingBuffer.get())->getNativeBuffer(), 0u, requiredBytes, dataLayout, readSize);
        auto readback = VKReadback(new VKReadbackImpl());
        readback->initStaging(this, stagingBuffer, requiredBytes);
        mPendingReadbackTracker.enqueueAsyncStaging(readbackId, readback, stagingBuffer);
        return readback;
    }

    void VKQueue::writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize)
    {
        ensureAlive("VKQueue::writeTexture");
        ensureNotLost();
        if (data == nullptr)
        {
            throw std::invalid_argument("VKQueue::writeTexture requires a valid destination texture and source data.");
        }
        if (destination.texture.isNull())
        {
            throw std::invalid_argument("VKQueue::writeTexture requires a valid destination texture and source data.");
        }
        const uint64_t requiredBytes = Private::computeTextureCopyFootprint(
            "VKQueue::writeTexture", destination.texture->getFormat(), dataLayout, writeSize, 1u).minimumDataBytes;
        if (dataSize < requiredBytes)
        {
            throw std::invalid_argument("VKQueue::writeTexture dataSize is smaller than the required upload footprint.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKUploadSlice slice = mUploadAllocator.allocateAndWrite(data, requiredBytes);
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTexture({}, slice.buffer, slice.offset, requiredBytes, dataLayout, destination, writeSize);
    }

    void VKQueue::readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
        ensureAlive("VKQueue::readTexture");
        ensureNotLost();
        if (data == nullptr)
        {
            throw std::invalid_argument("VKQueue::readTexture requires a valid source texture and destination pointer.");
        }
        if (source.texture.isNull())
        {
            throw std::invalid_argument("VKQueue::readTexture requires a valid source texture and destination pointer.");
        }
        const uint64_t requiredBytes = Private::computeTextureCopyFootprint(
            "VKQueue::readTexture", source.texture->getFormat(), dataLayout, readSize, 1u).minimumDataBytes;
        if (dataSize < requiredBytes)
        {
            throw std::invalid_argument("VKQueue::readTexture dataSize is smaller than the required readback footprint.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const uint64_t readbackId = mReadbackSequence++;
        Buffer stagingBuffer = createQueueReadbackBuffer(*mDevice, mQueueName, "_ReadbackTextureBuffer_", readbackId, dataSize);
        getOrCreateInternalReadbackBlitPassLocked().copyTextureToBuffer(source, stagingBuffer, static_cast<VKBuffer *>(stagingBuffer.get())->getNativeBuffer(), 0u, requiredBytes, dataLayout, readSize);
        mPendingReadbackTracker.enqueueStaging(readbackId, stagingBuffer, data, dataSize);
    }

    void VKQueue::uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureAlive("VKQueue::uploadTexture");
        ensureNotLost();
        if (data == nullptr)
        {
            throw std::invalid_argument("VKQueue::uploadTexture requires a valid destination texture and source data.");
        }
        if (destination.isNull())
        {
            throw std::invalid_argument("VKQueue::uploadTexture requires a valid destination texture and source data.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw std::invalid_argument("VKQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        const VKUploadSlice slice = mUploadAllocator.allocateAndWrite(data, dataStorageBytes);
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTextureMipChain({}, slice.buffer, slice.offset, dataStorageBytes, destination, mipmapOffsetBytes);
    }

    void VKQueue::uploadTexture(Texture destination, BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureAlive("VKQueue::uploadTexture");
        ensureNotLost();
        if (destination.isNull() || bufferRange.buffer.isNull())
        {
            throw std::invalid_argument("VKQueue::uploadTexture requires a valid destination texture and source buffer.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw std::invalid_argument("VKQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTextureMipChain(bufferRange, destination, mipmapOffsetBytes);
    }

    void VKQueue::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        ensureAlive("VKQueue::copyBufferToBuffer");
        ensureNotLost();
        {
            ScopedLock lock(mSubmitMutex);
            pollCompletedSubmissions();
            getOrCreateInternalBlitCopyPassLocked().copyBufferToBuffer(source, destination);
        }
    }

    void VKQueue::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureAlive("VKQueue::copyBufferToTexture");
        ensureNotLost();
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        auto *sourceBuffer = source.buffer.isNull() ? nullptr : static_cast<VKBuffer *>(source.buffer.get());
        const vk::Buffer nativeSourceBuffer = sourceBuffer != nullptr ? sourceBuffer->getNativeBuffer() : vk::Buffer{};
        uint64_t sourceSize = 0u;
        if (!destination.texture.isNull())
        {
            sourceSize = Private::computeTextureCopyFootprint(
                "VKQueue::copyBufferToTexture", destination.texture->getFormat(), source.layout, copySize, 1u).minimumDataBytes;
        }
        getOrCreateInternalBlitCopyPassLocked().copyBufferToTexture(source.buffer, nativeSourceBuffer, 0u, sourceSize, source.layout, destination, copySize);
    }

    void VKQueue::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        ensureAlive("VKQueue::copyTextureToBuffer");
        ensureNotLost();
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        auto *destinationBuffer = destination.buffer.isNull() ? nullptr : static_cast<VKBuffer *>(destination.buffer.get());
        const vk::Buffer nativeDestinationBuffer = destinationBuffer != nullptr ? destinationBuffer->getNativeBuffer() : vk::Buffer{};
        uint64_t destinationSize = 0u;
        if (!source.texture.isNull())
        {
            destinationSize = Private::computeTextureCopyFootprint(
                "VKQueue::copyTextureToBuffer", source.texture->getFormat(), destination.layout, copySize, 1u).minimumDataBytes;
        }
        getOrCreateInternalReadbackBlitPassLocked().copyTextureToBuffer(source, destination.buffer, nativeDestinationBuffer, 0u, destinationSize, destination.layout, copySize);
    }

    void VKQueue::copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount)
    {
        ensureAlive("VKQueue::copyBufferToBufferMultipleRegion");
        ensureNotLost();
        if (regionCount == 0u)
        {
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();

        Private::BufferCopyMultipleRegionInputs inputs = {};
        inputs.source = source;
        inputs.destination = destination;
        inputs.regions = regions;
        inputs.regionCount = regionCount;
        if (source.get() != nullptr)
        {
            inputs.sourceUsage = static_cast<VKBuffer *>(source.get())->getUsage();
        }
        if (destination.get() != nullptr)
        {
            inputs.destinationUsage = static_cast<VKBuffer *>(destination.get())->getUsage();
        }
        if (regions.get() != nullptr)
        {
            auto *regionsBuffer = static_cast<VKBuffer *>(regions.get());
            inputs.regionsUsage = regionsBuffer->getUsage();
            inputs.cpuRegions = static_cast<const BufferCopyRegion *>(regionsBuffer->getPersistentMappedData());
        }
        mBufferCopyMultipleRegionExecutor.validate(inputs);
        mBufferCopyMultipleRegionExecutor.encode(*getOrCreateInternalComputeCopyPassEncoderLocked(), inputs);
    }

    void VKQueue::fillBuffer(BufferRange source, uint8_t value)
    {
        ensureAlive("VKQueue::fillBuffer");
        ensureNotLost();
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        getOrCreateInternalBlitCopyPassLocked().fillBuffer(source, value);
    }

    void VKQueue::submit(const eastl::vector<CommandEncoder> &encoders)
    {
        ensureAlive("VKQueue::submit");
        ensureNotLost();
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submit", "queue={} requested_encoders={} pending_readbacks_before={}", mQueueName.c_str(), encoders.size(), mPendingReadbackTracker.pendingCount());
        VKSubmissionSerial readbackWaitSerial = 0u;
        bool assignedPendingReadbacks = false;

        {
            ScopedLock lock(mSubmitMutex);
            eastl::unordered_set<const VKCommandEncoder *> uniqueEncoders;
            for (const CommandEncoder &encoder : encoders)
            {
                auto *vulkanEncoder = encoder != nullptr ? static_cast<VKCommandEncoder *>(encoder.get()) : nullptr;
                if (vulkanEncoder != nullptr &&
                    (vulkanEncoder->isSubmitted() || !uniqueEncoders.insert(vulkanEncoder).second))
                {
                    throw std::logic_error("Vulkan command encoder was already submitted.");
                }
            }
            pollCompletedSubmissions();
            eastl::vector<vk::CommandBuffer> commandBuffers;
            eastl::vector<CommandEncoder> submittedEncoders;
            commandBuffers.reserve(encoders.size() + 2u);
            submittedEncoders.reserve(encoders.size() + 2u);

            bool internalContainsDeferredCopies = false;
            appendInternalPendingCommandsLocked(commandBuffers, submittedEncoders, internalContainsDeferredCopies);
            const size_t headInternalCommandBufferCount = commandBuffers.size();

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
                commandBuffers.push_back(vulkanEncoder->getNativeCommandBuffer());
                submittedEncoders.push_back(encoder);
            }

            const size_t userCommandBufferCount = commandBuffers.size() - headInternalCommandBufferCount;
            appendInternalReadbackPendingCommandsLocked(commandBuffers, submittedEncoders, internalContainsDeferredCopies);
            const size_t internalCommandBufferCount = commandBuffers.size() - userCommandBufferCount;

            VKSubmissionSerial submissionSerial = 0u;
            if (!commandBuffers.empty())
            {
                const bool includesUserCommandBuffers = commandBuffers.size() > internalCommandBufferCount;
                submissionSerial = submitRecordedCommandBuffersLocked(
                    commandBuffers,
                    submittedEncoders,
                    false,
                    includesUserCommandBuffers,
                    nullptr,
                    selectCollectedSubmitKind(commandBuffers.size(), internalCommandBufferCount, internalContainsDeferredCopies, false));
            }

            assignedPendingReadbacks = assignPendingReadbackCompletionLocked(submissionSerial, readbackWaitSerial);
            if (submissionSerial == 0u && !assignedPendingReadbacks)
            {
                GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.noop", "queue={}", mQueueName.c_str());
                GVMLogDebug(this, QueueLogCategory, "event=queue_submit_noop queue={}", mQueueName.c_str());
            }

            processCompletedSubmissionCleanupLocked();
        }

        mDevice->getObjectCache().releaseUnreferenced();

        if (!assignedPendingReadbacks)
        {
            return;
        }

        if (readbackWaitSerial != 0u && !mSubmissionManager.isRetired(readbackWaitSerial))
        {
            waitForSubmission(readbackWaitSerial, "readback_resolve");
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
    }

    VKSubmissionSerial VKQueue::submitTracked(const vk::SubmitInfo2 &submitInfo, bool internalPresentSubmit)
    {
        const char *submitKind = internalPresentSubmit ? "tracked_internal_present" : "tracked_external";
        ensureAlive("VKQueue::submitTracked");
        ensureNotLost();
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitTracked", "queue={} command_buffers={} internal_present={} submit_kind={}", mQueueName.c_str(), submitInfo.commandBufferInfoCount, internalPresentSubmit, submitKind);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.submit_tracked.command_buffer_count", submitInfo.commandBufferInfoCount);

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        if (hasPendingInternalCommands() || mPendingReadbackTracker.hasPending())
        {
            throw std::logic_error("VKQueue::submitTracked cannot bypass explicit user submit while queue-internal commands or pending readbacks still exist.");
        }

        eastl::vector<vk::CommandBuffer> commandBuffers;
        commandBuffers.reserve(submitInfo.commandBufferInfoCount);
        for (uint32_t index = 0; index < submitInfo.commandBufferInfoCount; ++index)
            commandBuffers.push_back(submitInfo.pCommandBufferInfos[index].commandBuffer);

        const eastl::vector<CommandEncoder> noEncoders;
        const VKSubmissionSerial submissionSerial = submitRecordedCommandBuffersLocked(commandBuffers, noEncoders, internalPresentSubmit, false, &submitInfo, submitKind);
        if (submissionSerial == 0u)
        {
            GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.noop", "queue={}", mQueueName.c_str());
            GVMLogDebug(this, QueueLogCategory, "event=queue_submit_noop queue={}", mQueueName.c_str());
        }
        processCompletedSubmissionCleanupLocked();
        return submissionSerial;
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
            hasPendingDeferredCopies = mInternalCopyCommandEncoder != nullptr || mInternalReadbackCommandEncoder != nullptr;
        }
        GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_begin queue={} in_flight={} pending_readbacks={} has_pending_deferred_copy={}", mQueueName.c_str(), inFlightCount, pendingReadbackCount, hasPendingDeferredCopies);

        if (mDevice != nullptr && mDevice->getNativeDevice())
        {
            const auto waitIdleStartTime = std::chrono::steady_clock::now();
            GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_wait_idle_begin queue={}", mQueueName.c_str());
            try
            {
                mDevice->getNativeDevice().waitIdle();
            }
            catch (const vk::DeviceLostError &error)
            {
                mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
                GVMLogWarn(this, QueueLogCategory, "event=queue_destroy_wait_idle_device_lost reason={}", error.what());
            }
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
            mDevice->getResourceRegistry().collectReleasedResources();
        }
        mBufferCopyMultipleRegionExecutor = Private::BufferCopyMultipleRegionExecutor{};
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

    void VKQueue::waitForSubmission(VKSubmissionSerial serial, const char *waitSource)
    {
        ensureAlive("VKQueue::waitForSubmission");
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::waitForSubmission", "queue={} target_submission_id={} pending_readbacks={} wait_source={}", mQueueName.c_str(), serial, mPendingReadbackTracker.pendingCount(), waitSource != nullptr ? waitSource : "unspecified");
        if (mSubmissionManager.isRetired(serial))
        {
            ScopedLock lock(mSubmitMutex);
            processCompletedSubmissionCleanupLocked();
            return;
        }

        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        if (mSubmissionManager.isRetired(serial))
        {
            return;
        }

        waitTrackedSubmissionLocked(serial);
    }

    bool VKQueue::trySampleSubmissionStatus(VKSubmissionSerial serial, SubmissionStatusSnapshot &outStatus) const
    {
        ScopedLock lock(mSubmitMutex);
        return mSubmissionManager.trySampleSubmissionStatus(serial, outStatus);
    }

    bool VKQueue::isRetired(VKSubmissionSerial serial) const
    {
        return mSubmissionManager.isRetired(serial);
    }

    bool VKQueue::waitForOldestPendingSubmission()
    {
        ScopedLock lock(mSubmitMutex);
        pollCompletedSubmissions();
        if (mSubmissionManager.getInFlightCount() == 0)
        {
            return false;
        }
        const VKSubmissionSerial oldestSerial = mSubmissionManager.getOldestSubmissionSerial();
        if (oldestSerial == 0u)
        {
            return false;
        }
        waitTrackedSubmissionLocked(oldestSerial);
        return true;
    }

    void VKQueue::ensureAlive(const char *apiName) const
    {
        if (mDestroyed || mDevice == nullptr)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on a destroyed Vulkan queue.").c_str());
        }
    }

    void VKQueue::ensureNotLost() const
    {
        mDevice->getLostState().throwIfLost("VKQueue::ensureNotLost");
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
            const VKSubmissionSerial oldestSerial = mSubmissionManager.getOldestSubmissionSerial();
            if (oldestSerial == 0u)
            {
                break;
            }

            GVMLogWarn(this, QueueLogCategory, "event=queue_submission_limit_wait_begin queue={} in_flight={} limit={} oldest_submission_id={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, oldestSerial);
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::enforceTrackedSubmissionLimitLocked.waitForSubmission", "queue={} target_submission_id={} in_flight_before={} limit={} wait_source={}", mQueueName.c_str(), oldestSerial, mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, "tracked_submission_limit");
            waitTrackedSubmissionLocked(oldestSerial);
            GVMLogWarn(this, QueueLogCategory, "event=queue_submission_limit_wait_end queue={} in_flight_after={} limit={} oldest_submission_id={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), kMaxTrackedSubmissions, oldestSerial);
        }
    }

    void VKQueue::waitTrackedSubmissionLocked(VKSubmissionSerial serial)
    {
        mSubmissionManager.waitForSubmission(serial, std::bind(&VKQueue::handleRetiredSubmissionLocked, this, std::placeholders::_1));
        processCompletedSubmissionCleanupLocked();
    }

    void VKQueue::handleRetiredSubmissionLocked(InFlightSubmission &submission)
    {
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

    bool VKQueue::assignPendingReadbackCompletionLocked(VKSubmissionSerial submissionSerial, VKSubmissionSerial &outAssignedSerial)
    {
        if (!mPendingReadbackTracker.hasPending())
        {
            return false;
        }

        const VKSubmissionSerial readbackSerial = submissionSerial != 0u ? submissionSerial : mSubmissionManager.getMostRecentSubmissionSerial();
        mPendingReadbackTracker.assignUnassignedSubmissionSerial(readbackSerial);
        outAssignedSerial = mPendingReadbackTracker.getLastAssignedSubmissionSerial();
        return true;
    }

    void VKQueue::processCompletedSubmissionCleanupLocked()
    {
        const uint64_t lastRetiredSubmissionId = mSubmissionManager.getLastRetiredSubmissionId();
        mUploadAllocator.onCompleted(lastRetiredSubmissionId);
        mPendingReadbackTracker.resolveCompleted(mDevice, lastRetiredSubmissionId);
        if (mDevice != nullptr)
        {
            mDevice->getResourceRegistry().collectReleasedResources();
        }
    }

    VKSubmissionSerial VKQueue::submitRecordedCommandBuffersLocked(const eastl::vector<vk::CommandBuffer> &commandBuffers, const eastl::vector<CommandEncoder> &encoders, bool internalPresentSubmit, bool noteUserSubmit, const vk::SubmitInfo2 *submitTemplate, const char *submitKind)
    {
        const char *effectiveSubmitKind = submitKind;
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked", "queue={} command_buffers={} encoders={} internal_present={} submit_kind={}", mQueueName.c_str(), commandBuffers.size(), encoders.size(), internalPresentSubmit, effectiveSubmitKind);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.submit_recorded.command_buffer_count", commandBuffers.size());

        {
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.enforceTrackedSubmissionLimitLocked", "queue={} in_flight_before={} command_buffers={} internal_present={} submit_kind={}", mQueueName.c_str(), mSubmissionManager.getInFlightCount(), commandBuffers.size(), internalPresentSubmit, effectiveSubmitKind);
            enforceTrackedSubmissionLimitLocked();
        }
        QueueDiagnostics::SubmissionStatistics submitStatistics = {};
        VKPresentManager::DirectPresentSubmitSync directPresentSync = {};
        eastl::vector<vk::SemaphoreSubmitInfo> submitWaitSemaphores;
        eastl::vector<vk::SemaphoreSubmitInfo> submitSignalSemaphores;
        const uint32_t templateWaitSemaphoreCount = submitTemplate != nullptr ? submitTemplate->waitSemaphoreInfoCount : 0u;
        const uint32_t templateSignalSemaphoreCount = submitTemplate != nullptr ? submitTemplate->signalSemaphoreInfoCount : 0u;
        {
            GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.prepareSubmitInfo", "queue={} command_buffers={} encoders={} internal_present={} submit_kind={} has_submit_template={} template_wait_semaphores={} template_signal_semaphores={}", mQueueName.c_str(), commandBuffers.size(), encoders.size(), internalPresentSubmit, effectiveSubmitKind, submitTemplate != nullptr, templateWaitSemaphoreCount, templateSignalSemaphoreCount);
            collectEncoderSubmitStatistics(this, commandBuffers, encoders, submitStatistics, directPresentSync);
            const bool hasDirectPresentSync = directPresentSync.texture != nullptr;
            GVMCpuProbeInstantDetail(this, QueueLogCategory, "queue.submit.prepare_submit_info_end", "queue={} submit_kind={} direct_present_texture={} wait_semaphores={} signal_semaphores={} retained_buffers={} retained_textures={}", mQueueName.c_str(), effectiveSubmitKind, hasDirectPresentSync, submitWaitSemaphores.size(), submitSignalSemaphores.size(), submitStatistics.retainedBufferCount, submitStatistics.retainedTextureCount);
        }

        const bool hasDirectPresentSync = directPresentSync.texture != nullptr;
        VKSubmissionSerial submissionSerial = 0u;
        try
        {
            const Texture presentTexture = hasDirectPresentSync ? directPresentSync.texture->getHandle() : Texture{};
            for (size_t index = 0; index < encoders.size(); ++index)
                static_cast<VKCommandEncoder *>(encoders[index].get())->encodeNative(
                    index + 1 == encoders.size() ? presentTexture : Texture{});
            // First-use stages become known while lowering, never from broad layout guesses.
            if (hasDirectPresentSync)
                if (!mDevice->getPresentManager().tryGetDirectPresentSubmitSync(directPresentSync.texture, directPresentSync))
                    throw std::logic_error("Direct-present acquisition disappeared during submission.");
            submitWaitSemaphores.clear(); submitSignalSemaphores.clear();
            prepareSubmitInfoSynchronization(submitTemplate, directPresentSync, submitWaitSemaphores, submitSignalSemaphores);
            submissionSerial = mSubmissionManager.executeTrackedSubmit(mQueue, commandBuffers, submitWaitSemaphores, submitSignalSemaphores,
                encoders, internalPresentSubmit, noteUserSubmit, submitStatistics, effectiveSubmitKind);
            if (noteUserSubmit && mDevice != nullptr && mDevice->getAllocator() != nullptr)
            {
                vmaSetCurrentFrameIndex(mDevice->getAllocator(), static_cast<uint32_t>(submissionSerial));
            }
        }
        catch (const vk::DeviceLostError &error)
        {
            mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
            throw DeviceLostError(error.what());
        }
        catch (const std::exception &error)
        {
            mDevice->getLostState().markLost(DeviceLostReason::SubmissionFailed, error.what());
            throw DeviceLostError(error.what());
        }
        catch (...)
        {
            mDevice->getLostState().markLost(DeviceLostReason::SubmissionFailed, "unknown exception");
            throw DeviceLostError("unknown exception");
        }
        mInternalCopyCommandEncoder = nullptr;
        mInternalReadbackCommandEncoder = nullptr;
        const uint64_t submissionId = submissionSerial;
        if (hasDirectPresentSync)
        {
            mDevice->getPresentManager().setDirectPresentSubmissionArmed(directPresentSync.texture);
        }

        const size_t inFlightAfter = mSubmissionManager.getInFlightCount();
        GVMCpuProbeScopeDetail(this, QueueLogCategory, "VKQueue::submitRecordedCommandBuffersLocked.trackSubmission", "queue={} submission_id={} encoders={} internal_present={} submit_kind={}", mQueueName.c_str(), submissionId, encoders.size(), internalPresentSubmit, effectiveSubmitKind);
        GVMLogDebug(this, QueueLogCategory, "{}", QueueDiagnostics::buildTrackedSubmissionLogMessage(mQueueName, submissionId, encoders.size(), submitStatistics, inFlightAfter, internalPresentSubmit).c_str());
        for (const CommandEncoder &encoder : encoders)
        {
            if (auto *vulkanEncoder = static_cast<VKCommandEncoder *>(encoder.get()); vulkanEncoder != nullptr)
            {
                vulkanEncoder->onSubmitted();
            }
        }
        mUploadAllocator.onSubmission(submissionSerial);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.track_submission.total_pass_count", submitStatistics.totalPassCount);
        GVMCpuProbeValueU64(this, QueueLogCategory, "queue.track_submission.in_flight_after", mSubmissionManager.getInFlightCount());
        return submissionSerial;
    }
} // namespace GVM::RHI::Vulkan
