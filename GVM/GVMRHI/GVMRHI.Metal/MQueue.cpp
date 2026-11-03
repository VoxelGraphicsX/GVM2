#include "MSubmissionState.hpp"
#include <EASTL/unordered_set.h>
#include "MQueue.hpp"
#include "MCommandEncoder.hpp"
#include "MAutoReleasePool.hpp"
#include "MBuffer.hpp"
#include "MBlitPassEncoder.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MTexture.hpp"
#include "MTextureCopyUtils.hpp"
#include "MUtilShaders/MUtilShaders.hpp"
#include <EASTL/make_intrusive.h>
#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
namespace GVM::RHI::Metal
{
    namespace
    {
        constexpr eastl::string_view QueueProbeCategory = "gvmrhi.metal.queue";
    } // namespace

    MQueue::MQueue()
    {
    }
    void MQueue::init(MDevice *device, const eastl::string &queueName, MTL::CommandQueue *native)
    {
        GVMCpuProbeScopeDetail(
            device,
            QueueProbeCategory,
            "MQueue::init",
            "queue={} frame_count={}",
            queueName.c_str(),
            device != nullptr ? device->MAX_FRAME_COUNT : 0);
        // if (mQueueName == "CopyQueue")
        // activateGCPool();
        this->mDevice = device;
        if (mDevice != nullptr)
        {
            mNativeQueue = native != nullptr ? native->retain() : mDevice->getNativeDevice()->newCommandQueue();
            if (mNativeQueue == nullptr)
            {
                throw std::runtime_error("Metal could not allocate its main command queue.");
            }
            mNativeQueue->setLabel(NS::String::string(queueName.c_str(), NS::UTF8StringEncoding));
        }
        mQueueName = queueName;
        mUploadAllocator.init(mDevice, mQueueName);

        const ShaderModule copyShader = mDevice->createShaderModule({
            .label = "BufferCopyMultipleRegionShader",
            .code = getCopyBufferToBufferMultipleRegion(),
        });
        mBufferCopyMultipleRegionExecutor.init(*mDevice, {.module = copyShader, .entryPoint = "executeCopyBufferCommands"});
    }

    CommandEncoder MQueue::createCommandEncoder()
    {
        mDevice->getLostState().throwIfLost("MQueue::createCommandEncoder");
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::createCommandEncoder",
            "queue={}",
            mQueueName.c_str());
        MTL::CommandBuffer *commandBuffer = nullptr;
        auto cmdDesp = MTL::CommandBufferDescriptor::alloc()->init();
        cmdDesp->setErrorOptions(MTL::CommandBufferErrorOptionEncoderExecutionStatus);
        commandBuffer = mNativeQueue->commandBuffer(cmdDesp);
        cmdDesp->release();

        eastl::intrusive_ptr<MCommandEncoder> encoder = eastl::make_intrusive<MCommandEncoder>();
        encoder->init(this->mDevice, commandBuffer, {});
        return encoder;
    }
    void MQueue::writeBuffer(BufferRange buffer, void const *data, uint64_t size)
    {
        mDevice->getLostState().throwIfLost("MQueue::writeBuffer");
        if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::writeBuffer received a null destination buffer.");
        }
        if (size == 0u)
        {
            return;
        }

        if (data == nullptr)
        {
            throw std::invalid_argument("MQueue::writeBuffer received null source data for a non-zero upload.");
        }

        const uint64_t storageSize = buffer.buffer->getStorageSize();
        const uint64_t remainingBytes = buffer.offset >= storageSize ? 0u : (storageSize - buffer.offset);
        const uint64_t writableBytes = std::min(buffer.size, remainingBytes);
        if (size > writableBytes)
        {
            throw std::out_of_range("MQueue::writeBuffer upload size exceeds the destination BufferRange.");
        }

        mUploadAllocator.writeBufferToCommand(getOrCreateTopBlitPassEncoder(), buffer, data, size);
    }

    void MQueue::readBuffer(BufferRange buffer, void *data, uint64_t size)
    {
        mDevice->getLostState().throwIfLost("MQueue::readBuffer");
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::readBuffer",
            "queue={} size={} offset={} range_size={}",
            mQueueName.c_str(),
            size,
            buffer.offset,
            buffer.size);
        if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::readBuffer received a null source buffer.");
        }
        if (size == 0u)
        {
            return;
        }

        if (data == nullptr)
        {
            throw std::invalid_argument("MQueue::readBuffer received null destination data for a non-zero readback.");
        }

        const uint64_t storageSize = buffer.buffer->getStorageSize();
        const uint64_t remainingBytes = buffer.offset >= storageSize ? 0u : (storageSize - buffer.offset);
        const uint64_t readableBytes = std::min(buffer.size, remainingBytes);
        if (size > readableBytes)
        {
            throw std::out_of_range("MQueue::readBuffer readback size exceeds the source BufferRange.");
        }

        auto *sourceBuffer = static_cast<MBuffer *>(buffer.buffer.get());
        if ((sourceBuffer->getUsage() & BufferUsage::MapRead) != 0)
        {
            getOrCreateReadbackCommandEncoder();
            mPendingBufferReadbacks.push_back({
                .source = BufferRange(buffer.buffer, buffer.offset, size),
                .stagingBuffer = Buffer(),
                .destination = data,
                .size = size,
                .usesDirectMapping = true,
            });
            return;
        }

        if ((sourceBuffer->getUsage() & BufferUsage::CopySrc) == 0)
        {
            throw std::invalid_argument("MQueue::readBuffer requires the source buffer to support MapRead or CopySrc.");
        }

        auto stagingBuffer = mDevice->createBuffer({
            .label = mQueueName + "_ReadbackBuffer_" + eastl::to_string(mReadbackSerial++),
            .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
            .size = size,
        });
        getOrCreateReadbackBlitPassEncoder()->copyBufferToBuffer(BufferRange(buffer.buffer, buffer.offset, size), BufferRange(stagingBuffer, 0, size));
        mPendingBufferReadbacks.push_back({
            .source = BufferRange(),
            .stagingBuffer = stagingBuffer,
            .destination = data,
            .size = size,
            .usesDirectMapping = false,
        });
    }

    void MQueue::writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize)
    {
        mDevice->getLostState().throwIfLost("MQueue::writeTexture");
        if (destination.texture.isNull() || destination.texture.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::writeTexture received a null destination texture.");
        }
        if (destination.mipLevel >= destination.texture->getMipLevelCount())
        {
            throw std::out_of_range("MQueue::writeTexture mip level exceeds the destination texture mip count.");
        }
        if (data == nullptr && dataSize != 0u)
        {
            throw std::invalid_argument("MQueue::writeTexture received null source data for a non-zero upload.");
        }
        const auto footprint = Private::computeTextureCopyFootprint("MQueue::writeTexture", destination.texture->getFormat(), dataLayout, writeSize, Detail::MetalTextureBytesPerRowAlignment);
        if (footprint.minimumDataBytes > dataSize)
        {
            throw std::invalid_argument("MQueue::writeTexture dataSize is smaller than the requested texture region.");
        }
        mUploadAllocator.writeTextureToCommand(getOrCreateTopBlitPassEncoder(), destination, data, dataLayout, writeSize, footprint);
    }

    void MQueue::readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
        mDevice->getLostState().throwIfLost("MQueue::readTexture");
        if (source.texture.isNull() || source.texture.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::readTexture received a null source texture.");
        }
        if (source.mipLevel >= source.texture->getMipLevelCount())
        {
            throw std::out_of_range("MQueue::readTexture mip level exceeds the source texture mip count.");
        }
        if (data == nullptr && dataSize != 0u)
        {
            throw std::invalid_argument("MQueue::readTexture received null destination data for a non-zero readback.");
        }

        auto *sourceTexture = static_cast<MTexture *>(source.texture.get());
        if ((sourceTexture->getUsage() & TextureUsage::CopySrc) == 0)
        {
            throw std::invalid_argument("MQueue::readTexture requires the source texture to support CopySrc.");
        }

        const auto footprint = Private::computeTextureCopyFootprint("MQueue::readTexture", source.texture->getFormat(), dataLayout, readSize, Detail::MetalTextureBytesPerRowAlignment);
        if (footprint.minimumDataBytes > dataSize)
        {
            throw std::invalid_argument("MQueue::readTexture dataSize is smaller than the requested texture region.");
        }
        auto stagingBuffer = mDevice->createBuffer({
            .label = mQueueName + "_ReadbackTextureBuffer_" + eastl::to_string(mReadbackSerial++),
            .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
            .size = footprint.stagingBytes,
        });

        getOrCreateReadbackBlitPassEncoder()->copyTextureToBuffer(
            source,
            {
                .layout =
                    {
                        .offset = 0,
                        .bytesPerRow = static_cast<uint32_t>(footprint.stagingBytesPerRow),
                        .rowsPerImage = static_cast<uint32_t>(footprint.rowsPerImage),
                    },
                .buffer = stagingBuffer,
            },
            readSize);

        mPendingTextureReadbacks.push_back({
            .stagingBuffer = stagingBuffer,
            .destination = static_cast<uint8_t *>(data) + dataLayout.offset,
            .tightRowBytes = footprint.tightRowBytes,
            .stagingBytesPerRow = footprint.stagingBytesPerRow,
            .stagingBytesPerImage = footprint.stagingBytesPerImage,
            .destinationBytesPerRow = footprint.bytesPerRow,
            .destinationBytesPerImage = footprint.bytesPerImage,
            .rowCount = footprint.blocksY,
            .depth = readSize.depth,
        });
    }

    void MQueue::uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        mDevice->getLostState().throwIfLost("MQueue::uploadTexture");
        if (destination.isNull() || data == nullptr)
            throw std::invalid_argument("MQueue::uploadTexture requires a valid destination texture and source data.");
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
            throw std::invalid_argument("MQueue::uploadTexture requires one mip offset per destination mip level.");

        for (uint32_t level = 0; level < destination->getMipLevelCount(); ++level)
        {
            const Extent3D size = {eastl::max(1u, destination->getWidth() >> level), eastl::max(1u, destination->getHeight() >> level), 1u};
            const ImageCopyTexture target = {.texture = destination, .mipLevel = level};
            const TextureDataLayout layout = {.offset = mipmapOffsetBytes[level]};
            writeTexture(target, data, dataStorageBytes, layout, size);
        }
    }

    void MQueue::uploadTexture(Texture destination, GVM::RHI::BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        mDevice->getLostState().throwIfLost("MQueue::uploadTexture");
        if (destination.isNull() || bufferRange.buffer.isNull())
            throw std::invalid_argument("MQueue::uploadTexture requires a valid destination texture and source buffer.");
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
            throw std::invalid_argument("MQueue::uploadTexture requires one mip offset per destination mip level.");
        if ((bufferRange.offset % Detail::TextureDataLayoutOffsetAlignment) != 0u)
            throw std::invalid_argument("MQueue::uploadTexture source offset must be a multiple of 4 bytes.");
        const uint64_t storageSize = bufferRange.buffer->getStorageSize();
        if (bufferRange.offset > storageSize)
            throw std::out_of_range("MQueue::uploadTexture source offset exceeds its buffer allocation.");
        const uint64_t availableBytes = bufferRange.size == WholeSize ? storageSize - bufferRange.offset : bufferRange.size;
        if (availableBytes > storageSize - bufferRange.offset)
            throw std::out_of_range("MQueue::uploadTexture source range exceeds its buffer allocation.");

        for (uint32_t level = 0; level < destination->getMipLevelCount(); ++level)
        {
            const Extent3D size = {eastl::max(1u, destination->getWidth() >> level), eastl::max(1u, destination->getHeight() >> level), 1u};
            TextureDataLayout layout = {.offset = mipmapOffsetBytes[level]};
            const auto footprint = Private::computeTextureCopyFootprint("MQueue::uploadTexture", destination->getFormat(), layout, size, 1u);
            if (footprint.minimumDataBytes > availableBytes)
            {
                throw std::invalid_argument("MQueue::uploadTexture dataSize is smaller than the requested texture region.");
            }
            layout.offset += bufferRange.offset;
            layout.bytesPerRow = static_cast<uint32_t>(footprint.bytesPerRow);
            layout.rowsPerImage = static_cast<uint32_t>(footprint.rowsPerImage);
            const ImageCopyTexture target = {.texture = destination, .mipLevel = level};
            copyBufferToTexture({.layout = layout, .buffer = bufferRange.buffer}, target, size);
        }
    }

    void MQueue::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        mDevice->getLostState().throwIfLost("MQueue::copyBufferToBuffer");
        getOrCreateTopBlitPassEncoder()->copyBufferToBuffer(source, destination);
    }

    void MQueue::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        mDevice->getLostState().throwIfLost("MQueue::copyBufferToTexture");
        getOrCreateTopBlitPassEncoder()->copyBufferToTexture(source, destination, copySize);
    }

    void MQueue::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        mDevice->getLostState().throwIfLost("MQueue::copyTextureToBuffer");
        getOrCreateReadbackBlitPassEncoder()->copyTextureToBuffer(source, destination, copySize);
    }

    void MQueue::copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount)
    {
        mDevice->getLostState().throwIfLost("MQueue::copyBufferToBufferMultipleRegion");
        if (regionCount == 0)
        {
            return;
        }
        if (source.isNull() || destination.isNull())
        {
            throw std::invalid_argument("MQueue::copyBufferToBufferMultipleRegion requires source and destination buffers.");
        }
        Private::BufferCopyMultipleRegionInputs inputs = {};
        inputs.source = source;
        inputs.destination = destination;
        inputs.regions = regions;
        inputs.regionCount = regionCount;
        inputs.sourceUsage = static_cast<MBuffer *>(source.get())->getUsage();
        inputs.destinationUsage = static_cast<MBuffer *>(destination.get())->getUsage();
        if (regions.get() != nullptr)
        {
            auto *regionsBuffer = static_cast<MBuffer *>(regions.get());
            inputs.regionsUsage = regionsBuffer->getUsage();
            inputs.cpuRegions = static_cast<const BufferCopyRegion *>(regionsBuffer->getNativeBuffer()->contents());
        }
        mBufferCopyMultipleRegionExecutor.validate(inputs);
        mBufferCopyMultipleRegionExecutor.encode(*getOrCreateComputeCopyPassEncoder(), inputs);
    }

    void MQueue::fillBuffer(BufferRange source, uint8_t value)
    {
        mDevice->getLostState().throwIfLost("MQueue::fillBuffer");
        getOrCreateTopBlitPassEncoder()->fillBuffer(source, value);
    }

    Readback MQueue::readBufferAsync(BufferRange source)
    {
        mDevice->getLostState().throwIfLost("MQueue::readBufferAsync");
        if (source.buffer.isNull() || source.buffer.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::readBufferAsync received a null source buffer.");
        }
        const uint64_t storageSize = source.buffer->getStorageSize();
        const uint64_t remainingBytes = source.offset >= storageSize ? 0u : (storageSize - source.offset);
        const uint64_t readSize = source.size == WholeSize ? remainingBytes : std::min(source.size, remainingBytes);
        if (readSize == 0u)
        {
            return MReadback(new MReadbackImpl(mDevice->getLostStateHandle(), nullptr, 0, 0));
        }
        auto *sourceBuffer = static_cast<MBuffer *>(source.buffer.get());
        if ((sourceBuffer->getUsage() & BufferUsage::MapRead) != 0)
        {
            auto readback = MReadback(new MReadbackImpl(mDevice->getLostStateHandle(), sourceBuffer->getNativeBuffer(), source.offset, readSize));
            getOrCreateReadbackCommandEncoder();
            mPendingAsyncReadbacks.push_back(readback);
            return readback;
        }
        if ((sourceBuffer->getUsage() & BufferUsage::CopySrc) == 0)
        {
            throw std::invalid_argument("MQueue::readBufferAsync requires the source buffer to support MapRead or CopySrc.");
        }
        auto stagingBuffer = mDevice->createBuffer({
            .label = mQueueName + "_ReadbackBuffer_" + eastl::to_string(mReadbackSerial++),
            .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
            .size = readSize,
        });
        MReadback readback;
        try
        {
            getOrCreateReadbackBlitPassEncoder()->copyBufferToBuffer(BufferRange(source.buffer, source.offset, readSize), BufferRange(stagingBuffer, 0, readSize));
            auto *native = static_cast<MBuffer *>(stagingBuffer.get())->getNativeBuffer();
            readback = MReadback(new MReadbackImpl(mDevice->getLostStateHandle(), native, 0, readSize));
            mPendingAsyncReadbacks.push_back(readback);
        }
        catch (...)
        {
            mDevice->freeBuffer(stagingBuffer);
            throw;
        }
        mDevice->freeBuffer(stagingBuffer);
        return readback;
    }

    Readback MQueue::readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
        mDevice->getLostState().throwIfLost("MQueue::readTextureAsync");
        if (source.texture.isNull() || source.texture.get() == nullptr)
        {
            throw std::invalid_argument("MQueue::readTextureAsync received a null source texture.");
        }
        if (source.mipLevel >= source.texture->getMipLevelCount())
        {
            throw std::out_of_range("MQueue::readTextureAsync mip level exceeds the source texture mip count.");
        }
        const auto footprint = Private::computeTextureCopyFootprint("MQueue::readTextureAsync", source.texture->getFormat(), dataLayout, readSize, Detail::MetalTextureBytesPerRowAlignment);
        auto stagingBuffer = mDevice->createBuffer({
            .label = mQueueName + "_ReadbackTextureBuffer_" + eastl::to_string(mReadbackSerial++),
            .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
            .size = footprint.stagingBytes,
        });
        MReadback readback;
        try
        {
            const uint32_t destinationBytesPerRow = dataLayout.bytesPerRow != 0 ? dataLayout.bytesPerRow : static_cast<uint32_t>(footprint.stagingBytesPerRow);
            getOrCreateReadbackBlitPassEncoder()->copyTextureToBuffer(
                source,
                {
                    .layout =
                        {
                            .offset = 0,
                            .bytesPerRow = destinationBytesPerRow,
                            .rowsPerImage = static_cast<uint32_t>(footprint.rowsPerImage),
                        },
                    .buffer = stagingBuffer,
                },
                readSize);
            auto *native = static_cast<MBuffer *>(stagingBuffer.get())->getNativeBuffer();
            readback = MReadback(new MReadbackImpl(mDevice->getLostStateHandle(), native, 0, footprint.stagingBytes));
            mPendingAsyncReadbacks.push_back(readback);
        }
        catch (...)
        {
            mDevice->freeBuffer(stagingBuffer);
            throw;
        }
        mDevice->freeBuffer(stagingBuffer);
        return readback;
    }


    void MQueue::submit(const eastl::vector<CommandEncoder> &encoders)
    {
        submitBatch(encoders, nullptr);
    }

    Submission MQueue::submit(const eastl::vector<CommandEncoder> &encoders, const eastl::vector<Submission> &waits)
    {
        return submitBatch(encoders, &waits);
    }

    void MQueue::publishAcceptedTail()
    {
        std::lock_guard lock(mSubmitMutex);
        if (mLastCommittedSubmitSerial == 0)
        {
            return;
        }
        auto event = NS::TransferPtr(mDevice->getNativeDevice()->newEvent());
        if (!event)
        {
            throw std::runtime_error("Metal could not allocate an event for the accepted main-queue tail.");
        }
        auto marker = createCommandEncoder();
        auto *native = static_cast<MCommandEncoder *>(marker.get())->getNativeCommandEncoder();
        native->encodeSignalEvent(event.get(), 1);
        marker->end();
        eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers{NS::RetainPtr(native)};
        auto completion = eastl::make_shared<MSubmissionState>(eastl::move(buffers), eastl::move(event), mDevice->getLostStateHandle());
        completion->arm();
        eastl::vector<Submission> waits;
        mDevice->queueHistory().prepare(this, {}, true, waits);
        commitCommandEncoder(marker, "accepted_tail", false);
        ++mNextSubmitSerial;
        mDevice->queueHistory().accept(this, {}, true, makeSubmission(eastl::move(completion)));
    }

    Submission MQueue::submitBatch(const eastl::vector<CommandEncoder> &encoders, const eastl::vector<Submission> *waits)
    {
        auto &history = mDevice->queueHistory();
        std::lock_guard deviceLock(history.mutex());
        std::lock_guard lock(mSubmitMutex);
        mDevice->getLostState().throwIfLost("MQueue::submit");
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::submit",
            "queue={} user_encoders={} pending_buffer_readbacks={} pending_texture_readbacks={}",
            mQueueName.c_str(),
            encoders.size(),
            mPendingBufferReadbacks.size(),
            mPendingTextureReadbacks.size());
        GVMCpuProbeValueU64(mDevice, QueueProbeCategory, "metal_queue.submit.user_encoder_count", encoders.size());
        CommandEncoder dependencyEncoder;
        CommandEncoder completionEncoder;
        eastl::shared_ptr<MSubmissionState> completion;
        const bool managed = mDevice->tracksQueue(*this);
        bool opaque = mWriteBufferCommandEncoder != nullptr || mReadbackCommandEncoder != nullptr;
        eastl::vector<MQueueAccess> uses;
        eastl::vector<Submission> dependencies;
        if (waits != nullptr)
        {
            dependencies = *waits;
        }
        eastl::unordered_set<const MCommandEncoder *> uniqueEncoders;
        for (const auto &encoder : encoders)
        {
            const auto *native = encoder ? dynamic_cast<const MCommandEncoder *>(encoder.get()) : nullptr;
            if (native == nullptr || !uniqueEncoders.insert(native).second)
            {
                throw std::invalid_argument("Submission requires unique Metal command encoders.");
            }
            native->validateSubmission(*mNativeQueue, waits != nullptr);
            if (managed)
            {
                native->collectAccesses(uses, opaque);
            }
        }
        if (managed)
        {
            history.prepare(this, uses, opaque, dependencies);
        }
        const bool publish = managed && (opaque || !uses.empty());
        if (waits != nullptr || publish || !dependencies.empty())
        {
            eastl::unordered_set<MTL::Event *> events;
            for (const auto &dependency : dependencies)
            {
                const auto state = eastl::dynamic_pointer_cast<MSubmissionState>(getSubmissionState(dependency));
                if (!state)
                {
                    throw std::invalid_argument("Submission dependency belongs to a different backend.");
                }
                events.insert(state->dependency(mDevice->getLostStateHandle()));
            }
            if (!events.empty())
            {
                dependencyEncoder = createCommandEncoder();
                auto *native = static_cast<MCommandEncoder *>(dependencyEncoder.get())->getNativeCommandEncoder();
                for (auto *event : events)
                {
                    native->encodeWait(event, 1);
                }
                dependencyEncoder->end();
            }
            auto event = NS::TransferPtr(mDevice->getNativeDevice()->newEvent());
            if (!event)
            {
                throw std::runtime_error("Metal could not allocate a submission event.");
            }
            completionEncoder = createCommandEncoder();
            static_cast<MCommandEncoder *>(completionEncoder.get())->getNativeCommandEncoder()->encodeSignalEvent(event.get(), 1);
            completionEncoder->end();
            eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers;
            buffers.reserve(encoders.size() + 4);
            for (const auto &encoder : {dependencyEncoder, mWriteBufferCommandEncoder})
            {
                if (encoder)
                {
                    buffers.push_back(NS::RetainPtr(static_cast<MCommandEncoder *>(encoder.get())->getNativeCommandEncoder()));
                }
            }
            for (const auto &encoder : encoders)
            {
                buffers.push_back(NS::RetainPtr(static_cast<MCommandEncoder *>(encoder.get())->getNativeCommandEncoder()));
            }
            for (const auto &encoder : {mReadbackCommandEncoder, completionEncoder})
            {
                if (encoder)
                {
                    buffers.push_back(NS::RetainPtr(static_cast<MCommandEncoder *>(encoder.get())->getNativeCommandEncoder()));
                }
            }
            completion = eastl::make_shared<MSubmissionState>(eastl::move(buffers), eastl::move(event), mDevice->getLostStateHandle());
            completion->arm();
        }
        try
        {
            submitInternal(encoders, dependencyEncoder, completionEncoder);
        }
        catch (...)
        {
            if (waits != nullptr || managed)
            {
                mDevice->getLostState().markLost(DeviceLostReason::SubmissionFailed, "Metal batch submission failed after commit began.");
            }
            throw;
        }
        if (publish)
        {
            history.accept(this, eastl::move(uses), opaque, makeSubmission(completion));
        }
        mUploadAllocator.markSubmitted(mNextSubmitSerial);
        finalizePendingReadbacks();
        postSubmit();
        mDevice->getObjectCache().releaseUnreferenced();
        pollCommittedCommandBuffers();
        return completion ? makeSubmission(eastl::move(completion)) : Submission{};
    }

    void MQueue::destroy()
    {
        std::lock_guard lock(mSubmitMutex);
        for (const auto &committed : mCommittedCommandBuffers)
        {
            committed->commandBuffer->waitUntilCompleted();
            if (committed->commandBuffer->status() == MTL::CommandBufferStatusError)
            {
                const auto *error = committed->commandBuffer->error();
                const char *message = error != nullptr && error->localizedDescription() != nullptr
                    ? error->localizedDescription()->cString(NS::UTF8StringEncoding) : "Metal submission failed during queue destruction.";
                mDevice->getLostState().markLost(DeviceLostReason::GpuFault,
                    message != nullptr ? message : "Metal submission failed during queue destruction.");
            }
        }
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::destroy",
            "queue={} pending_buffer_readbacks={} pending_texture_readbacks={}",
            mQueueName.c_str(),
            mPendingBufferReadbacks.size(),
            mPendingTextureReadbacks.size());
        if (mWriteBufferCommandEncoder)
        {
            if (mWriteBufferBlitPassEncoder)
            {
                mWriteBufferBlitPassEncoder->end();
            }
            mWriteBufferBlitPassEncoder = nullptr;
            if (mComputeCopyPassEncoder)
            {
                mComputeCopyPassEncoder->end();
            }
            mComputeCopyPassEncoder = nullptr;
        }
        if (mReadbackCommandEncoder)
        {
            if (mReadbackBlitPassEncoder)
            {
                mReadbackBlitPassEncoder->end();
            }
            mReadbackBlitPassEncoder = nullptr;
        }
        for (auto &pending : mPendingBufferReadbacks)
        {
            if (!pending.stagingBuffer.isNull())
            {
                mDevice->freeBuffer(pending.stagingBuffer);
            }
        }
        for (auto &pending : mPendingTextureReadbacks)
        {
            if (!pending.stagingBuffer.isNull() && pending.destination != nullptr)
            {
                mDevice->freeBuffer(pending.stagingBuffer);
            }
        }
        mPendingAsyncReadbacks.clear();
        mPendingBufferReadbacks.clear();
        mPendingTextureReadbacks.clear();
        mUploadAllocator.destroy();
        mWriteBufferCommandEncoder = nullptr;
        mComputeCopyPassEncoder = nullptr;
        mBufferCopyMultipleRegionExecutor = Private::BufferCopyMultipleRegionExecutor{};
        mReadbackCommandEncoder = nullptr;
        mLastCommittedEncoder = nullptr;
        mCommittedCommandBuffers.clear();
        if (this->mNativeQueue != nullptr)
        {
            this->mNativeQueue->release();
            this->mNativeQueue = nullptr;
        }
    }
    MTL::CommandQueue *MQueue::getNativeQueue() const
    {
        return this->mNativeQueue;
    }

    CommandEncoder MQueue::getOrCreateWriteBufferCommandEncoder()
    {
        if (!mWriteBufferCommandEncoder)
        {
            mWriteBufferCommandEncoder = createCommandEncoder();
        }
        return mWriteBufferCommandEncoder;
    }

    BlitPassEncoder MQueue::getOrCreateTopBlitPassEncoder()
    {
        if (mComputeCopyPassEncoder)
        {
            mComputeCopyPassEncoder->end();
            mComputeCopyPassEncoder = nullptr;
        }
        if (!mWriteBufferBlitPassEncoder)
        {
            mWriteBufferBlitPassEncoder = getOrCreateWriteBufferCommandEncoder()->beginBlitPass({});
        }
        return mWriteBufferBlitPassEncoder;
    }

    ComputePassEncoder MQueue::getOrCreateComputeCopyPassEncoder()
    {
        if (mWriteBufferBlitPassEncoder)
        {
            mWriteBufferBlitPassEncoder->end();
            mWriteBufferBlitPassEncoder = nullptr;
        }
        if (!mComputeCopyPassEncoder)
        {
            mComputeCopyPassEncoder = getOrCreateWriteBufferCommandEncoder()->beginComputePass({});
        }
        return mComputeCopyPassEncoder;
    }

    CommandEncoder MQueue::getOrCreateReadbackCommandEncoder()
    {
        if (!mReadbackCommandEncoder)
        {
            mReadbackCommandEncoder = createCommandEncoder();
        }
        return mReadbackCommandEncoder;
    }

    BlitPassEncoder MQueue::getOrCreateReadbackBlitPassEncoder()
    {
        getOrCreateReadbackCommandEncoder();
        if (!mReadbackBlitPassEncoder)
        {
            mReadbackBlitPassEncoder = getOrCreateReadbackCommandEncoder()->beginBlitPass({});
        }
        return mReadbackBlitPassEncoder;
    }

    void MQueue::trackCommittedCommandBuffer(MCommandEncoder &encoder, const char *role, uint64_t submitSerial)
    {
        auto *nativeCommandBuffer = encoder.getNativeCommandEncoder();
        if (nativeCommandBuffer == nullptr)
        {
            return;
        }
        mCommittedCommandBuffers.push_back(eastl::unique_ptr<MCommittedCommandBuffer>(new MCommittedCommandBuffer{NS::RetainPtr(nativeCommandBuffer), role, submitSerial}));
        mLastCommittedSubmitSerial = submitSerial;
    }

    void MQueue::pollCommittedCommandBuffers()
    {
        std::string messages;
        for (auto entry = mCommittedCommandBuffers.begin(); entry != mCommittedCommandBuffers.end();)
        {
            const MTL::CommandBufferStatus status = (*entry)->commandBuffer->status();
            if (status == MTL::CommandBufferStatusCompleted)
            {
                entry = mCommittedCommandBuffers.erase(entry);
                continue;
            }
            if (status == MTL::CommandBufferStatusError)
            {
                const auto *error = (*entry)->commandBuffer->error();
                const char *localizedDescription = error != nullptr ? error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) : "";
                const char *debugDescription = error != nullptr ? error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding) : "";
                if (!messages.empty())
                {
                    messages += "; ";
                }
                messages += "Metal command buffer '";
                messages += (*entry)->role;
                messages += "' failed: ";
                messages += localizedDescription != nullptr ? localizedDescription : "";
                messages += " ";
                messages += debugDescription != nullptr ? debugDescription : "";
                const auto code = error != nullptr ? error->code() : MTL::CommandBufferErrorNone;
                if (code == MTL::CommandBufferErrorTimeout || code == MTL::CommandBufferErrorPageFault || code == MTL::CommandBufferErrorDeviceRemoved)
                {
                    mDevice->getLostState().markLost(DeviceLostReason::GpuFault, messages.c_str());
                    throw DeviceLostError(messages);
                }
                entry = mCommittedCommandBuffers.erase(entry);
                continue;
            }
            ++entry;
        }
        mRetiredSubmitSerial = mCommittedCommandBuffers.empty()
            ? mLastCommittedSubmitSerial
            : mCommittedCommandBuffers.front()->submitSerial - 1u;
        mUploadAllocator.reclaim(mRetiredSubmitSerial);
        if (!messages.empty())
        {
            throw std::runtime_error(messages);
        }
    }

    void MQueue::submitInternal(const eastl::vector<CommandEncoder> &encoders, const CommandEncoder &dependency, const CommandEncoder &completion)
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::submitInternal",
            "queue={} user_encoders={} has_write={} has_readback={}",
            mQueueName.c_str(),
            encoders.size(),
            mWriteBufferCommandEncoder != nullptr,
            mReadbackCommandEncoder != nullptr);
        mLastCommittedEncoder = nullptr;
        if (dependency)
        {
            commitCommandEncoder(dependency, "dependency", false);
        }

        if (mWriteBufferCommandEncoder != nullptr)
        {
            if (mWriteBufferBlitPassEncoder != nullptr)
            {
                mWriteBufferBlitPassEncoder->end();
                mWriteBufferBlitPassEncoder = nullptr;
            }
            if (mComputeCopyPassEncoder != nullptr)
            {
                mComputeCopyPassEncoder->end();
                mComputeCopyPassEncoder = nullptr;
            }
            commitCommandEncoder(mWriteBufferCommandEncoder, "write", true);
        }
        for (const auto &encoder : encoders)
        {
            commitCommandEncoder(encoder, "user", false);
        }

        if (mReadbackCommandEncoder != nullptr)
        {
            if (mReadbackBlitPassEncoder != nullptr)
            {
                mReadbackBlitPassEncoder->end();
            }
            commitCommandEncoder(mReadbackCommandEncoder, "readback", true);
            auto *nativeReadback = static_cast<MCommandEncoder *>(mReadbackCommandEncoder.get())->getNativeCommandEncoder();
            for (auto &asyncReadback : mPendingAsyncReadbacks)
            {
                asyncReadback->assignCommandBuffer(*nativeReadback);
            }
            mPendingAsyncReadbacks.clear();
        }
        if (completion)
        {
            commitCommandEncoder(completion, "completion", false);
        }
    }

    void MQueue::commitCommandEncoder(const CommandEncoder &encoder, const char *role, bool endEncoder)
    {
        auto *metalEncoder = eastl::static_pointer_cast<MCommandEncoder>(encoder).get();
        if (endEncoder)
        {
            metalEncoder->end();
        }
        trackCommittedCommandBuffer(*metalEncoder, role, mNextSubmitSerial);
        metalEncoder->commit();
        mLastCommittedEncoder = metalEncoder;
    }

    void MQueue::finalizePendingReadbacks()
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::finalizePendingReadbacks",
            "queue={} pending_buffer_readbacks={} pending_texture_readbacks={} has_last_committed_encoder={}",
            mQueueName.c_str(),
            mPendingBufferReadbacks.size(),
            mPendingTextureReadbacks.size(),
            mLastCommittedEncoder != nullptr);
        if (mPendingBufferReadbacks.empty() && mPendingTextureReadbacks.empty())
        {
            return;
        }
        GVMCpuProbeValueU64(mDevice, QueueProbeCategory, "metal_queue.readback.pending_buffer_count", mPendingBufferReadbacks.size());
        GVMCpuProbeValueU64(mDevice, QueueProbeCategory, "metal_queue.readback.pending_texture_count", mPendingTextureReadbacks.size());

        bool requiresHostCopy = false;
        for (const auto &pending : mPendingBufferReadbacks)
        {
            requiresHostCopy = requiresHostCopy || pending.destination != nullptr;
        }
        for (const auto &pending : mPendingTextureReadbacks)
        {
            requiresHostCopy = requiresHostCopy || pending.destination != nullptr;
        }
        if (requiresHostCopy && mLastCommittedEncoder != nullptr)
        {
            mLastCommittedEncoder->waitUntilCompleted();
        }

        for (auto &pending : mPendingBufferReadbacks)
        {
            Buffer sourceBuffer = pending.usesDirectMapping ? pending.source.buffer : pending.stagingBuffer;
            if (pending.destination != nullptr)
            {
                bool isMapped = false;
                try
                {
                    sourceBuffer->map();
                    isMapped = true;
                    const void *mapped = sourceBuffer->getConstMappedRange(pending.source.offset, pending.size);
                    if (mapped == nullptr)
                    {
                        throw std::runtime_error("MQueue::finalizePendingReadbacks received a null mapped pointer while resolving buffer readback.");
                    }
                    std::memcpy(pending.destination, mapped, pending.size);
                    sourceBuffer->unmap();
                    isMapped = false;
                }
                catch (...)
                {
                    if (isMapped)
                    {
                        sourceBuffer->unmap();
                    }
                    throw;
                }
            }

            if (!pending.usesDirectMapping && !pending.stagingBuffer.isNull() && pending.destination != nullptr)
            {
                mDevice->freeBuffer(pending.stagingBuffer);
            }
        }
        mPendingBufferReadbacks.clear();

        for (auto &pending : mPendingTextureReadbacks)
        {
            if (pending.destination == nullptr)
            {
                continue;
            }
            bool isMapped = false;
            try
            {
                pending.stagingBuffer->map();
                isMapped = true;
                const auto *mapped = static_cast<const uint8_t *>(pending.stagingBuffer->getConstMappedRange(0u, WholeMapSize));
                if (mapped == nullptr)
                {
                    throw std::runtime_error("MQueue::finalizePendingReadbacks received a null mapped pointer while resolving texture readback.");
                }

                Detail::copyTextureRows(
                    mapped,
                    static_cast<uint8_t *>(pending.destination),
                    pending.tightRowBytes,
                    pending.stagingBytesPerRow,
                    pending.stagingBytesPerImage,
                    pending.destinationBytesPerRow,
                    pending.destinationBytesPerImage,
                    pending.rowCount,
                    pending.depth);

                pending.stagingBuffer->unmap();
                isMapped = false;
            }
            catch (...)
            {
                if (isMapped)
                {
                    pending.stagingBuffer->unmap();
                }
                throw;
            }
            mDevice->freeBuffer(pending.stagingBuffer);
        }
        mPendingTextureReadbacks.clear();
    }

    void MQueue::postSubmit()
    {
        mWriteBufferCommandEncoder = nullptr;
        mWriteBufferBlitPassEncoder = nullptr;

        mComputeCopyPassEncoder = nullptr;

        mReadbackCommandEncoder = nullptr;
        mReadbackBlitPassEncoder = nullptr;
        mLastCommittedEncoder = nullptr;
        ++mNextSubmitSerial;
    }

} // namespace GVM::RHI::Metal
