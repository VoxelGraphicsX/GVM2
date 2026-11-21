#include "VKBlitPassEncoder.hpp"
#include "VKInstance.hpp"

#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <GVMRHI/Private/BlitPassValidation.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>

#include <EASTL/algorithm.h>

#include <stdexcept>
#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
        namespace
    {
        constexpr eastl::string_view BlitPassEncoderLogCategory = "gvmrhi.vulkan.blit_pass_encoder";
    } // namespace

namespace
    {
        /** Applies the shared blit buffer-range validation to a Vulkan buffer using its declared usage. */
        void validateTransferBuffer(
            const char *apiName,
            const VKBuffer *buffer,
            uint64_t offset,
            uint64_t size,
            Private::TransferDirection direction)
        {
            Private::validateTransferBufferRange(apiName, buffer, buffer != nullptr ? buffer->getUsage() : BufferUsage::None, offset, size, direction);
        }

        void recordTextureMipChainCopy(
            vk::CommandBuffer commandBuffer,
            VKTaskDependencyResolver &stateTracker,
            vk::Buffer sourceBuffer,
            uint64_t sourceOffset,
            VKTexture *destinationTexture,
            const eastl::vector<uint64_t> &mipmapOffsetBytes)
        {
            eastl::vector<vk::BufferImageCopy> regions;
            regions.reserve(destinationTexture->getMipLevelCount());

            for (uint32_t mipLevel = 0; mipLevel < destinationTexture->getMipLevelCount(); ++mipLevel)
            {
                const uint32_t width = eastl::max(1u, destinationTexture->getWidth() >> mipLevel);
                const uint32_t height = eastl::max(1u, destinationTexture->getHeight() >> mipLevel);
                const uint32_t depth = destinationTexture->is3D()
                    ? eastl::max(1u, destinationTexture->getDepth() >> mipLevel)
                    : 1u;

                vk::BufferImageCopy region = {};
                region.bufferOffset = sourceOffset + mipmapOffsetBytes[mipLevel];
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = resolveTextureAspect(destinationTexture->getFormat(), TextureAspect::All);
                region.imageSubresource.mipLevel = mipLevel;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = destinationTexture->is3D() ? 1u : destinationTexture->getArrayLayerCount();
                region.imageOffset = vk::Offset3D{0, 0, 0};
                region.imageExtent = vk::Extent3D{width, height, depth};
                regions.push_back(region);
            }

            stateTracker.synchronizeTextureSubresources(commandBuffer, *destinationTexture, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
            eastl::vector<vk::BufferImageCopy> nativeRegions(regions.begin(), regions.end());
            commandBuffer.copyBufferToImage(
                sourceBuffer,
                destinationTexture->getNativeImage(),
                vk::ImageLayout::eTransferDstOptimal,
                nativeRegions);
        }

        /** Validates one endpoint of a texture-to-texture copy against usage, mip count, and subresource bounds. */
        void validateTextureToTextureEndpoint(const VKTexture &texture, const ImageCopyTexture &endpoint, const Extent3D &extent, bool isDestination)
        {
            const TextureUsageFlags requiredUsage = isDestination ? TextureUsage::CopyDst : TextureUsage::CopySrc;
            if ((texture.getUsage() & requiredUsage) == 0u)
            {
                throw std::invalid_argument(isDestination
                    ? "VKBlitPassEncoder::copyTextureToTexture destination texture must be created with TextureUsage::CopyDst."
                    : "VKBlitPassEncoder::copyTextureToTexture source texture must be created with TextureUsage::CopySrc.");
            }
            if (endpoint.mipLevel >= texture.getMipLevelCount())
            {
                throw std::invalid_argument("VKBlitPassEncoder::copyTextureToTexture mip level exceeds the texture mip count.");
            }
            if (extent.width == 0u || extent.height == 0u || extent.depth == 0u)
            {
                return;
            }
            const uint32_t mipWidth = eastl::max(1u, texture.getWidth() >> endpoint.mipLevel);
            const uint32_t mipHeight = texture.getDimension() == TextureDimension::e1D ? 1u : eastl::max(1u, texture.getHeight() >> endpoint.mipLevel);
            if (endpoint.origin.x > mipWidth || extent.width > mipWidth - endpoint.origin.x ||
                endpoint.origin.y > mipHeight || extent.height > mipHeight - endpoint.origin.y)
            {
                throw std::invalid_argument("VKBlitPassEncoder::copyTextureToTexture copy extent exceeds the mip level bounds.");
            }
            if (texture.is3D())
            {
                const uint32_t mipDepth = eastl::max(1u, texture.getDepth() >> endpoint.mipLevel);
                if (endpoint.origin.z > mipDepth || extent.depth > mipDepth - endpoint.origin.z)
                {
                    throw std::invalid_argument("VKBlitPassEncoder::copyTextureToTexture 3D depth extent exceeds the mip level bounds.");
                }
            }
            else
            {
                const uint32_t layers = texture.getCachedArrayLayerCount();
                if (endpoint.origin.z > layers || extent.depth > layers - endpoint.origin.z)
                {
                    throw std::invalid_argument("VKBlitPassEncoder::copyTextureToTexture array layer range exceeds the texture layer count.");
                }
            }
        }
    } // namespace

    void VKBlitPassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const BlitPassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
            throw std::invalid_argument("Vulkan blit pass requires a device and command encoder.");
        mDevice = device;
        mRecordingOwner = commandEncoder;
        mCommandEncoder = static_cast<VKCommandEncoder *>(commandEncoder.get());
        mDescriptor = descriptor;
        mTimestampWrites = descriptor.timestampWrites;
        mLabelName = descriptor.label;
        mCommandEncoder->writePassTimestamp(mTimestampWrites, true, vk::PipelineStageFlagBits2::eTopOfPipe);
    }

    void VKBlitPassEncoder::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        ensureOpen("copyBufferToBuffer");
        if (source.buffer.isNull() || destination.buffer.isNull())
            throw std::invalid_argument("Buffer copy requires source and destination resources.");
        destination.size = eastl::min(source.size, destination.size);
        if (destination.size == 0) return;
        auto &src = *static_cast<VKBuffer *>(source.buffer.get());
        auto &dst = *static_cast<VKBuffer *>(destination.buffer.get());
        validateTransferBuffer("copyBufferToBuffer", &src, source.offset, destination.size, Private::TransferDirection::Source);
        validateTransferBuffer("copyBufferToBuffer", &dst, destination.offset, destination.size, Private::TransferDirection::Destination);
        mCommandEncoder->retainBuffer(source.buffer);
        mCommandEncoder->retainBuffer(destination.buffer);
        mCommands.push_back(BufferCopyCommand{source.buffer, src.getNativeBuffer(), source.offset, destination});
    }

    void VKBlitPassEncoder::copyNativeBufferToBuffer(vk::Buffer sourceBuffer, uint64_t sourceOffset, BufferRange destination)
    {
        ensureOpen("copyNativeBufferToBuffer");
        if (!sourceBuffer || destination.buffer.isNull())
            throw std::invalid_argument("Upload copy requires source and destination buffers.");
        if (destination.size == 0) return;
        validateTransferBuffer("copyNativeBufferToBuffer", static_cast<VKBuffer *>(destination.buffer.get()), destination.offset, destination.size, Private::TransferDirection::Destination);
        mCommandEncoder->retainBuffer(destination.buffer);
        mCommands.push_back(BufferCopyCommand{{}, sourceBuffer, sourceOffset, destination});
    }

    void VKBlitPassEncoder::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &extent)
    {
        if (source.buffer.isNull() || destination.texture.isNull())
            throw std::invalid_argument("Texture upload requires source and destination resources.");
        copyBufferToTexture(source.buffer, static_cast<VKBuffer *>(source.buffer.get())->getNativeBuffer(), 0,
            Private::computeTextureCopyFootprint(
                "VKBlitPassEncoder::copyBufferToTexture", destination.texture->getFormat(), source.layout, extent, 1u).minimumDataBytes,
            source.layout, destination, extent);
    }

    void VKBlitPassEncoder::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &extent)
    {
        if (source.texture.isNull() || destination.buffer.isNull())
            throw std::invalid_argument("Texture readback requires source and destination resources.");
        copyTextureToBuffer(source, destination.buffer, static_cast<VKBuffer *>(destination.buffer.get())->getNativeBuffer(), 0,
            Private::computeTextureCopyFootprint(
                "VKBlitPassEncoder::copyTextureToBuffer", source.texture->getFormat(), destination.layout, extent, 1u).minimumDataBytes,
            destination.layout, extent);
    }

    void VKBlitPassEncoder::copyBufferToTexture(Buffer source, vk::Buffer nativeSourceBuffer, uint64_t sourceOffset, uint64_t sourceSize,
        const TextureDataLayout &sourceLayout, const ImageCopyTexture &destination, const Extent3D &extent)
    {
        ensureOpen("copyBufferToTexture");
        if (!nativeSourceBuffer || destination.texture.isNull())
        {
            throw std::invalid_argument("Texture upload requires a buffer and texture.");
        }
        validateTextureTransfer(destination, extent);
        if (!source.isNull())
        {
            validateTransferBuffer("copyBufferToTexture", static_cast<VKBuffer *>(source.get()), sourceOffset, sourceSize, Private::TransferDirection::Source);
            mCommandEncoder->retainBuffer(source);
        }
        mCommandEncoder->retainTexture(destination.texture);
        mCommands.push_back(TextureCopyCommand{source, nativeSourceBuffer, sourceOffset, sourceSize, sourceLayout, destination, extent, true});
    }

    void VKBlitPassEncoder::copyTextureToBuffer(const ImageCopyTexture &source, Buffer destination, vk::Buffer nativeDestinationBuffer,
        uint64_t destinationOffset, uint64_t destinationSize, const TextureDataLayout &destinationLayout, const Extent3D &extent)
    {
        ensureOpen("copyTextureToBuffer");
        if (source.texture.isNull() || !nativeDestinationBuffer)
        {
            throw std::invalid_argument("Texture readback requires a texture and buffer.");
        }
        validateTextureTransfer(source, extent);
        if (!destination.isNull())
        {
            validateTransferBuffer("copyTextureToBuffer", static_cast<VKBuffer *>(destination.get()), destinationOffset, destinationSize, Private::TransferDirection::Destination);
            mCommandEncoder->retainBuffer(destination);
        }
        mCommandEncoder->retainTexture(source.texture);
        mCommands.push_back(TextureCopyCommand{destination, nativeDestinationBuffer, destinationOffset, destinationSize, destinationLayout, source, extent, false});
    }

    void VKBlitPassEncoder::copyBufferToTextureMipChain(BufferRange source, Texture destination, const eastl::vector<uint64_t> &offsets)
    {
        if (source.buffer.isNull()) throw std::invalid_argument("Mip copy requires a source buffer.");
        copyBufferToTextureMipChain(source.buffer, static_cast<VKBuffer *>(source.buffer.get())->getNativeBuffer(), source.offset, source.size, destination, offsets);
    }

    void VKBlitPassEncoder::copyBufferToTextureMipChain(Buffer source, vk::Buffer nativeSourceBuffer, uint64_t sourceOffset,
        uint64_t sourceSize, Texture destination, const eastl::vector<uint64_t> &offsets)
    {
        ensureOpen("copyBufferToTextureMipChain");
        if (!nativeSourceBuffer || destination.isNull() || offsets.size() < static_cast<VKTexture *>(destination.get())->getMipLevelCount())
            throw std::invalid_argument("Mip copy requires a valid buffer, texture and one offset per mip.");
        if (!source.isNull())
        {
            validateTransferBuffer("copyBufferToTextureMipChain", static_cast<VKBuffer *>(source.get()), sourceOffset, sourceSize, Private::TransferDirection::Source);
            mCommandEncoder->retainBuffer(source);
        }
        mCommandEncoder->retainTexture(destination);
        mCommands.push_back(MipCopyCommand{source, nativeSourceBuffer, sourceOffset, sourceSize, destination, offsets});
    }

    void VKBlitPassEncoder::copyTextureToTexture(const ImageCopyTexture &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureOpen("copyTextureToTexture");
        if (source.texture.isNull() || destination.texture.isNull())
        {
            throw std::invalid_argument("Texture-to-texture copy requires source and destination textures.");
        }
        const auto &src = *static_cast<const VKTexture *>(source.texture.get());
        const auto &dst = *static_cast<const VKTexture *>(destination.texture.get());
        validateTextureToTextureEndpoint(src, source, copySize, false);
        validateTextureToTextureEndpoint(dst, destination, copySize, true);
        if (copySize.width == 0u || copySize.height == 0u || copySize.depth == 0u)
        {
            return;
        }
        if (&src == &dst && source.mipLevel == destination.mipLevel)
        {
            const vk::ImageAspectFlags srcAspect = resolveTextureAspect(src.getFormat(), source.aspect);
            const vk::ImageAspectFlags dstAspect = resolveTextureAspect(dst.getFormat(), destination.aspect);
            const uint32_t srcZEnd = source.origin.z + copySize.depth;
            const uint32_t dstZEnd = destination.origin.z + copySize.depth;
            if ((srcAspect & dstAspect) != vk::ImageAspectFlags{} &&
                source.origin.z < dstZEnd && destination.origin.z < srcZEnd)
            {
                throw std::invalid_argument("Texture-to-texture copy cannot overlap subresources within the same texture.");
            }
        }
        validateTextureTransfer(source, copySize);
        validateTextureTransfer(destination, copySize);
        mCommandEncoder->retainTexture(source.texture);
        mCommandEncoder->retainTexture(destination.texture);
        mCommands.push_back(TextureToTextureCopyCommand{source, destination, copySize});
    }

    void VKBlitPassEncoder::fillBuffer(BufferRange destination, uint8_t value)
    {
        ensureOpen("fillBuffer");
        Private::validateFillBufferRange(destination);
        validateTransferBuffer("fillBuffer", static_cast<VKBuffer *>(destination.buffer.get()), destination.offset, destination.size, Private::TransferDirection::Destination);
        mCommandEncoder->retainBuffer(destination.buffer);
        mCommands.push_back(FillCommand{destination, value});
    }

    void VKBlitPassEncoder::encode(VKCommandEncoder &owner)
    {
        const auto cb = owner.getNativeCommandBuffer();
        const char *debugPassLabel = mLabelName.empty() ? "BlitPass" : mLabelName.c_str();
        if (mDevice->getInstance()->supportsDebugUtils())
        {
            VkDebugUtilsLabelEXT label = {};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = debugPassLabel;
            vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cb), &label);
        }
        auto &states = owner.getTaskDependencyResolver();
        for (const auto &command : mCommands)
        {
            if (const auto *copy = eastl::get_if<BufferCopyCommand>(&command))
            {
                auto &dst = *static_cast<VKBuffer *>(copy->destination.buffer.get());
                if (!copy->source.isNull())
                {
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->source.get()), copy->sourceOffset, copy->destination.size,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
                }
                states.synchronizeBufferRange(cb, dst, copy->destination.offset, copy->destination.size,
                    vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
                cb.copyBuffer(copy->nativeSource, dst.getNativeBuffer(), vk::BufferCopy{copy->sourceOffset, copy->destination.offset, copy->destination.size});
            }
            else if (const auto *copy = eastl::get_if<TextureCopyCommand>(&command))
            {
                auto &texture = *static_cast<VKTexture *>(copy->texture.texture.get());
                const auto layout = copy->upload ? vk::ImageLayout::eTransferDstOptimal : vk::ImageLayout::eTransferSrcOptimal;
                states.synchronizeTextureSubresources(cb, texture, layout, vk::PipelineStageFlagBits2::eCopy,
                    copy->upload ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eTransferRead,
                    copy->texture.aspect, copy->texture.mipLevel, 1,
                    resolveImageCopyBaseArrayLayer(&texture, copy->texture), resolveImageCopyLayerCount(&texture, copy->extent));
                if (!copy->buffer.isNull())
                {
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->buffer.get()), copy->offset, copy->size,
                        vk::PipelineStageFlagBits2::eCopy, copy->upload ? vk::AccessFlagBits2::eTransferRead : vk::AccessFlagBits2::eTransferWrite);
                }
                const auto region = buildBufferImageCopy(&texture, copy->offset, copy->layout, copy->texture, copy->extent);
                if (copy->upload)
                {
                    cb.copyBufferToImage(copy->nativeBuffer, texture.getNativeImage(), layout, region);
                }
                else
                {
                    cb.copyImageToBuffer(texture.getNativeImage(), layout, copy->nativeBuffer, region);
                }
                if (!copy->upload && copy->buffer.isNull())
                {
                    // Readback allocator buffers are consumed by the host after completion.
                    vk::BufferMemoryBarrier2 barrier = {};
                    barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
                    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                    barrier.dstStageMask = vk::PipelineStageFlagBits2::eHost;
                    barrier.dstAccessMask = vk::AccessFlagBits2::eHostRead;
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer = copy->nativeBuffer; barrier.offset = copy->offset; barrier.size = copy->size;
                    vk::DependencyInfo dependency = {}; dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
                    cb.pipelineBarrier2(dependency);
                }
            }
            else if (const auto *copy = eastl::get_if<TextureToTextureCopyCommand>(&command))
            {
                auto &src = *static_cast<VKTexture *>(copy->source.texture.get());
                auto &dst = *static_cast<VKTexture *>(copy->destination.texture.get());
                const uint32_t srcBaseLayer = resolveImageCopyBaseArrayLayer(&src, copy->source);
                const uint32_t srcLayerCount = resolveImageCopyLayerCount(&src, copy->extent);
                const uint32_t dstBaseLayer = resolveImageCopyBaseArrayLayer(&dst, copy->destination);
                const uint32_t dstLayerCount = resolveImageCopyLayerCount(&dst, copy->extent);
                const vk::ImageAspectFlags srcAspect = resolveTextureAspect(src.getFormat(), copy->source.aspect);
                const vk::ImageAspectFlags dstAspect = resolveTextureAspect(dst.getFormat(), copy->destination.aspect);
                const bool same3DSubresource = (&src == &dst) &&
                    src.is3D() &&
                    (copy->source.mipLevel == copy->destination.mipLevel) &&
                    ((srcAspect & dstAspect) != vk::ImageAspectFlags{});
                vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal;
                vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal;
                if (same3DSubresource)
                {
                    srcLayout = vk::ImageLayout::eGeneral;
                    dstLayout = vk::ImageLayout::eGeneral;
                    const TextureAspectFlags combinedAspect =
                        (copy->source.aspect == copy->destination.aspect)
                            ? TextureAspectFlags(copy->source.aspect)
                            : TextureAspectFlags(TextureAspect::All);
                    states.synchronizeTextureSubresources(
                        cb,
                        src,
                        vk::ImageLayout::eGeneral,
                        vk::PipelineStageFlagBits2::eCopy,
                        vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
                        combinedAspect,
                        copy->source.mipLevel,
                        1u,
                        srcBaseLayer,
                        srcLayerCount);
                }
                else
                {
                    states.synchronizeTextureSubresources(cb, src, vk::ImageLayout::eTransferSrcOptimal,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                        copy->source.aspect, copy->source.mipLevel, 1u, srcBaseLayer, srcLayerCount);
                    states.synchronizeTextureSubresources(cb, dst, vk::ImageLayout::eTransferDstOptimal,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                        copy->destination.aspect, copy->destination.mipLevel, 1u, dstBaseLayer, dstLayerCount);
                }

                vk::ImageCopy region = {};
                region.srcSubresource.aspectMask = srcAspect;
                region.srcSubresource.mipLevel = copy->source.mipLevel;
                region.srcSubresource.baseArrayLayer = srcBaseLayer;
                region.srcSubresource.layerCount = srcLayerCount;
                region.srcOffset = vk::Offset3D{
                    static_cast<int32_t>(copy->source.origin.x),
                    static_cast<int32_t>(src.getDimension() == TextureDimension::e1D ? 0u : copy->source.origin.y),
                    static_cast<int32_t>(src.is3D() ? copy->source.origin.z : 0u)};
                region.dstSubresource.aspectMask = dstAspect;
                region.dstSubresource.mipLevel = copy->destination.mipLevel;
                region.dstSubresource.baseArrayLayer = dstBaseLayer;
                region.dstSubresource.layerCount = dstLayerCount;
                region.dstOffset = vk::Offset3D{
                    static_cast<int32_t>(copy->destination.origin.x),
                    static_cast<int32_t>(dst.getDimension() == TextureDimension::e1D ? 0u : copy->destination.origin.y),
                    static_cast<int32_t>(dst.is3D() ? copy->destination.origin.z : 0u)};
                region.extent = vk::Extent3D{
                    copy->extent.width,
                    (src.getDimension() == TextureDimension::e1D && dst.getDimension() == TextureDimension::e1D) ? 1u : copy->extent.height,
                    (src.is3D() || dst.is3D()) ? copy->extent.depth : 1u};
                cb.copyImage(
                    src.getNativeImage(),
                    srcLayout,
                    dst.getNativeImage(),
                    dstLayout,
                    region);
            }
            else if (const auto *copy = eastl::get_if<MipCopyCommand>(&command))
            {
                if (!copy->source.isNull())
                {
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->source.get()), copy->offset, copy->size,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
                }
                recordTextureMipChainCopy(cb, states, copy->nativeSource, copy->offset, static_cast<VKTexture *>(copy->destination.get()), copy->mipOffsets);
            }
            else
            {
                const auto &fill = eastl::get<FillCommand>(command);
                auto &buffer = *static_cast<VKBuffer *>(fill.destination.buffer.get());
                states.synchronizeBufferRange(cb, buffer, fill.destination.offset, fill.destination.size,
                    vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite);
                cb.fillBuffer(buffer.getNativeBuffer(), fill.destination.offset, fill.destination.size, static_cast<uint32_t>(fill.value) * 0x01010101u);
            }
        }

        if (mDevice->getInstance()->supportsDebugUtils())
        {
            vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cb));
        }
    }

    void VKBlitPassEncoder::end()
    {
        if (mEnded) return;
        ensureOpen("end");
        mCommandEncoder->appendPass(BlitPassEncoder(this));
        mCommandEncoder->writePassTimestamp(mTimestampWrites, false, vk::PipelineStageFlagBits2::eBottomOfPipe);
        if (!mLabelName.empty()) mCommandEncoder->notePassSummary();
        mCommandEncoder->notifyPassEnded();
        mEnded = true;
        mRecordingOwner = nullptr;
    }

    void VKBlitPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
            throw std::runtime_error((eastl::string(apiName) + " was called on an ended blit pass encoder.").c_str());
    }

    void VKBlitPassEncoder::validateTextureTransfer(const ImageCopyTexture &copy, const Extent3D &extent) const
    {
        const auto granularity = mDevice->getCapabilities().getQueueFamilyProperties()[
            mCommandEncoder->getQueue()->getQueueFamilyIndex()].minImageTransferGranularity;
        if (granularity.width == 1 && granularity.height == 1 && granularity.depth == 1)
        {
            return;
        }
        const auto &texture = *static_cast<VKTexture *>(copy.texture.get());
        if (copy.mipLevel >= texture.getMipLevelCount())
        {
            throw std::invalid_argument("Texture copy mip level exceeds the texture mip count.");
        }
        const auto block = Private::getTextureBlockInfo(texture.getFormat());
        const uint64_t alignment[] = {granularity.width * block.width, granularity.height * block.height, granularity.depth};
        const uint64_t origin[] = {copy.origin.x, texture.getDimension() == TextureDimension::e1D ? 0u : copy.origin.y,
            texture.is3D() ? copy.origin.z : 0u};
        const uint64_t size[] = {extent.width, texture.getDimension() == TextureDimension::e1D ? 1u : extent.height,
            texture.is3D() ? extent.depth : 1u};
        const uint64_t mipSize[] = {eastl::max(1u, texture.getWidth() >> copy.mipLevel),
            eastl::max(1u, texture.getHeight() >> copy.mipLevel), texture.is3D() ? eastl::max(1u, texture.getDepth() >> copy.mipLevel) : 1u};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const bool wholeMip = origin[axis] == 0 && size[axis] == mipSize[axis];
            if (alignment[axis] == 0)
            {
                if (!wholeMip)
                {
                    throw std::invalid_argument("The selected transfer queue requires whole-mip image copies.");
                }
            }
            else if (origin[axis] % alignment[axis] != 0 ||
                (size[axis] % alignment[axis] != 0 && origin[axis] + size[axis] != mipSize[axis]))
            {
                throw std::invalid_argument("Texture copy region does not satisfy the selected queue family's transfer granularity.");
            }
        }
    }
} // namespace GVM::RHI::Vulkan
