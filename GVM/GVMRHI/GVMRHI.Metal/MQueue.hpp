#pragma once
#include "MDefines.hpp"
#include "MUtilShaders/MBufferCopyMultipleRegionExecutor.hpp"
#include "MWriteBufferPool.hpp"
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MQueue final : public QueueImpl
    {
    public:
        MQueue();
        void init(MDevice *device, const eastl::string &queueName);
        virtual CommandEncoder createCommandEncoder() override;

        virtual void writeBuffer(BufferRange buffer, void const *data, uint64_t size) override;
        virtual void readBuffer(BufferRange buffer, void *data, uint64_t size) override;
        virtual void writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize) override;
        virtual void readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
        virtual void uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
        virtual void uploadTexture(Texture destination, GVM::RHI::BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes) override;
        virtual void copyBufferToBuffer(BufferRange source, BufferRange destination) override;
        virtual void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) override;
        virtual void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) override;
        virtual void copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount) override;
        virtual void fillBuffer(BufferRange source, uint32_t data) override;
        virtual void submit(const eastl::vector<CommandEncoder> &encoders) override;
        virtual void destroy() override;
        MTL::CommandQueue *getNativeQueue() const;

    private:
        eastl::vector<eastl::shared_ptr<MWriteBufferPool>> mWriteBufferPools;
        eastl::string mQueueName;

        MTL::CommandQueue *mNativeQueue = nullptr;
        CommandEncoder mWriteBufferCommandEncoder;
        BlitPassEncoder mWriteBufferBlitPassEncoder;


        CommandEncoder mComputeCopyCommandEncoder;
        ComputePassEncoder mComputeCopyPassEncoder;
        MBufferCopyMultipleRegionExecutor mBufferCopyMultipleRegionExecutor = nullptr;

        CommandEncoder mReadbackCommandEncoder;
        BlitPassEncoder mReadbackBlitPassEncoder;

        MDevice *mDevice = nullptr;
        CommandEncoder getOrCreateWriteBufferCommandEncoder();
        BlitPassEncoder getOrCreateTopBlitPassEncoder();

        CommandEncoder getOrCreateComputeCopyCommandEncoder();
        ComputePassEncoder getOrCreateComputeCopyPassEncoder();

        CommandEncoder getOrCreateReadbackCommandEncoder();
        BlitPassEncoder getOrCreateReadbackBlitPassEncoder();

        struct PendingBufferReadback
        {
            BufferRange source;
            Buffer stagingBuffer;
            void *destination = nullptr;
            uint64_t size = 0;
            bool usesDirectMapping = false;
        };

        struct PendingTextureReadback
        {
            Buffer stagingBuffer;
            void *destination = nullptr;
            uint64_t tightRowBytes = 0;
            uint64_t stagingBytesPerRow = 0;
            uint64_t stagingBytesPerImage = 0;
            uint64_t destinationBytesPerRow = 0;
            uint64_t destinationBytesPerImage = 0;
            uint32_t rowCount = 0;
            uint32_t depth = 1;
        };

        eastl::vector<PendingBufferReadback> mPendingBufferReadbacks;
        eastl::vector<PendingTextureReadback> mPendingTextureReadbacks;
        uint64_t mReadbackSerial = 0;
        uint64_t mNextSubmitSerial = 1;
        uint64_t mWriteBufferFrameIndex = ~uint64_t(0);
        MCommandEncoder *mLastCommittedEncoder = nullptr;

        /** Resets a staging pool only when its frame-in-flight slot is reused. */
        void prepareWriteBufferPool();
        void submitInternal(const eastl::vector<CommandEncoder> &encoders);
        void finalizePendingReadbacks();
        void postSubmit();
    };

} // namespace GVM::RHI::Metal
