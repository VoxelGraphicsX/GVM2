#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_DEPTH_WRITE_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_DEPTH_WRITE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalDepthWriteFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        lighting;

    PixelLocalDepthAttachment<TextureFormat::Depth32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        depth;
};

class InvalidPixelLocalDepthWritePass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalDepthWriteFrame pixel()
    {
        InvalidPixelLocalDepthWriteFrame outputValue;
        outputValue.lighting = half4(0.5f, 0.5f, 0.5f, 1.0f);
        outputValue.depth = 0.5f;
        return outputValue;
    }
};

#endif
