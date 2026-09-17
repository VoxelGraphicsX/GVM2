#include "VKBlitPassEncoder.hpp"

#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

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
        [[nodiscard]]
        eastl::string resolveLogLabel(const eastl::string &label)
        {
            return label.empty() ? eastl::string("<unlabeled>") : label;
        }

        [[nodiscard]]
        eastl::string describeBufferForValidation(const VKBuffer &buffer)
        {
            eastl::string text = "label=";
            text += resolveLogLabel(buffer.getLabelName());
            text += " size=";
            text += eastl::to_string(buffer.getStorageSize());
            text += " native_usage=";
            text += eastl::to_string(static_cast<uint64_t>(static_cast<VkBufferUsageFlags>(buffer.getNativeUsageFlags())));
            text += " declared_usage=";
            text += eastl::to_string(static_cast<uint64_t>(buffer.getUsage()));
            return text;
        }

        void validateTransferBuffer(
            const char *apiName,
            const VKBuffer *buffer,
            uint64_t offset,
            uint64_t size,
            vk::BufferUsageFlagBits requiredUsage)
        {
            if (buffer == nullptr)
            {
                throw makeInvalidArgument(eastl::string(apiName) + " requires a valid Vulkan buffer.");
            }
            if ((buffer->getNativeUsageFlags() & requiredUsage) != requiredUsage)
            {
                throw makeInvalidArgument(
                    eastl::string(apiName) +
                    " requires transfer-compatible buffer usage, but the destination/source buffer was created without the required native usage bit. " +
                    describeBufferForValidation(*buffer));
            }
            if (offset > buffer->getStorageSize() || size > (buffer->getStorageSize() - offset))
            {
                throw makeOutOfRange(
                    eastl::string(apiName) +
                    " transfer range exceeds the Vulkan buffer bounds. " +
                    describeBufferForValidation(*buffer) +
                    " requested_offset=" + eastl::to_string(offset) +
                    " requested_size=" + eastl::to_string(size));
            }
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

            stateTracker.transitionTextureLayout(commandBuffer, *destinationTexture, vk::ImageLayout::eTransferDstOptimal);
            eastl::vector<vk::BufferImageCopy> nativeRegions(regions.begin(), regions.end());
            commandBuffer.copyBufferToImage(
                sourceBuffer,
                destinationTexture->getNativeImage(),
                vk::ImageLayout::eTransferDstOptimal,
                nativeRegions);
            stateTracker.restoreTextureSteadyStateLayout(commandBuffer, *destinationTexture);
        }
    } // namespace

    void VKBlitPassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const BlitPassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
        {
            throw makeInvalidArgument("VKBlitPassEncoder::init requires a valid device and command encoder.");
        }

        mDevice = device;
        mCommandEncoder = commandEncoder;
        mDescriptor = descriptor;
        mTimestampWrites = descriptor.timestampWrites;
        mLabelName = descriptor.label;
        mCopyBufferToBufferCount = 0u;
        mCopyBufferToTextureCount = 0u;
        mCopyTextureToBufferCount = 0u;
        mFillBufferCount = 0u;
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->writePassTimestamp(
            mTimestampWrites,
            true,
            vk::PipelineStageFlagBits::eTopOfPipe);
    }

    void VKBlitPassEncoder::copyBufferToBuffer(BufferRange source, BufferRange destination)
    {
        ensureOpen("VKBlitPassEncoder::copyBufferToBuffer");
        if (source.buffer.isNull() || destination.buffer.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToBuffer requires valid source and destination buffers.");
        }

        auto *sourceBuffer = static_cast<VKBuffer *>(source.buffer.get());
        auto *destinationBuffer = static_cast<VKBuffer *>(destination.buffer.get());
        const uint64_t copySize = eastl::min(source.size, destination.size);
        if (copySize == 0)
        {
            return;
        }
        validateTransferBuffer("VKBlitPassEncoder::copyBufferToBuffer", sourceBuffer, source.offset, copySize, vk::BufferUsageFlagBits::eTransferSrc);
        validateTransferBuffer("VKBlitPassEncoder::copyBufferToBuffer", destinationBuffer, destination.offset, copySize, vk::BufferUsageFlagBits::eTransferDst);

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainBuffer(source.buffer);
        commandEncoder->retainBuffer(destination.buffer);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        commandEncoder->getTaskDependencyResolver().synchronizeBufferRanges(
            commandBuffer,
            {
                VKTaskDependencyResolver::BufferRangeSyncRequest{
                    .buffer = sourceBuffer,
                    .offset = source.offset,
                    .size = copySize,
                    .requiredStageMask = vk::PipelineStageFlagBits::eTransfer,
                    .requiredAccessMask = vk::AccessFlagBits::eTransferRead,
                },
                VKTaskDependencyResolver::BufferRangeSyncRequest{
                    .buffer = destinationBuffer,
                    .offset = destination.offset,
                    .size = copySize,
                    .requiredStageMask = vk::PipelineStageFlagBits::eTransfer,
                    .requiredAccessMask = vk::AccessFlagBits::eTransferWrite,
                },
            });
        commandBuffer.copyBuffer(sourceBuffer->getNativeBuffer(), destinationBuffer->getNativeBuffer(), vk::BufferCopy{source.offset, destination.offset, copySize});
        ++mCopyBufferToBufferCount;
    }

    void VKBlitPassEncoder::copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize)
    {
        ensureOpen("VKBlitPassEncoder::copyBufferToTexture");
        if (source.buffer.isNull() || destination.texture.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToTexture requires a valid source buffer and destination texture.");
        }

        auto *sourceBuffer = static_cast<VKBuffer *>(source.buffer.get());
        auto *destinationTexture = static_cast<VKTexture *>(destination.texture.get());
        const uint64_t bytesToCopy = calculateRequiredTextureCopyBytes(destination.texture->getFormat(), source.layout, copySize);
        validateTransferBuffer("VKBlitPassEncoder::copyBufferToTexture", sourceBuffer, 0u, bytesToCopy, vk::BufferUsageFlagBits::eTransferSrc);
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainBuffer(source.buffer);
        commandEncoder->retainTexture(destination.texture);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        stateTracker.synchronizeBufferRange(commandBuffer, *sourceBuffer, 0, WholeSize, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
        stateTracker.synchronizeTextureSubresources(
            commandBuffer,
            *destinationTexture,
            vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite,
            destination.aspect,
            destination.mipLevel,
            1u,
            resolveImageCopyBaseArrayLayer(destinationTexture, destination),
            resolveImageCopyLayerCount(destinationTexture, copySize));
        const vk::BufferImageCopy region = buildBufferImageCopy(destinationTexture, 0, source.layout, destination, copySize);
        commandBuffer.copyBufferToImage(
            sourceBuffer->getNativeBuffer(),
            destinationTexture->getNativeImage(),
            vk::ImageLayout::eTransferDstOptimal,
            region);
        ++mCopyBufferToTextureCount;
    }

    void VKBlitPassEncoder::copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize)
    {
        ensureOpen("VKBlitPassEncoder::copyTextureToBuffer");
        if (source.texture.isNull() || destination.buffer.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyTextureToBuffer requires a valid source texture and destination buffer.");
        }

        auto *sourceTexture = static_cast<VKTexture *>(source.texture.get());
        auto *destinationBuffer = static_cast<VKBuffer *>(destination.buffer.get());
        const uint64_t bytesToCopy = calculateRequiredTextureCopyBytes(source.texture->getFormat(), destination.layout, copySize);
        validateTransferBuffer("VKBlitPassEncoder::copyTextureToBuffer", destinationBuffer, 0u, bytesToCopy, vk::BufferUsageFlagBits::eTransferDst);
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainTexture(source.texture);
        commandEncoder->retainBuffer(destination.buffer);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        stateTracker.synchronizeTextureSubresources(
            commandBuffer,
            *sourceTexture,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferRead,
            source.aspect,
            source.mipLevel,
            1u,
            resolveImageCopyBaseArrayLayer(sourceTexture, source),
            resolveImageCopyLayerCount(sourceTexture, copySize));
        stateTracker.synchronizeBufferRange(commandBuffer, *destinationBuffer, 0, WholeSize, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
        const vk::BufferImageCopy region = buildBufferImageCopy(sourceTexture, 0, destination.layout, source, copySize);
        commandBuffer.copyImageToBuffer(
            sourceTexture->getNativeImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            destinationBuffer->getNativeBuffer(),
            region);
        ++mCopyTextureToBufferCount;
    }

    void VKBlitPassEncoder::fillBuffer(BufferRange source, uint32_t data)
    {
        ensureOpen("VKBlitPassEncoder::fillBuffer");
        if (source.buffer.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::fillBuffer requires a valid destination buffer.");
        }
        if ((source.offset % 4u) != 0u || (source.size % 4u) != 0u)
        {
            throw makeInvalidArgument("VKBlitPassEncoder::fillBuffer requires 4-byte aligned offsets and sizes.");
        }

        auto *buffer = static_cast<VKBuffer *>(source.buffer.get());
        validateTransferBuffer("VKBlitPassEncoder::fillBuffer", buffer, source.offset, source.size, vk::BufferUsageFlagBits::eTransferDst);
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainBuffer(source.buffer);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();
        GVMLogTrace(
            mDevice, BlitPassEncoderLogCategory,
            "event=blit_fill_buffer pass_label={} command_encoder_ptr={} command_buffer_ptr={} buffer_label={} buffer_ptr={} allocation_ptr={} device_memory_ptr={} allocation_offset={} native_usage_bits={} host_visible={} offset={} size={} data={}",
            safeLogLabel(mLabelName),
            static_cast<void *>(commandEncoder),
            reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
            safeLogLabel(buffer->getLabelName()),
            reinterpret_cast<void *>(static_cast<VkBuffer>(buffer->getNativeBuffer())),
            static_cast<void *>(buffer->getNativeAllocation()),
            reinterpret_cast<void *>(buffer->getNativeDeviceMemory()),
            static_cast<uint64_t>(buffer->getNativeAllocationOffset()),
            static_cast<uint64_t>(static_cast<VkBufferUsageFlags>(buffer->getNativeUsageFlags())),
            buffer->isHostVisible(),
            source.offset,
            source.size,
            data);
        stateTracker.synchronizeBufferRange(commandBuffer, *buffer, source.offset, source.size, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
        commandBuffer.fillBuffer(buffer->getNativeBuffer(), source.offset, source.size, data);
        ++mFillBufferCount;
    }

    void VKBlitPassEncoder::copyNativeBufferToBuffer(vk::Buffer sourceBuffer, uint64_t sourceOffset, BufferRange destination)
    {
        ensureOpen("VKBlitPassEncoder::copyNativeBufferToBuffer");
        if (sourceBuffer == vk::Buffer{} || destination.buffer.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyNativeBufferToBuffer requires a valid native source buffer and destination buffer.");
        }

        auto *destinationBuffer = static_cast<VKBuffer *>(destination.buffer.get());
        if (destinationBuffer == nullptr)
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyNativeBufferToBuffer requires a Vulkan destination buffer.");
        }
        if (destination.size == 0u)
        {
            return;
        }
        validateTransferBuffer("VKBlitPassEncoder::copyNativeBufferToBuffer", destinationBuffer, destination.offset, destination.size, vk::BufferUsageFlagBits::eTransferDst);

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainBuffer(destination.buffer);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        stateTracker.synchronizeBufferRange(
            commandBuffer,
            *destinationBuffer,
            destination.offset,
            destination.size,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite);
        commandBuffer.copyBuffer(
            sourceBuffer,
            destinationBuffer->getNativeBuffer(),
            vk::BufferCopy{sourceOffset, destination.offset, destination.size});
        ++mCopyBufferToBufferCount;
    }

    void VKBlitPassEncoder::copyBufferToTexture(
        Buffer source,
        vk::Buffer nativeSourceBuffer,
        uint64_t sourceOffset,
        uint64_t sourceSize,
        const TextureDataLayout &sourceLayout,
        const ImageCopyTexture &destination,
        const Extent3D &copySize)
    {
        ensureOpen("VKBlitPassEncoder::copyBufferToTexture");
        if (nativeSourceBuffer == vk::Buffer{} || destination.texture.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToTexture requires a valid source buffer and destination texture.");
        }

        auto *sourceBuffer = source.isNull() ? nullptr : static_cast<VKBuffer *>(source.get());
        auto *destinationTexture = static_cast<VKTexture *>(destination.texture.get());
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        if (!source.isNull())
        {
            commandEncoder->retainBuffer(source);
        }
        commandEncoder->retainTexture(destination.texture);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        if (sourceBuffer != nullptr)
        {
            validateTransferBuffer("VKBlitPassEncoder::copyBufferToTexture", sourceBuffer, sourceOffset, sourceSize, vk::BufferUsageFlagBits::eTransferSrc);
            stateTracker.synchronizeBufferRange(
                commandBuffer,
                *sourceBuffer,
                sourceOffset,
                sourceSize,
                vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eTransferRead);
        }

        const uint32_t baseArrayLayer = resolveImageCopyBaseArrayLayer(destinationTexture, destination);
        const uint32_t layerCount = resolveImageCopyLayerCount(destinationTexture, copySize);
        stateTracker.transitionTextureLayout(
            commandBuffer,
            *destinationTexture,
            vk::ImageLayout::eTransferDstOptimal,
            destination.aspect,
            destination.mipLevel,
            1u,
            baseArrayLayer,
            layerCount);
        const vk::BufferImageCopy region = buildBufferImageCopy(destinationTexture, sourceOffset, sourceLayout, destination, copySize);
        commandBuffer.copyBufferToImage(
            nativeSourceBuffer,
            destinationTexture->getNativeImage(),
            vk::ImageLayout::eTransferDstOptimal,
            region);
        stateTracker.restoreTextureSteadyStateLayout(
            commandBuffer,
            *destinationTexture,
            destination.aspect,
            destination.mipLevel,
            1u,
            baseArrayLayer,
            layerCount);
        ++mCopyBufferToTextureCount;
    }

    void VKBlitPassEncoder::copyTextureToBuffer(
        const ImageCopyTexture &source,
        Buffer destination,
        vk::Buffer nativeDestinationBuffer,
        uint64_t destinationOffset,
        uint64_t destinationSize,
        const TextureDataLayout &destinationLayout,
        const Extent3D &copySize)
    {
        ensureOpen("VKBlitPassEncoder::copyTextureToBuffer");
        if (source.texture.isNull() || nativeDestinationBuffer == vk::Buffer{})
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyTextureToBuffer requires a valid source texture and destination buffer.");
        }

        auto *sourceTexture = static_cast<VKTexture *>(source.texture.get());
        auto *destinationBuffer = destination.isNull() ? nullptr : static_cast<VKBuffer *>(destination.get());
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->retainTexture(source.texture);
        if (!destination.isNull())
        {
            commandEncoder->retainBuffer(destination);
        }
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        const uint32_t baseArrayLayer = resolveImageCopyBaseArrayLayer(sourceTexture, source);
        const uint32_t layerCount = resolveImageCopyLayerCount(sourceTexture, copySize);
        stateTracker.transitionTextureLayout(
            commandBuffer,
            *sourceTexture,
            vk::ImageLayout::eTransferSrcOptimal,
            source.aspect,
            source.mipLevel,
            1u,
            baseArrayLayer,
            layerCount);
        if (destinationBuffer != nullptr)
        {
            validateTransferBuffer("VKBlitPassEncoder::copyTextureToBuffer", destinationBuffer, destinationOffset, destinationSize, vk::BufferUsageFlagBits::eTransferDst);
            stateTracker.synchronizeBufferRange(
                commandBuffer,
                *destinationBuffer,
                destinationOffset,
                destinationSize,
                vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eTransferWrite);
        }
        const vk::BufferImageCopy region = buildBufferImageCopy(sourceTexture, destinationOffset, destinationLayout, source, copySize);
        commandBuffer.copyImageToBuffer(
            sourceTexture->getNativeImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            nativeDestinationBuffer,
            region);
        stateTracker.restoreTextureSteadyStateLayout(
            commandBuffer,
            *sourceTexture,
            source.aspect,
            source.mipLevel,
            1u,
            baseArrayLayer,
            layerCount);
        ++mCopyTextureToBufferCount;
    }

    void VKBlitPassEncoder::copyBufferToTextureMipChain(BufferRange source, Texture destination, const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureOpen("VKBlitPassEncoder::copyBufferToTextureMipChain");
        if (source.buffer.isNull() || destination.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToTextureMipChain requires a valid source buffer and destination texture.");
        }

        copyBufferToTextureMipChain(
            source.buffer,
            static_cast<VKBuffer *>(source.buffer.get())->getNativeBuffer(),
            source.offset,
            source.size,
            destination,
            mipmapOffsetBytes);
    }

    void VKBlitPassEncoder::copyBufferToTextureMipChain(
        Buffer source,
        vk::Buffer nativeSourceBuffer,
        uint64_t sourceOffset,
        uint64_t sourceSize,
        Texture destination,
        const eastl::vector<uint64_t> &mipmapOffsetBytes)
    {
        ensureOpen("VKBlitPassEncoder::copyBufferToTextureMipChain");
        if (nativeSourceBuffer == vk::Buffer{} || destination.isNull())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToTextureMipChain requires a valid source buffer and destination texture.");
        }

        auto *sourceBuffer = source.isNull() ? nullptr : static_cast<VKBuffer *>(source.get());
        auto *destinationTexture = static_cast<VKTexture *>(destination.get());
        if (mipmapOffsetBytes.size() < destinationTexture->getMipLevelCount())
        {
            throw makeInvalidArgument("VKBlitPassEncoder::copyBufferToTextureMipChain requires one mip offset per destination mip level.");
        }

        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        if (!source.isNull())
        {
            commandEncoder->retainBuffer(source);
        }
        commandEncoder->retainTexture(destination);
        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();

        if (sourceBuffer != nullptr)
        {
            validateTransferBuffer("VKBlitPassEncoder::copyBufferToTextureMipChain", sourceBuffer, sourceOffset, sourceSize, vk::BufferUsageFlagBits::eTransferSrc);
            stateTracker.synchronizeBufferRange(
                commandBuffer,
                *sourceBuffer,
                sourceOffset,
                sourceSize,
                vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eTransferRead);
        }

        recordTextureMipChainCopy(
            commandBuffer,
            stateTracker,
            nativeSourceBuffer,
            sourceOffset,
            destinationTexture,
            mipmapOffsetBytes);
        ++mCopyBufferToTextureCount;
    }

    void VKBlitPassEncoder::end()
    {
        if (mEnded)
        {
            return;
        }
        ensureOpen("VKBlitPassEncoder::end");
        auto *commandEncoder = static_cast<VKCommandEncoder *>(mCommandEncoder.get());
        commandEncoder->writePassTimestamp(
            mTimestampWrites,
            false,
            vk::PipelineStageFlagBits::eBottomOfPipe);
        if (!mLabelName.empty())
        {
            commandEncoder->notePassSummary();
        }
        commandEncoder->notifyPassEnded();
        mEnded = true;
    }

    void VKBlitPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on an ended Vulkan blit pass encoder.");
        }
    }
} // namespace GVM::RHI::Vulkan
