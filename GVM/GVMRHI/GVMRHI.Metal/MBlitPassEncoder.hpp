#pragma once
#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MBlitPassEncoder final : public BlitPassEncoderImpl
    {
    public:
        MBlitPassEncoder();
        ~MBlitPassEncoder();
        void init(MDevice *device, MCommandEncoder *commandEncoder, const BlitPassDescriptor &descriptor);
        virtual void copyBufferToBuffer(BufferRange source, BufferRange destination) override;
        virtual void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) override;
        virtual void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) override;
        virtual void fillBuffer(BufferRange source, uint32_t data) override;
        virtual void end() override;
        MTL::BlitCommandEncoder *getNativeCommandEncoder() const;

    private:
        MTL::BlitCommandEncoder *mNativeCommandEncoder = nullptr;
        MDevice *mDevice = nullptr;
        MCommandEncoder *mWeakCommandEncoder = nullptr;
        bool mEnded = false;
    };

} // namespace GVM::RHI::Metal
