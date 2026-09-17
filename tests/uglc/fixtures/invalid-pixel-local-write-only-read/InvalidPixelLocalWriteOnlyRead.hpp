#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_WRITE_ONLY_READ_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_WRITE_ONLY_READ_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalWriteOnlyReadFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::WriteOnly,
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

class InvalidPixelLocalWriteOnlyReadPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalWriteOnlyReadFrame pixel(InvalidPixelLocalWriteOnlyReadFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalWriteOnlyReadFrame outputValue;
        half4 albedoValue = inputValue.albedo.read();
        outputValue.lighting = albedoValue;
        return outputValue;
    }
};

#endif
