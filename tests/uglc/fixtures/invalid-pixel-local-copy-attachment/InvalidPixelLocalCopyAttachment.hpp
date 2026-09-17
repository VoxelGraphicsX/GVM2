#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_COPY_ATTACHMENT_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_COPY_ATTACHMENT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalCopyFrame final : public IFrameBuffer
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

class InvalidPixelLocalCopyPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalCopyFrame pixel(InvalidPixelLocalCopyFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalCopyFrame outputValue;
        auto copiedAlbedo = inputValue.albedo;
        half4 albedoValue = copiedAlbedo.read();
        outputValue.lighting = albedoValue;
        return outputValue;
    }
};

#endif
