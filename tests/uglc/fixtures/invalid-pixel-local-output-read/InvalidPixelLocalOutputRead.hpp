#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_OUTPUT_READ_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_OUTPUT_READ_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalOutputReadFrame final : public IFrameBuffer
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

class InvalidPixelLocalOutputReadPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalOutputReadFrame pixel(InvalidPixelLocalOutputReadFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalOutputReadFrame outputValue;
        half4 albedoValue = outputValue.albedo.read();
        outputValue.lighting = albedoValue;
        return outputValue;
    }
};

#endif
