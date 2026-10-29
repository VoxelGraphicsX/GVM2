#include "MUploadAllocator.hpp"

#include "MBuffer.hpp"
#include "MDevice.hpp"
#include "MTextureCopyUtils.hpp"

#include <cstring>
#include <stdexcept>

namespace GVM::RHI::Metal
{
    MUploadAllocator::~MUploadAllocator() = default;

    void MUploadAllocator::init(MDevice *device, const eastl::string &queueName)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("MUploadAllocator::init requires a valid Metal device.");
        }
        mDevice = device;
        mQueueName = queueName;
        destroyPages(mPages.releaseAll());
    }

    MUploadPage MUploadAllocator::createPage(uint64_t size)
    {
        MUploadPage page = {};
        page.size = size;
        const eastl::string name = mQueueName + "_UploadPage_" + eastl::to_string(mNextPageIndex++);
        page.buffer = mDevice->createBuffer({
            .label = name.c_str(),
            .usage = BufferUsage::MapWrite | BufferUsage::CopySrc,
            .size = size,
        });
        if (page.buffer.isNull())
        {
            throw std::runtime_error("MUploadAllocator failed to create an upload staging buffer.");
        }
        page.mappedData = static_cast<uint8_t *>(static_cast<MBuffer *>(page.buffer.get())->getNativeBuffer()->contents());
        if (page.mappedData == nullptr)
        {
            mDevice->freeBuffer(page.buffer);
            page.buffer.reset();
            throw std::runtime_error("MUploadAllocator expected persistently mapped staging memory.");
        }
        return page;
    }

    Private::UploadSlice<MUploadPage> MUploadAllocator::allocateSlice(uint64_t size, uint64_t alignment)
    {
        eastl::optional<Private::UploadSlice<MUploadPage>> slice = mPages.tryAllocate(size, alignment);
        if (slice.has_value())
        {
            return *slice;
        }
        mPages.addPage(createPage(Private::UploadPageAllocator<MUploadPage>::pageSizeFor(size)));
        slice = mPages.tryAllocate(size, alignment);
        if (!slice.has_value())
        {
            throw std::runtime_error("MUploadAllocator failed to sub-allocate from a new upload page.");
        }
        return *slice;
    }

    void MUploadAllocator::writeBufferToCommand(BlitPassEncoder encoder, BufferRange buffer, void const *data, uint64_t size)
    {
        const Private::UploadSlice<MUploadPage> slice = allocateSlice(size, 256u);
        memcpy(slice.mappedData, data, size);
        encoder->copyBufferToBuffer(BufferRange(slice.page.buffer, slice.offset, size), BufferRange(buffer.buffer, buffer.offset, size));
    }

    void MUploadAllocator::writeTextureToCommand(
        BlitPassEncoder encoder,
        const ImageCopyTexture &destination,
        void const *data,
        const TextureDataLayout &dataLayout,
        const Extent3D &writeSize,
        const Private::TextureCopyFootprint &footprint)
    {
        const Private::UploadSlice<MUploadPage> slice = allocateSlice(footprint.stagingBytes, 256u);
        const auto *sourceBytes = static_cast<const uint8_t *>(data) + dataLayout.offset;
        if (footprint.bytesPerRow == footprint.stagingBytesPerRow && footprint.bytesPerImage == footprint.stagingBytesPerImage)
        {
            memcpy(slice.mappedData, sourceBytes, footprint.minimumDataBytes - dataLayout.offset);
        }
        else
        {
            Detail::copyTextureRows(
                sourceBytes,
                slice.mappedData,
                footprint.tightRowBytes,
                footprint.bytesPerRow,
                footprint.bytesPerImage,
                footprint.stagingBytesPerRow,
                footprint.stagingBytesPerImage,
                footprint.blocksY,
                writeSize.depth);
        }
        ImageCopyBuffer source{};
        source.layout.offset = slice.offset;
        source.layout.bytesPerRow = static_cast<uint32_t>(footprint.stagingBytesPerRow);
        source.layout.rowsPerImage = static_cast<uint32_t>(footprint.rowsPerImage);
        source.buffer = slice.page.buffer;
        encoder->copyBufferToTexture(source, destination, writeSize);
    }

    void MUploadAllocator::markSubmitted(uint64_t serial)
    {
        mPages.markSubmitted(serial);
    }

    void MUploadAllocator::reclaim(uint64_t retiredSerial)
    {
        destroyPages(mPages.reclaim(retiredSerial));
    }

    void MUploadAllocator::destroy()
    {
        destroyPages(mPages.releaseAll());
        mDevice = nullptr;
    }

    void MUploadAllocator::destroyPages(eastl::vector<MUploadPage> pages)
    {
        for (MUploadPage &page : pages)
        {
            if (mDevice != nullptr && !page.buffer.isNull())
            {
                mDevice->freeBuffer(page.buffer);
            }
            page.buffer.reset();
            page.mappedData = nullptr;
            page.size = 0;
        }
    }
} // namespace GVM::RHI::Metal
