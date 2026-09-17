#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_READ_ONLY_WRITE_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_READ_ONLY_WRITE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalReadOnlyWriteFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadOnly,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        lighting;
};

class InvalidPixelLocalReadOnlyWritePass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalReadOnlyWriteFrame pixel()
    {
        InvalidPixelLocalReadOnlyWriteFrame outputValue;
        outputValue.lighting = half4(0.5f, 0.5f, 0.5f, 1.0f);
        return outputValue;
    }
};

#endif
