#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_READ_WRITE_SAME_FIELD_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_READ_WRITE_SAME_FIELD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalReadWriteFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        lighting;
};

class InvalidPixelLocalReadWritePass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalReadWriteFrame pixel(InvalidPixelLocalReadWriteFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalReadWriteFrame outputValue;
        half4 previousLighting = inputValue.lighting.read();
        outputValue.lighting = previousLighting;
        return outputValue;
    }
};

#endif
