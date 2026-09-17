#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_MOVE_ATTACHMENT_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_MOVE_ATTACHMENT_HPP

#include "UGL.h"
#include <utility>

using namespace UGL;

struct InvalidPixelLocalMoveFrame final : public IFrameBuffer
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

class InvalidPixelLocalMovePass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalMoveFrame pixel(InvalidPixelLocalMoveFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalMoveFrame outputValue;
        auto movedAlbedo = std::move(inputValue.albedo);
        half4 albedoValue = movedAlbedo.read();
        outputValue.lighting = albedoValue;
        return outputValue;
    }
};

#endif
