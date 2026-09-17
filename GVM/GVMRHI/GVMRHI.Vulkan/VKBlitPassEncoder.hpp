#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>

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
        void fillBuffer(BufferRange source, uint32_t data) override;
        void end() override;

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
        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mCommandEncoder = nullptr;
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
