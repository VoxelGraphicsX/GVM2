#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/variant.h>

namespace GVM::RHI::Vulkan
{
    class VKBlitPassEncoder final : public BlitPassEncoderImpl
    {
    public:
        VKBlitPassEncoder() = default;

        void init(VKDevice *device, CommandEncoder commandEncoder, const BlitPassDescriptor &descriptor);

        void copyBufferToBuffer(BufferRange source, BufferRange destination) override;
        void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) override;
        void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) override;
        void fillBuffer(BufferRange source, uint8_t value) override;
        void end() override;
        /// Emits retained copies in their final queue order.
        void encode(VKCommandEncoder &commandEncoder);

        void copyNativeBufferToBuffer(vk::Buffer sourceBuffer, uint64_t sourceOffset, BufferRange destination);
        void copyBufferToTexture(
            Buffer source,
            vk::Buffer nativeSourceBuffer,
            uint64_t sourceOffset,
            uint64_t sourceSize,
            const TextureDataLayout &sourceLayout,
            const ImageCopyTexture &destination,
            const Extent3D &copySize);
        void copyTextureToBuffer(
            const ImageCopyTexture &source,
            Buffer destination,
            vk::Buffer nativeDestinationBuffer,
            uint64_t destinationOffset,
            uint64_t destinationSize,
            const TextureDataLayout &destinationLayout,
            const Extent3D &copySize);
        void copyBufferToTextureMipChain(BufferRange source, Texture destination, const eastl::vector<uint64_t> &mipmapOffsetBytes);
        void copyBufferToTextureMipChain(
            Buffer source,
            vk::Buffer nativeSourceBuffer,
            uint64_t sourceOffset,
            uint64_t sourceSize,
            Texture destination,
            const eastl::vector<uint64_t> &mipmapOffsetBytes);

    private:
        /// Holds either a resource-backed copy source or an upload allocator buffer.
        struct BufferCopyCommand
        {
            Buffer source;
            vk::Buffer nativeSource;
            uint64_t sourceOffset;
            BufferRange destination;
        };
        /// Describes one upload or readback, retaining tracked resources when present.
        struct TextureCopyCommand
        {
            Buffer buffer;
            vk::Buffer nativeBuffer;
            uint64_t offset, size;
            TextureDataLayout layout;
            ImageCopyTexture texture;
            Extent3D extent;
            bool upload;
        };
        /// Copies an uploaded mip chain without retaining caller-owned offset storage.
        struct MipCopyCommand
        {
            Buffer source;
            vk::Buffer nativeSource;
            uint64_t offset, size;
            Texture destination;
            eastl::vector<uint64_t> mipOffsets;
        };
        /// Records a typed buffer clear.
        struct FillCommand { BufferRange destination; uint8_t value; };
        eastl::vector<eastl::variant<BufferCopyCommand, TextureCopyCommand, MipCopyCommand, FillCommand>> mCommands;
        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mRecordingOwner = nullptr;
        VKCommandEncoder *mCommandEncoder = nullptr;
        BlitPassDescriptor mDescriptor = {};
        PassTimestampWrites mTimestampWrites = {};
        eastl::string mLabelName;
        uint32_t mCopyBufferToBufferCount = 0u;
        uint32_t mCopyBufferToTextureCount = 0u;
        uint32_t mCopyTextureToBufferCount = 0u;
        uint32_t mFillBufferCount = 0u;
        bool mEnded = false;
    };
} // namespace GVM::RHI::Vulkan
