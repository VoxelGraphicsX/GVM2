#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_DEPTH_READ_METAL_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_DEPTH_READ_METAL_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalDepthReadFrame final : public IFrameBuffer
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

class InvalidPixelLocalDepthReadPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalDepthReadFrame pixel(InvalidPixelLocalDepthReadFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalDepthReadFrame outputValue;
        const float depthValue = inputValue.depth.read();
        outputValue.lighting = half4(half(depthValue), half(0.0f), half(0.0f), half(1.0f));
        return outputValue;
    }
};

#endif
