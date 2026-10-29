#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_TRANSIENT_PERSISTENT_POLICY_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_TRANSIENT_PERSISTENT_POLICY_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalTransientPersistentFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Load,
                              PixelLocalStore::Store>
        lighting;
};

#endif
