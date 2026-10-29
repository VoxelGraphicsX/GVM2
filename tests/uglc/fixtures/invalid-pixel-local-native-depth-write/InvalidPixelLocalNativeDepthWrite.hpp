#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_NATIVE_DEPTH_WRITE_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_NATIVE_DEPTH_WRITE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalNativeDepthWriteFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        lighting;

    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less>
        depth;
};

class InvalidPixelLocalNativeDepthWritePass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalNativeDepthWriteFrame pixel()
    {
        InvalidPixelLocalNativeDepthWriteFrame outputValue;
        outputValue.lighting = half4(0.5f, 0.5f, 0.5f, 1.0f);
        outputValue.depth = 0.5f;
        return outputValue;
    }
};

#endif
