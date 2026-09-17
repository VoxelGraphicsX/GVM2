#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_LOCAL_READ_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_LOCAL_READ_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalLocalReadFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;

    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::DontCare,
                              PixelLocalStore::Discard>
        lighting;
};

class InvalidPixelLocalLocalReadPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalLocalReadFrame pixel(InvalidPixelLocalLocalReadFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalLocalReadFrame localValue;
        InvalidPixelLocalLocalReadFrame outputValue;
        half4 albedoValue = localValue.albedo.read();
        outputValue.lighting = albedoValue;
        return outputValue;
    }
};

#endif
