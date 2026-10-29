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
            throw std::invalid_argument("Texture upload requires a buffer and texture.");
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
            throw std::invalid_argument("Texture readback requires a texture and buffer.");
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
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->source.get()), copy->sourceOffset, copy->destination.size,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
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
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->buffer.get()), copy->offset, copy->size,
                        vk::PipelineStageFlagBits2::eCopy, copy->upload ? vk::AccessFlagBits2::eTransferRead : vk::AccessFlagBits2::eTransferWrite);
                const auto region = buildBufferImageCopy(&texture, copy->offset, copy->layout, copy->texture, copy->extent);
                if (copy->upload) cb.copyBufferToImage(copy->nativeBuffer, texture.getNativeImage(), layout, region);
                else cb.copyImageToBuffer(texture.getNativeImage(), layout, copy->nativeBuffer, region);
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
            else if (const auto *copy = eastl::get_if<MipCopyCommand>(&command))
            {
                if (!copy->source.isNull())
                    states.synchronizeBufferRange(cb, *static_cast<VKBuffer *>(copy->source.get()), copy->offset, copy->size,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
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
} // namespace GVM::RHI::Vulkan
