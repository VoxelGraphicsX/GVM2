#include "MWriteBufferPool.hpp"
#include "MDevice.hpp"
#include "MTextureCopyUtils.hpp"
#include <EASTL/string.h>
#include <cstring>
#include <stdexcept>
namespace GVM::RHI::Metal
{

    MWriteBufferPool::MWriteBufferPool()
    {
    }

    void MWriteBufferBlock::init(MDevice *device, const eastl::string &queueName, uint64_t blockSerial, uint64_t size)
    {
        mDevice = device;
        eastl::string name = queueName + "_queue_WriteBufferBlock_Size_" + eastl::to_string(size) + "_Serial_" + eastl::to_string(blockSerial);
        mBuffer = device->createBuffer({.label = name.c_str(), .usage = BufferUsage::CopySrc | BufferUsage::CopyDst | BufferUsage::MapWrite, .size = size});
        mStorageSize = size;
        mOffsetInUse = 0;
        mLastUsedSubmitSerial = 0;
    }

    bool MWriteBufferBlock::hasSpaceFor(uint64_t size) const
    {
        return Detail::hasSpaceForWrite(mOffsetInUse, mStorageSize, size);
    }

    void MWriteBufferBlock::markBytesUsed(uint64_t size, uint64_t submitSerial)
    {
        const uint64_t alignedSize = Detail::alignStagingWriteSize(size);
        if (mOffsetInUse + alignedSize > mStorageSize)
        {
            throw std::out_of_range("MWriteBufferBlock::markBytesUsed exceeded the storage capacity of the staging block.");
        }
        mOffsetInUse += alignedSize;
        mLastUsedSubmitSerial = submitSerial;
    }

    Buffer MWriteBufferBlock::getBuffer() const
    {
        return mBuffer;
    }

    BufferRange MWriteBufferBlock::getBufferRange(uint64_t offset, uint64_t size) const
    {
        return BufferRange(mBuffer, offset, size);
    }

    uint64_t MWriteBufferBlock::getOffsetInUse() const
    {
        return mOffsetInUse;
    }

    void MWriteBufferBlock::resetUsage()
    {
        mOffsetInUse = 0;
    }

    bool MWriteBufferBlock::isExpired(uint64_t completedSubmitSerial, uint64_t retentionSubmitCount) const
    {
        return Detail::isWriteBufferBlockExpired(completedSubmitSerial, mLastUsedSubmitSerial, retentionSubmitCount);
    }

    void MWriteBufferBlock::destroy(MDevice *device)
    {
        if (!mBuffer.isNull())
        {
            device->freeBuffer(mBuffer);
            mBuffer.reset();
        }
        mStorageSize = 0;
        mOffsetInUse = 0;
        mLastUsedSubmitSerial = 0;
    }

    void MWriteBufferPool::init(MDevice *device, const eastl::string &queueName)
    {
        mDevice = device;
        mQueueName = queueName;
        mNextBlockSerial = 0;
        mRetentionSubmitCount = mDevice->MAX_FRAME_COUNT;
    }

    void MWriteBufferPool::writeBufferToCommand(BlitPassEncoder encoder, BufferRange buffer, void const *data, uint64_t size, uint64_t submitSerial)
    {
        const uint64_t blockStorageSize = Detail::selectWriteBufferBlockSize(size);
        auto targetBufferBlockPool = findOrCreateBufferBlockPool(blockStorageSize);
        auto targetBufferBlock = findOrCreateBufferBlock(targetBufferBlockPool, blockStorageSize, size);
        const uint64_t writeOffset = targetBufferBlock->getOffsetInUse();

        auto blockBuffer = targetBufferBlock->getBuffer();
        bool isMapped = false;
        try
        {
            blockBuffer->map();
            isMapped = true;
            auto blockBufferPointer = blockBuffer->getMappedRange(writeOffset, size);
            if (blockBufferPointer == nullptr)
            {
                throw std::runtime_error("MWriteBufferPool::writeBufferToCommand failed to map the staging block.");
            }
            memcpy(blockBufferPointer, data, size);
            blockBuffer->unmap();
            isMapped = false;
        }
        catch (...)
        {
            if (isMapped)
            {
                blockBuffer->unmap();
            }
            throw;
        }
        BufferRange bufferRangeFromCopy = buffer;
        bufferRangeFromCopy.size = size;
        encoder->copyBufferToBuffer(targetBufferBlock->getBufferRange(writeOffset, size), bufferRangeFromCopy);
        targetBufferBlock->markBytesUsed(size, submitSerial);
    }

    void MWriteBufferPool::writeTextureToCommand(BlitPassEncoder encoder, const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize, uint64_t submitSerial)
    {
        Detail::validateTextureDataRequest("MWriteBufferPool::writeTextureToCommand", destination.texture->getFormat(), dataLayout, writeSize, dataSize);
        const Detail::TextureCopyFootprint footprint = Detail::buildTextureCopyFootprint(destination.texture->getFormat(), dataLayout, writeSize, true);
        const uint64_t blockStorageSize = Detail::selectWriteBufferBlockSize(footprint.requiredBytes);

        auto targetBufferBlockPool = findOrCreateBufferBlockPool(blockStorageSize);
        auto targetBufferBlock = findOrCreateBufferBlock(targetBufferBlockPool, blockStorageSize, footprint.requiredBytes);
        const uint64_t writeOffset = targetBufferBlock->getOffsetInUse();
        auto blockBuffer = targetBufferBlock->getBuffer();
        bool isMapped = false;
        try
        {
            blockBuffer->map();
            isMapped = true;
            auto blockBufferPointer = static_cast<uint8_t *>(blockBuffer->getMappedRange(writeOffset, footprint.requiredBytes));
            if (blockBufferPointer == nullptr)
            {
                throw std::runtime_error("MWriteBufferPool::writeTextureToCommand failed to map the staging block for a texture upload.");
            }

            const auto *sourceBytes = static_cast<const uint8_t *>(data) + dataLayout.offset;
            if (footprint.logicalBytesPerRow == footprint.metalBytesPerRow && footprint.logicalBytesPerImage == footprint.metalBytesPerImage)
            {
                std::memcpy(blockBufferPointer, sourceBytes, footprint.requiredBytes);
            }
            else
            {
                Detail::copyTextureRows(
                    sourceBytes,
                    blockBufferPointer,
                    footprint.tightRowBytes,
                    footprint.logicalBytesPerRow,
                    footprint.logicalBytesPerImage,
                    footprint.metalBytesPerRow,
                    footprint.metalBytesPerImage,
                    footprint.blocksY,
                    writeSize.depth);
            }

            blockBuffer->unmap();
            isMapped = false;
        }
        catch (...)
        {
            if (isMapped)
            {
                blockBuffer->unmap();
            }
            throw;
        }
        ImageCopyBuffer source{};
        source.layout.offset = writeOffset;
        source.layout.bytesPerRow = static_cast<uint32_t>(footprint.metalBytesPerRow);
        source.layout.rowsPerImage = static_cast<uint32_t>(footprint.metalRowsPerImage);
        source.buffer = blockBuffer;
        encoder->copyBufferToTexture(source, destination, writeSize);
        targetBufferBlock->markBytesUsed(footprint.requiredBytes, submitSerial);
    }

    void MWriteBufferPool::setRetentionSubmitCount(uint64_t retentionSubmitCount)
    {
        this->mRetentionSubmitCount = retentionSubmitCount;
    }

    void MWriteBufferPool::reset(uint64_t completedSubmitSerial)
    {
        for (auto &[alignedSize, blockPool] : mBufferBlockPool)
        {
            for (auto &block : blockPool->blocks)
            {
                block->resetUsage();
            }
        }

        for (auto &[alignedSize, blockPool] : mBufferBlockPool)
        {
            for (auto it = blockPool->blocks.begin(); it != blockPool->blocks.end();)
            {
                if ((*it)->isExpired(completedSubmitSerial, mRetentionSubmitCount))
                {
                    (*it)->destroy(mDevice);
                    it = blockPool->blocks.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void MWriteBufferPool::destroy()
    {
        for (auto &[alignedSize, blockPool] : mBufferBlockPool)
        {
            for (auto &block : blockPool->blocks)
            {
                block->destroy(mDevice);
            }
        }
        mBufferBlockPool.clear();
        mNextBlockSerial = 0;
    }

    MWriteBufferBlockPoolPTR MWriteBufferPool::findOrCreateBufferBlockPool(uint64_t blockStorageSize)
    {
        auto it = mBufferBlockPool.find(blockStorageSize);
        if (it != mBufferBlockPool.end())
        {
            return it->second;
        }
        auto newBlockPool = eastl::make_shared<MWriteBufferBlockPool>();
        mBufferBlockPool.emplace(blockStorageSize, newBlockPool);
        return newBlockPool;
    }

    MWriteBufferBlockPTR MWriteBufferPool::findOrCreateBufferBlock(MWriteBufferBlockPoolPTR blockPoolPTR, uint64_t blockStorageSize, uint64_t requiredBytes)
    {
        for (auto &block : blockPoolPTR->blocks)
        {
            if (block->hasSpaceFor(requiredBytes))
            {
                return block;
            }
        }
        auto newBlock = eastl::make_shared<MWriteBufferBlock>();
        newBlock->init(mDevice, this->mQueueName, mNextBlockSerial++, blockStorageSize);
        blockPoolPTR->blocks.insert(newBlock);
        return newBlock;
    }

} // namespace GVM::RHI::Metal
