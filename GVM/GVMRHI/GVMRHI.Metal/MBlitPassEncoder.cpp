#include "MBlitPassEncoder.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MTexture.hpp"
namespace GVM::RHI::Metal
{
    namespace
    {
        void configureBlitTimestampWrites(MTL::BlitPassDescriptor *nativeDescriptor, const PassTimestampWrites &timestampWrites)
        {
            MQuerySet *querySet = validateMetalTimestampWrites("MBlitPassEncoder::init", timestampWrites);
            if (querySet == nullptr)
            {
                return;
            }
            auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(0);
            attachment->setSampleBuffer(querySet->getNativeCounterSampleBuffer());
            attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(timestampWrites.beginningOfPassWriteIndex));
            attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(timestampWrites.endOfPassWriteIndex));
        }

        void configureBlitPassCounterWrites(MTL::BlitPassDescriptor *nativeDescriptor, const PassCounterWrites &counterWrites)
        {
            MQuerySet *stageQuerySet = validateMetalPassCounterWrites(
                "MBlitPassEncoder::init",
                QueryType::PassCounterStageUtilization,
                counterWrites.stageUtilizationQuerySet,
                counterWrites.stageUtilizationBeginIndex,
                counterWrites.stageUtilizationEndIndex);
            if (stageQuerySet != nullptr)
            {
                auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(1);
                attachment->setSampleBuffer(stageQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationBeginIndex));
                attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationEndIndex));
            }

            MQuerySet *statisticQuerySet = validateMetalPassCounterWrites(
                "MBlitPassEncoder::init",
                QueryType::PassCounterStatistic,
                counterWrites.statisticQuerySet,
                counterWrites.statisticBeginIndex,
                counterWrites.statisticEndIndex);
            if (statisticQuerySet != nullptr)
            {
                auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(2);
                attachment->setSampleBuffer(statisticQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticBeginIndex));
                attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticEndIndex));
            }
        }

        bool hasBlitPassCounterWrites(const PassCounterWrites &counterWrites)
        {
            return counterWrites.stageUtilizationQuerySet != nullptr || counterWrites.statisticQuerySet != nullptr;
        }
    } // namespace

    MBlitPassEncoder::MBlitPassEncoder()
    {
    }

    MBlitPassEncoder::~MBlitPassEncoder()
    {
        if (mNativeCommandEncoder != nullptr) { mNativeCommandEncoder->release(); }
    }

    void MBlitPassEncoder::init(MDevice *device, MCommandEncoder *commandEncoder, const BlitPassDescriptor &descriptor)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mDevice = device;
        this->mWeakCommandEncoder = commandEncoder;

        if (descriptor.timestampWrites.querySet == nullptr && !hasBlitPassCounterWrites(descriptor.counterWrites))
        {
            this->mNativeCommandEncoder = commandEncoder->getNativeCommandEncoder()->blitCommandEncoder();
        }
        else
        {
            auto *blitPassDescriptor = MTL::BlitPassDescriptor::alloc()->init();
            configureBlitTimestampWrites(blitPassDescriptor, descriptor.timestampWrites);
            configureBlitPassCounterWrites(blitPassDescriptor, descriptor.counterWrites);
            if (descriptor.timestampWrites.querySet != nullptr)
            {
                commandEncoder->retainQuerySet(descriptor.timestampWrites.querySet);
            }
            if (descriptor.counterWrites.stageUtilizationQuerySet != nullptr)
            {
                commandEncoder->retainQuerySet(descriptor.counterWrites.stageUtilizationQuerySet);
            }
            if (descriptor.counterWrites.statisticQuerySet != nullptr)
            {
                commandEncoder->retainQuerySet(descriptor.counterWrites.statisticQuerySet);
            }
            this->mNativeCommandEncoder = commandEncoder->getNativeCommandEncoder()->blitCommandEncoder(blitPassDescriptor);
            blitPassDescriptor->release();
        }

        this->mLabelName = descriptor.label;
        this->mNativeCommandEncoder->retain();
        if (descriptor.label.empty() == false)
        {
            this->mNativeCommandEncoder->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
        }
    }

    void MBlitPassEncoder::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mNativeCommandEncoder->copyFromBuffer(static_cast<MBuffer *>(source.buffer.get())->getNativeBuffer(), source.offset, static_cast<MBuffer *>(destination.buffer.get())->getNativeBuffer(), destination.offset, destination.size);
    }


    void MBlitPassEncoder::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        auto sourceBuffer = static_cast<MBuffer *>(source.buffer.get())->getNativeBuffer();
        auto destinationTexture = static_cast<MTexture *>(destination.texture.get())->getNativeTexture();
        const Private::BlockInfo blockInfo = Private::getTextureBlockInfo(destination.texture->getFormat());
        const uint64_t tightBytesPerRow = ((copySize.width + blockInfo.width - 1u) / blockInfo.width) * blockInfo.bytes;
        const uint64_t tightRowsPerImage = (copySize.height + blockInfo.height - 1u) / blockInfo.height;

        uint64_t sourceBytesPerRow = source.layout.bytesPerRow != 0 ? source.layout.bytesPerRow : tightBytesPerRow;
        uint64_t sourceBytesPerImage = source.layout.rowsPerImage != 0 ? source.layout.rowsPerImage * sourceBytesPerRow : tightRowsPerImage * sourceBytesPerRow;

        //MTL::Origin origin = {destination.origin.x, destination.origin.y, destination.origin.z};
        //MTL::Size size = {copySize.width, copySize.height, copySize.depth};
        // ─── 核心修正开始 ──────────────────────────────

            MTL::TextureType texType = destinationTexture->textureType();

            NS::UInteger destinationSlice = 0;
            NS::UInteger destinationZ = 0;

            // 判断是 3D 纹理还是 数组纹理
            if (texType == MTL::TextureType3D) {
                // 3D 纹理：Z 轴偏移放在 Origin.z，Slice 固定为 0
                destinationSlice = 0;
                destinationZ = destination.origin.z;
            } else {
                // 2DArray, Cube, CubeArray 等：Z 轴偏移放在 Slice 参数中，Origin.z 固定为 0
                // WebGPU 的 origin.z 对应 Metal 的 slice index
                destinationSlice = destination.origin.z;
                destinationZ = 0;
            }

            MTL::Origin origin = MTL::Origin::Make(destination.origin.x, destination.origin.y, destinationZ);
            MTL::Size size = MTL::Size::Make(copySize.width, copySize.height, copySize.depth);

            // ─── 核心修正结束 ──────────────────────────────

        this->mNativeCommandEncoder->copyFromBuffer(sourceBuffer, source.layout.offset, sourceBytesPerRow, sourceBytesPerImage, size, destinationTexture, destinationSlice, destination.mipLevel, origin);
    }

    void MBlitPassEncoder::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        auto sourceTexture = static_cast<MTexture *>(source.texture.get())->getNativeTexture();
        auto destinationBuffer = static_cast<MBuffer *>(destination.buffer.get())->getNativeBuffer();
        const Private::BlockInfo blockInfo = Private::getTextureBlockInfo(source.texture->getFormat());
        const uint64_t tightBytesPerRow = ((copySize.width + blockInfo.width - 1u) / blockInfo.width) * blockInfo.bytes;
        const uint64_t tightRowsPerImage = (copySize.height + blockInfo.height - 1u) / blockInfo.height;

        uint64_t destinationBytesPerRow = destination.layout.bytesPerRow != 0 ? destination.layout.bytesPerRow : tightBytesPerRow;
        uint64_t destinationBytesPerImage = destination.layout.rowsPerImage != 0 ? destination.layout.rowsPerImage * destinationBytesPerRow : tightRowsPerImage * destinationBytesPerRow;

        MTL::TextureType texType = sourceTexture->textureType();

        NS::UInteger sourceSlice = 0;
        NS::UInteger sourceZ = 0;
        if (texType == MTL::TextureType3D)
        {
            sourceSlice = 0;
            sourceZ = source.origin.z;
        }
        else
        {
            sourceSlice = source.origin.z;
            sourceZ = 0;
        }

        MTL::Origin origin = MTL::Origin::Make(source.origin.x, source.origin.y, sourceZ);
        MTL::Size size = MTL::Size::Make(copySize.width, copySize.height, copySize.depth);

        this->mNativeCommandEncoder->copyFromTexture(
            sourceTexture,
            sourceSlice,
            source.mipLevel,
            origin,
            size,
            destinationBuffer,
            destination.layout.offset,
            destinationBytesPerRow,
            destinationBytesPerImage);
    }

    void MBlitPassEncoder::fillBuffer(BufferRange source, uint32_t data)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        NS::Range range = NS::Range(source.offset, source.size);
        this->mNativeCommandEncoder->fillBuffer(static_cast<MBuffer *>(source.buffer.get())->getNativeBuffer(), range, data);
    }

    void MBlitPassEncoder::end()
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        if (mEnded)
        {
            return;
        }
        this->mNativeCommandEncoder->endEncoding();
        if (mWeakCommandEncoder != nullptr)
        {
            mWeakCommandEncoder->notifyPassEnded();
        }
        mEnded = true;
    }

    MTL::BlitCommandEncoder *MBlitPassEncoder::getNativeCommandEncoder() const
    {
        return this->mNativeCommandEncoder;
    }

} // namespace GVM::RHI::Metal
