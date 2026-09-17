#include "MQueue.hpp"
#include "MAutoReleasePool.hpp"
#include "MBuffer.hpp"
#include "MBlitPassEncoder.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MTexture.hpp"
#include "MTextureCopyUtils.hpp"
#include <EASTL/make_intrusive.h>
#include <GVMRHI/GVMCpuProbe.hpp>
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
    void MQueue::init(MDevice *device, const eastl::string &queueName)
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
            mNativeQueue = mDevice->getNativeDevice()->newCommandQueue();
            mNativeQueue->setLabel(NS::String::string(queueName.c_str(), NS::UTF8StringEncoding));
        }
        mQueueName = queueName;
        mWriteBufferPools.resize(device->MAX_FRAME_COUNT);
        for (int i = 0; i < mWriteBufferPools.size(); i++)
        {
            auto &pool = mWriteBufferPools[i];
            pool = eastl::make_shared<MWriteBufferPool>();
            pool->init(mDevice, mQueueName + "_frame_" + eastl::to_string(i) + "_");
        }

        mBufferCopyMultipleRegionExecutor = eastl::make_intrusive<MBufferCopyMultipleRegionExecutorImpl>();
        mBufferCopyMultipleRegionExecutor->create(mDevice);
    }

    CommandEncoder MQueue::createCommandEncoder()
    {
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

        prepareWriteBufferPool();
        mWriteBufferPools.at(mDevice->getFrameIndex())->writeBufferToCommand(getOrCreateTopBlitPassEncoder(), buffer, data, size, mNextSubmitSerial);
    }

    void MQueue::readBuffer(BufferRange buffer, void *data, uint64_t size)
    {
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
        Detail::validateTextureDataRequest("MQueue::writeTexture", destination.texture->getFormat(), dataLayout, writeSize, dataSize);
        prepareWriteBufferPool();
        mWriteBufferPools.at(mDevice->getFrameIndex())->writeTextureToCommand(getOrCreateTopBlitPassEncoder(), destination, data, dataSize, dataLayout, writeSize, mNextSubmitSerial);
    }

    void MQueue::readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize)
    {
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

        Detail::validateTextureDataRequest("MQueue::readTexture", source.texture->getFormat(), dataLayout, readSize, dataSize);

        const Detail::TextureCopyFootprint footprint = Detail::buildTextureCopyFootprint(source.texture->getFormat(), dataLayout, readSize, true);
        auto stagingBuffer = mDevice->createBuffer({
            .label = mQueueName + "_ReadbackTextureBuffer_" + eastl::to_string(mReadbackSerial++),
            .usage = BufferUsage::CopyDst | BufferUsage::MapRead,
            .size = footprint.requiredBytes,
        });

        getOrCreateReadbackBlitPassEncoder()->copyTextureToBuffer(
            source,
            {
                .layout =
                    {
                        .offset = 0,
                        .bytesPerRow = static_cast<uint32_t>(footprint.metalBytesPerRow),
                        .rowsPerImage = dataLayout.rowsPerImage != 0 ? dataLayout.rowsPerImage : footprint.blocksY,
                    },
                .buffer = stagingBuffer,
            },
            readSize);

        mPendingTextureReadbacks.push_back({
            .stagingBuffer = stagingBuffer,
            .destination = static_cast<uint8_t *>(data) + dataLayout.offset,
            .tightRowBytes = footprint.tightRowBytes,
            .stagingBytesPerRow = footprint.metalBytesPerRow,
            .stagingBytesPerImage = footprint.metalBytesPerImage,
            .destinationBytesPerRow = footprint.logicalBytesPerRow,
            .destinationBytesPerImage = footprint.logicalBytesPerImage,
            .rowCount = footprint.blocksY,
            .depth = readSize.depth,
        });
    }

    void MQueue::uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        if (destination.isNull() || data == nullptr)
        {
            throw std::invalid_argument("MQueue::uploadTexture requires a valid destination texture and source data.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw std::invalid_argument("MQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        for (uint32_t level = 0; level < destination->getMipLevelCount(); ++level)
        {
            const uint32_t mipWidth = std::max(1u, destination->getWidth() >> level);
            const uint32_t mipHeight = std::max(1u, destination->getHeight() >> level);

            uint32_t bytesPerRow = 0;
            uint32_t rowsPerImage = 0;

            const Detail::TextureCopyFootprint footprint = Detail::buildTextureCopyFootprint(
                destination->getFormat(),
                {},
                {mipWidth, mipHeight, 1},
                false);
            bytesPerRow = static_cast<uint32_t>(footprint.logicalBytesPerRow);
            rowsPerImage = static_cast<uint32_t>(footprint.logicalRowsPerImage);


            // ----- 目标描述 -----
            ImageCopyTexture dst = {};
            dst.texture = destination;
            dst.mipLevel = level;
            dst.origin = {0, 0, 0};
            dst.aspect = TextureAspect::All;

            // ----- 源数据布局 -----
            TextureDataLayout layout = {};
            layout.offset = mipmapOffsetBytes[level]; // 数据从开头开始
            layout.bytesPerRow = bytesPerRow;
            layout.rowsPerImage = rowsPerImage;

            // ----- 拷贝尺寸 -----
            Extent3D size = {mipWidth, mipHeight, 1};
            Detail::validateTextureDataRequest("MQueue::uploadTexture", destination->getFormat(), layout, size, dataStorageBytes);

            // 写入队列
            writeTexture(dst, data, dataStorageBytes, layout, size);
        }
    }

    void MQueue::uploadTexture(Texture destination, GVM::RHI::BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        if (destination.isNull() || bufferRange.buffer.isNull())
        {
            throw std::invalid_argument("MQueue::uploadTexture requires a valid destination texture and source buffer.");
        }
        if (mipmapOffsetBytes.size() < destination->getMipLevelCount())
        {
            throw std::invalid_argument("MQueue::uploadTexture requires one mip offset per destination mip level.");
        }

        const uint64_t accessibleBytes = bufferRange.offset + bufferRange.size;
        for (uint32_t level = 0; level < destination->getMipLevelCount(); ++level)
        {
            const uint32_t mipWidth = std::max(1u, destination->getWidth() >> level);
            const uint32_t mipHeight = std::max(1u, destination->getHeight() >> level);

            uint32_t bytesPerRow = 0;
            uint32_t rowsPerImage = 0;

            const Detail::TextureCopyFootprint footprint = Detail::buildTextureCopyFootprint(
                destination->getFormat(),
                {},
                {mipWidth, mipHeight, 1},
                false);
            bytesPerRow = static_cast<uint32_t>(footprint.logicalBytesPerRow);
            rowsPerImage = static_cast<uint32_t>(footprint.logicalRowsPerImage);


            // ----- 目标描述 -----
            ImageCopyTexture dst = {};
            dst.texture = destination;
            dst.mipLevel = level;
            dst.origin = {0, 0, 0};
            dst.aspect = TextureAspect::All;

            // ----- 源数据布局 -----
            TextureDataLayout layout = {};
            layout.offset = bufferRange.offset + mipmapOffsetBytes[level];
            layout.bytesPerRow = bytesPerRow;
            layout.rowsPerImage = rowsPerImage;

            // ----- 拷贝尺寸 -----
            Extent3D size = {mipWidth, mipHeight, 1};
            Detail::validateTextureDataRequest("MQueue::uploadTexture", destination->getFormat(), layout, size, accessibleBytes);

            // 写入队列
            ImageCopyBuffer source{};
            source.layout = layout;
            source.buffer = bufferRange.buffer;
            copyBufferToTexture(source, dst, size);
        }
    }

    void MQueue::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        getOrCreateTopBlitPassEncoder()->copyBufferToBuffer(source, destination);
    }

    void MQueue::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        getOrCreateTopBlitPassEncoder()->copyBufferToTexture(source, destination, copySize);
    }

    void MQueue::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        getOrCreateReadbackBlitPassEncoder()->copyTextureToBuffer(source, destination, copySize);
    }

    void MQueue::copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount)
    {
        if (regionCount == 0)
        {
            return;
        }
        auto executor = mBufferCopyMultipleRegionExecutor;
        executor->validateCopyRegions(regions, regionCount);
        executor->execute(getOrCreateComputeCopyPassEncoder(), source, destination, regions, regionCount);
    }

    void MQueue::fillBuffer(BufferRange source, uint32_t data)
    {
        getOrCreateTopBlitPassEncoder()->fillBuffer(source, data);
    }


    void MQueue::submit(const eastl::vector<CommandEncoder> &encoders)
    {
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
        submitInternal(encoders);
        finalizePendingReadbacks();
        postSubmit();
    }

    /* void MQueue::submitAndWait(const eastl::vector<CommandEncoder> &encoders)
    {
        mWriteBufferPool->setBufferClearFrameInterval(0);
        submitInternal(encoders);
        if (!encoders.empty())
        {
            eastl::static_pointer_cast<MCommandEncoder>(encoders.back())->waitUntilCompleted();
        }
        else if (this->mWriteBufferCommandEncoder)
        {
            eastl::static_pointer_cast<MCommandEncoder>(this->mWriteBufferCommandEncoder)->waitUntilCompleted();
        }
        postSubmit();
    } */

    void MQueue::destroy()
    {
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
            if (!pending.stagingBuffer.isNull())
            {
                mDevice->freeBuffer(pending.stagingBuffer);
            }
        }
        mPendingBufferReadbacks.clear();
        mPendingTextureReadbacks.clear();
        for (auto &pool : mWriteBufferPools)
        {
            if (pool)
            {
                pool->destroy();
            }
        }
        mWriteBufferPools.clear();
        mWriteBufferCommandEncoder = nullptr;
        mComputeCopyCommandEncoder = nullptr;
        mComputeCopyPassEncoder = nullptr;
        mBufferCopyMultipleRegionExecutor = nullptr;
        mReadbackCommandEncoder = nullptr;
        mLastCommittedEncoder = nullptr;
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
        getOrCreateWriteBufferCommandEncoder();
        if (!mWriteBufferBlitPassEncoder)
        {
            mWriteBufferBlitPassEncoder = getOrCreateWriteBufferCommandEncoder()->beginBlitPass({});
        }
        return mWriteBufferBlitPassEncoder;
    }

    CommandEncoder MQueue::getOrCreateComputeCopyCommandEncoder()
    {
        if (!mComputeCopyCommandEncoder)
        {
            mComputeCopyCommandEncoder = createCommandEncoder();
        }
        return mComputeCopyCommandEncoder;
    }

    ComputePassEncoder MQueue::getOrCreateComputeCopyPassEncoder()
    {
        getOrCreateComputeCopyCommandEncoder();
        if (!mComputeCopyPassEncoder)
        {
            mComputeCopyPassEncoder = getOrCreateComputeCopyCommandEncoder()->beginComputePass({});
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

    void MQueue::submitInternal(const eastl::vector<CommandEncoder> &encoders)
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            QueueProbeCategory,
            "MQueue::submitInternal",
            "queue={} user_encoders={} has_write={} has_compute_copy={} has_readback={}",
            mQueueName.c_str(),
            encoders.size(),
            mWriteBufferCommandEncoder != nullptr,
            mComputeCopyCommandEncoder != nullptr,
            mReadbackCommandEncoder != nullptr);
        mLastCommittedEncoder = nullptr;

        if (mWriteBufferCommandEncoder != nullptr)
        {
            mWriteBufferBlitPassEncoder->end();
            mWriteBufferCommandEncoder->end();

            eastl::static_pointer_cast<MCommandEncoder>(mWriteBufferCommandEncoder)->getNativeCommandEncoder()->addCompletedHandler([](MTL::CommandBuffer *commandBuffer) {
                auto error = commandBuffer->error();
                if (error)
                {
                    throw std::runtime_error(std::string("Write buffer Error: ") + error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) + " " + error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
                }
            });

            auto *encoder = eastl::static_pointer_cast<MCommandEncoder>(mWriteBufferCommandEncoder).get();
            encoder->commit();
            mLastCommittedEncoder = encoder;
        }
        if (mComputeCopyCommandEncoder != nullptr)
        {
            mComputeCopyPassEncoder->end();
            mComputeCopyCommandEncoder->end();

            eastl::static_pointer_cast<MCommandEncoder>(mComputeCopyCommandEncoder)->getNativeCommandEncoder()->addCompletedHandler([](MTL::CommandBuffer *commandBuffer) {
                auto error = commandBuffer->error();
                if (error)
                {
                    throw std::runtime_error(std::string("compute copy buffer Error: ") + error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) + " " + error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
                }
            });

            auto *encoder = eastl::static_pointer_cast<MCommandEncoder>(mComputeCopyCommandEncoder).get();
            encoder->commit();
            mLastCommittedEncoder = encoder;
        }
        for (const auto &encoder : encoders)
        {

            eastl::static_pointer_cast<MCommandEncoder>(encoder)->getNativeCommandEncoder()->addCompletedHandler([](MTL::CommandBuffer *commandBuffer) {
                auto error = commandBuffer->error();
                if (error)
                {
                    throw std::runtime_error(std::string("common cmd Error: ") + error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) + " " + error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
                }
            });

            auto *metalEncoder = eastl::static_pointer_cast<MCommandEncoder>(encoder).get();
            metalEncoder->commit();
            mLastCommittedEncoder = metalEncoder;
        }

        if (mReadbackCommandEncoder != nullptr)
        {
            if (mReadbackBlitPassEncoder != nullptr)
            {
                mReadbackBlitPassEncoder->end();
            }
            mReadbackCommandEncoder->end();

            eastl::static_pointer_cast<MCommandEncoder>(mReadbackCommandEncoder)->getNativeCommandEncoder()->addCompletedHandler([](MTL::CommandBuffer *commandBuffer) {
                auto error = commandBuffer->error();
                if (error)
                {
                    throw std::runtime_error(std::string("readback cmd Error: ") + error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) + " " + error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
                }
            });

            auto *encoder = eastl::static_pointer_cast<MCommandEncoder>(mReadbackCommandEncoder).get();
            encoder->commit();
            mLastCommittedEncoder = encoder;
        }
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

        if (mLastCommittedEncoder != nullptr)
        {
            mLastCommittedEncoder->waitUntilCompleted();
            auto *commandBuffer = mLastCommittedEncoder->getNativeCommandEncoder();
            if (auto *error = commandBuffer->error())
            {
                throw std::runtime_error(
                    std::string("MQueue::finalizePendingReadbacks command buffer failed: ") +
                    error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) + " " +
                    error->debugDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
            }
        }

        for (auto &pending : mPendingBufferReadbacks)
        {
            Buffer sourceBuffer = pending.usesDirectMapping ? pending.source.buffer : pending.stagingBuffer;
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

            if (!pending.usesDirectMapping && !pending.stagingBuffer.isNull())
            {
                mDevice->freeBuffer(pending.stagingBuffer);
            }
        }
        mPendingBufferReadbacks.clear();

        for (auto &pending : mPendingTextureReadbacks)
        {
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
        // The readback command completed after every earlier submission on this queue.
        // Reuse upload storage here even when a headless client never presents a frame.
        for (auto &pool : mWriteBufferPools) { pool->reset(mNextSubmitSerial); }
    }

    void MQueue::prepareWriteBufferPool()
    {
        const uint64_t frameIndex = mDevice->getFrameIndex();
        if (mWriteBufferFrameIndex == frameIndex)
        {
            return;
        }
        mWriteBufferPools.at(frameIndex)->reset(mNextSubmitSerial);
        mWriteBufferFrameIndex = frameIndex;
    }

    void MQueue::postSubmit()
    {
        mWriteBufferCommandEncoder = nullptr;
        mWriteBufferBlitPassEncoder = nullptr;

        mComputeCopyCommandEncoder = nullptr;
        mComputeCopyPassEncoder = nullptr;

        mReadbackCommandEncoder = nullptr;
        mReadbackBlitPassEncoder = nullptr;
        mLastCommittedEncoder = nullptr;
        ++mNextSubmitSerial;
    }

} // namespace GVM::RHI::Metal
