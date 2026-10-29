#pragma once
#include "MDefines.hpp"
#include "MReadback.hpp"
#include "MUploadAllocator.hpp"
#include <GVMRHI/Private/BufferCopyMultipleRegionExecutor.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    /** Retains a committed Metal command buffer together with the queue role that produced it, so its completion status can be reported on the submitting thread instead of a Metal completion thread. */
    struct MCommittedCommandBuffer
    {
        NS::SharedPtr<MTL::CommandBuffer> commandBuffer;
        const char *role = "";
        uint64_t submitSerial = 0;
    };

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
        virtual void fillBuffer(BufferRange source, uint8_t value) override;
        /// Queues a buffer readback executed by the next submission; the Readback becomes ready once that submission completes.
        virtual Readback readBufferAsync(BufferRange source) override;
        /// Queues a texture readback executed by the next submission; the Readback becomes ready once that submission completes.
        virtual Readback readTextureAsync(const ImageCopyTexture &source, const TextureDataLayout &dataLayout, const Extent3D &readSize) override;
        virtual void submit(const eastl::vector<CommandEncoder> &encoders) override;
        virtual void destroy() override;
        MTL::CommandQueue *getNativeQueue() const;

    private:
        MUploadAllocator mUploadAllocator;
        eastl::string mQueueName;

        MTL::CommandQueue *mNativeQueue = nullptr;
        CommandEncoder mWriteBufferCommandEncoder;
        BlitPassEncoder mWriteBufferBlitPassEncoder;


        ComputePassEncoder mComputeCopyPassEncoder;
        Private::BufferCopyMultipleRegionExecutor mBufferCopyMultipleRegionExecutor;

        CommandEncoder mReadbackCommandEncoder;
        BlitPassEncoder mReadbackBlitPassEncoder;

        MDevice *mDevice = nullptr;
    public:
        /** Returns the owning Metal device. */
        [[nodiscard]] MDevice *getDevice() const { return mDevice; }
    private:
        CommandEncoder getOrCreateWriteBufferCommandEncoder();
        BlitPassEncoder getOrCreateTopBlitPassEncoder();

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
        eastl::vector<MReadback> mPendingAsyncReadbacks;
        uint64_t mReadbackSerial = 0;
        uint64_t mNextSubmitSerial = 1;
        uint64_t mLastCommittedSubmitSerial = 0;
        uint64_t mRetiredSubmitSerial = 0;
        MCommandEncoder *mLastCommittedEncoder = nullptr;
        eastl::vector<eastl::unique_ptr<MCommittedCommandBuffer>> mCommittedCommandBuffers;

        /** Retains the encoder's native command buffer under the given role and submit serial so its completion status can be inspected later on the submitting thread. */
        void trackCommittedCommandBuffer(MCommandEncoder &encoder, const char *role, uint64_t submitSerial);
        /**
         * Removes tracked command buffers that reached a final status, advances mRetiredSubmitSerial to one
         * below the smallest serial still pending (or the last committed serial when none remain), reclaims
         * retired upload pages, then throws one runtime_error when any buffer completed with an error.
         */
        void pollCommittedCommandBuffers();
        /**
         * Commits one encoder during submitInternal: ends it first only when endEncoder is set, then
         * tracks its command buffer under role, commits it, and records it as the last committed encoder.
         */
        void commitCommandEncoder(const CommandEncoder &encoder, const char *role, bool endEncoder);
        void submitInternal(const eastl::vector<CommandEncoder> &encoders);
        void finalizePendingReadbacks();
        void postSubmit();
    };

} // namespace GVM::RHI::Metal
