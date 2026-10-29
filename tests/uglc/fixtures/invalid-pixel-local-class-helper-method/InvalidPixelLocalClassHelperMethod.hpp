#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_CLASS_HELPER_METHOD_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_CLASS_HELPER_METHOD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalClassHelperFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        color;
};

class InvalidPixelLocalClassHelperPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    half4 resolveMaterial()
    {
        return half4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    InvalidPixelLocalClassHelperFrame pixel()
    {
        InvalidPixelLocalClassHelperFrame outputValue;
        outputValue.color = half4(0.0f, 0.0f, 0.0f, 1.0f);
        return outputValue;
    }
};

#endif
