#include "MBlitPassEncoder.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MTexture.hpp"
#include <GVMRHI/Private/BlitPassValidation.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>
#include <EASTL/algorithm.h>
#include <stdexcept>
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

        /** Returns the bytesPerImage argument Metal expects for one texture type: 0 for non-arrayed textures, the computed value otherwise. */
        uint64_t metalTextureBytesPerImageArgument(MTL::TextureType textureType, uint64_t bytesPerImage)
        {
            if (textureType == MTL::TextureType1D || textureType == MTL::TextureType2D || textureType == MTL::TextureType2DMultisample)
            {
                return 0u;
            }
            return bytesPerImage;
        }

        /** Validates that an ImageCopyTexture aspect matches its texture format and Metal's native blit contract. */
        void validateTextureCopyAspect(
            const char *apiName,
            TextureFormat format,
            TextureAspectFlags aspect,
            bool rejectPartialCombinedAspect)
        {
            const bool hasDepth = Private::isDepthFormat(format);
            const bool hasStencil = Private::hasStencilAspect(format);
            if (aspect != TextureAspect::All && aspect != TextureAspect::DepthOnly && aspect != TextureAspect::StencilOnly)
            {
                throw std::invalid_argument((eastl::string(apiName) + " received an unsupported TextureAspect mask.").c_str());
            }
            if (aspect == TextureAspect::DepthOnly && !hasDepth)
            {
                throw std::invalid_argument((eastl::string(apiName) + " DepthOnly aspect requires a depth-capable texture format.").c_str());
            }
            if (aspect == TextureAspect::StencilOnly && !hasStencil)
            {
                throw std::invalid_argument((eastl::string(apiName) + " StencilOnly aspect requires a stencil-capable texture format.").c_str());
            }
            if (rejectPartialCombinedAspect && hasDepth && hasStencil && aspect != TextureAspect::All)
            {
                throw std::invalid_argument((eastl::string(apiName) + " does not support partial-aspect copies on combined depth-stencil textures.").c_str());
            }
        }

        /** Validates one texture copy region against usage, aspect, mip levels, and subresource bounds. */
        void validateTextureCopyEndpoint(const MTexture &texture, const ImageCopyTexture &endpoint, const Extent3D &copySize, bool isDestination)
        {
            const TextureUsageFlags requiredUsage = isDestination ? TextureUsage::CopyDst : TextureUsage::CopySrc;
            if ((texture.getUsage() & requiredUsage) == 0u)
            {
                throw std::invalid_argument(isDestination
                    ? "MBlitPassEncoder::copyTextureToTexture destination texture must be created with TextureUsage::CopyDst."
                    : "MBlitPassEncoder::copyTextureToTexture source texture must be created with TextureUsage::CopySrc.");
            }
            validateTextureCopyAspect("MBlitPassEncoder::copyTextureToTexture", texture.getFormat(), endpoint.aspect, true);
            if (endpoint.mipLevel >= texture.getMipLevelCount())
            {
                throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture mip level exceeds the texture mip count.");
            }
            if (copySize.width == 0u || copySize.height == 0u || copySize.depth == 0u)
            {
                return;
            }
            const uint32_t mipWidth = eastl::max(1u, texture.getWidth() >> endpoint.mipLevel);
            const uint32_t mipHeight = texture.getDimension() == TextureDimension::e1D ? 1u : eastl::max(1u, texture.getHeight() >> endpoint.mipLevel);
            if (endpoint.origin.x > mipWidth || copySize.width > mipWidth - endpoint.origin.x ||
                endpoint.origin.y > mipHeight || copySize.height > mipHeight - endpoint.origin.y)
            {
                throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture copy extent exceeds the mip level bounds.");
            }
            if (texture.getDimension() == TextureDimension::e3D)
            {
                const uint32_t mipDepth = eastl::max(1u, texture.getDepth() >> endpoint.mipLevel);
                if (endpoint.origin.z > mipDepth || copySize.depth > mipDepth - endpoint.origin.z)
                {
                    throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture 3D depth extent exceeds the mip level bounds.");
                }
            }
            else
            {
                const uint32_t layers = texture.getArrayLayerCount();
                if (endpoint.origin.z > layers || copySize.depth > layers - endpoint.origin.z)
                {
                    throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture array layer range exceeds the texture layer count.");
                }
            }
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
        ensureOpen("MBlitPassEncoder::copyBufferToBuffer");
        if (source.buffer.isNull() || destination.buffer.isNull())
        {
            throw std::invalid_argument("Buffer copy requires source and destination resources.");
        }
        const uint64_t copySize = eastl::min(source.size, destination.size);
        if (copySize == 0u)
        {
            return;
        }
        auto *sourceBuffer = static_cast<MBuffer *>(source.buffer.get());
        auto *destinationBuffer = static_cast<MBuffer *>(destination.buffer.get());
        Private::validateTransferBufferRange("copyBufferToBuffer", sourceBuffer, sourceBuffer->getUsage(), source.offset, copySize, Private::TransferDirection::Source);
        Private::validateTransferBufferRange("copyBufferToBuffer", destinationBuffer, destinationBuffer->getUsage(), destination.offset, copySize, Private::TransferDirection::Destination);
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mNativeCommandEncoder->copyFromBuffer(sourceBuffer->getNativeBuffer(), source.offset, destinationBuffer->getNativeBuffer(), destination.offset, copySize);
    }

    void MBlitPassEncoder::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureOpen("MBlitPassEncoder::copyBufferToTexture");
        if (source.buffer.isNull() || destination.texture.isNull())
        {
            throw std::invalid_argument("Texture upload requires source and destination resources.");
        }
        validateTextureCopyAspect("MBlitPassEncoder::copyBufferToTexture", destination.texture->getFormat(), destination.aspect, false);
        const uint64_t requiredBytes = Private::computeTextureCopyFootprint(
            "MBlitPassEncoder::copyBufferToTexture", destination.texture->getFormat(), source.layout, copySize, 1u).minimumDataBytes;
        auto *sourceBufferImpl = static_cast<MBuffer *>(source.buffer.get());
        Private::validateTransferBufferRange("copyBufferToTexture", sourceBufferImpl, sourceBufferImpl->getUsage(), 0u, requiredBytes, Private::TransferDirection::Source);
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

            // ─── 核心修正结束 ──────────────────────────────

        if (texType == MTL::TextureType3D)
        {
            this->mNativeCommandEncoder->copyFromBuffer(sourceBuffer, source.layout.offset, sourceBytesPerRow, sourceBytesPerImage,
                MTL::Size::Make(copySize.width, copySize.height, copySize.depth), destinationTexture, destinationSlice, destination.mipLevel, origin);
        }
        else
        {
            // Non-3D textures address layers through the slice argument, so a multi-layer copy needs one call per slice.
            const uint64_t sourceBytesPerImageArgument = metalTextureBytesPerImageArgument(texType, sourceBytesPerImage);
            const MTL::Size sliceSize = MTL::Size::Make(copySize.width, copySize.height, 1u);
            for (uint32_t slice = 0u; slice < copySize.depth; ++slice)
            {
                this->mNativeCommandEncoder->copyFromBuffer(sourceBuffer, source.layout.offset + slice * sourceBytesPerImage,
                    sourceBytesPerRow, sourceBytesPerImageArgument, sliceSize, destinationTexture, destinationSlice + slice, destination.mipLevel, origin);
            }
        }
    }

    void MBlitPassEncoder::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        ensureOpen("MBlitPassEncoder::copyTextureToBuffer");
        if (source.texture.isNull() || destination.buffer.isNull())
        {
            throw std::invalid_argument("Texture readback requires source and destination resources.");
        }
        validateTextureCopyAspect("MBlitPassEncoder::copyTextureToBuffer", source.texture->getFormat(), source.aspect, false);
        const uint64_t requiredBytes = Private::computeTextureCopyFootprint(
            "MBlitPassEncoder::copyTextureToBuffer", source.texture->getFormat(), destination.layout, copySize, 1u).minimumDataBytes;
        auto *destinationBufferImpl = static_cast<MBuffer *>(destination.buffer.get());
        Private::validateTransferBufferRange("copyTextureToBuffer", destinationBufferImpl, destinationBufferImpl->getUsage(), 0u, requiredBytes, Private::TransferDirection::Destination);
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

        if (texType == MTL::TextureType3D)
        {
            this->mNativeCommandEncoder->copyFromTexture(
                sourceTexture,
                sourceSlice,
                source.mipLevel,
                origin,
                MTL::Size::Make(copySize.width, copySize.height, copySize.depth),
                destinationBuffer,
                destination.layout.offset,
                destinationBytesPerRow,
                destinationBytesPerImage);
        }
        else
        {
            // Non-3D textures address layers through the slice argument, so a multi-layer copy needs one call per slice.
            const uint64_t destinationBytesPerImageArgument = metalTextureBytesPerImageArgument(texType, destinationBytesPerImage);
            const MTL::Size sliceSize = MTL::Size::Make(copySize.width, copySize.height, 1u);
            for (uint32_t slice = 0u; slice < copySize.depth; ++slice)
            {
                this->mNativeCommandEncoder->copyFromTexture(
                    sourceTexture,
                    sourceSlice + slice,
                    source.mipLevel,
                    origin,
                    sliceSize,
                    destinationBuffer,
                    destination.layout.offset + slice * destinationBytesPerImage,
                    destinationBytesPerRow,
                    destinationBytesPerImageArgument);
            }
        }
    }

    /// Copies a subresource region from one Metal texture to another using native slice or 3D coordinates.
    void MBlitPassEncoder::copyTextureToTexture(const ImageCopyTexture &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureOpen("MBlitPassEncoder::copyTextureToTexture");
        if (source.texture.isNull() || destination.texture.isNull())
        {
            throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture requires source and destination textures.");
        }
        auto *sourceTexture = static_cast<MTexture *>(source.texture.get());
        auto *destinationTexture = static_cast<MTexture *>(destination.texture.get());
        validateTextureCopyEndpoint(*sourceTexture, source, copySize, false);
        validateTextureCopyEndpoint(*destinationTexture, destination, copySize, true);
        if (copySize.width == 0u || copySize.height == 0u || copySize.depth == 0u)
        {
            return;
        }
        if (sourceTexture == destinationTexture && source.mipLevel == destination.mipLevel)
        {
            const uint32_t srcZEnd = source.origin.z + copySize.depth;
            const uint32_t dstZEnd = destination.origin.z + copySize.depth;
            const bool zOverlaps = source.origin.z < dstZEnd && destination.origin.z < srcZEnd;
            if (zOverlaps)
            {
                throw std::invalid_argument("MBlitPassEncoder::copyTextureToTexture cannot copy overlapping subresources within the same texture.");
            }
        }
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        MTL::Texture *nativeSource = sourceTexture->getNativeTexture();
        MTL::Texture *nativeDestination = destinationTexture->getNativeTexture();
        const bool sourceIs3D = nativeSource->textureType() == MTL::TextureType3D;
        const bool destinationIs3D = nativeDestination->textureType() == MTL::TextureType3D;
        if (sourceIs3D && destinationIs3D)
        {
            this->mNativeCommandEncoder->copyFromTexture(
                nativeSource,
                0u,
                source.mipLevel,
                MTL::Origin::Make(source.origin.x, source.origin.y, source.origin.z),
                MTL::Size::Make(copySize.width, copySize.height, copySize.depth),
                nativeDestination,
                0u,
                destination.mipLevel,
                MTL::Origin::Make(destination.origin.x, destination.origin.y, destination.origin.z));
            return;
        }
        const MTL::Size sliceSize = MTL::Size::Make(copySize.width, copySize.height, 1u);
        for (uint32_t slice = 0u; slice < copySize.depth; ++slice)
        {
            const NS::UInteger sourceSlice = sourceIs3D ? 0u : static_cast<NS::UInteger>(source.origin.z + slice);
            const NS::UInteger sourceZ = sourceIs3D ? static_cast<NS::UInteger>(source.origin.z + slice) : 0u;
            const NS::UInteger destinationSlice = destinationIs3D ? 0u : static_cast<NS::UInteger>(destination.origin.z + slice);
            const NS::UInteger destinationZ = destinationIs3D ? static_cast<NS::UInteger>(destination.origin.z + slice) : 0u;
            this->mNativeCommandEncoder->copyFromTexture(
                nativeSource,
                sourceSlice,
                source.mipLevel,
                MTL::Origin::Make(source.origin.x, source.origin.y, sourceZ),
                sliceSize,
                nativeDestination,
                destinationSlice,
                destination.mipLevel,
                MTL::Origin::Make(destination.origin.x, destination.origin.y, destinationZ));
        }
    }

    /// Fills each byte in the range with the given value using Metal's native blit operation.
    void MBlitPassEncoder::fillBuffer(BufferRange source, uint8_t value)
    {
        ensureOpen("MBlitPassEncoder::fillBuffer");
        Private::validateFillBufferRange(source);
        auto *destinationBuffer = static_cast<MBuffer *>(source.buffer.get());
        Private::validateTransferBufferRange("fillBuffer", destinationBuffer, destinationBuffer->getUsage(), source.offset, source.size, Private::TransferDirection::Destination);
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        NS::Range range = NS::Range(source.offset, source.size);
        mNativeCommandEncoder->fillBuffer(destinationBuffer->getNativeBuffer(), range, value);
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

    void MBlitPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on an ended blit pass encoder.").c_str());
        }
    }

} // namespace GVM::RHI::Metal
