#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>
#include <GVMRHI/Private/UploadPageAllocator.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::RHI::Metal
{
    class MDevice;

    /** One backend-owned upload staging page: a MapWrite|CopySrc buffer and its persistent contents pointer. */
    struct MUploadPage
    {
        Buffer buffer;
        uint8_t *mappedData = nullptr;
        uint64_t size = 0;
    };

    /**
     * Stages CPU uploads into Metal staging pages and records the matching blit copies.
     * Pages recycle once the last submission that read them retires on the GPU.
     */
    class MUploadAllocator final
    {
    public:
        MUploadAllocator() = default;
        ~MUploadAllocator();

        void init(MDevice *device, const eastl::string &queueName);

        /** Stages a buffer upload and records a blit copy into the destination range. */
        void writeBufferToCommand(BlitPassEncoder encoder, BufferRange buffer, void const *data, uint64_t size);

        /**
         * Stages a texture upload using the footprint already checked by the queue and records the blit copy.
         * dataLayout.offset bytes in `data` are skipped; staging uses the backend's 256-byte row alignment.
         */
        void writeTextureToCommand(
            BlitPassEncoder encoder,
            const ImageCopyTexture &destination,
            void const *data,
            const TextureDataLayout &dataLayout,
            const Extent3D &writeSize,
            const Private::TextureCopyFootprint &footprint);

        /** Stamps the pages written since the previous submit with the serial of the submission carrying their copies. */
        void markSubmitted(uint64_t serial);

        /** Destroys retired pages once the last submission that read them completed on the GPU. */
        void reclaim(uint64_t retiredSerial);

        /** Destroys every page while the queue tears down. */
        void destroy();

    private:
        /** Allocates one staging slice, creating a new page when no existing page fits the request. */
        Private::UploadSlice<MUploadPage> allocateSlice(uint64_t size, uint64_t alignment);

        /** Creates one staging page through the device. */
        MUploadPage createPage(uint64_t size);

        /** Destroys the pages the shared allocator handed back. */
        void destroyPages(eastl::vector<MUploadPage> pages);

        MDevice *mDevice = nullptr;
        eastl::string mQueueName;
        Private::UploadPageAllocator<MUploadPage> mPages;
        uint64_t mNextPageIndex = 0;
    };
} // namespace GVM::RHI::Metal
