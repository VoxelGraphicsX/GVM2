#ifndef UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_ASTC4X4_HPP
#define UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_ASTC4X4_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupRWTexture3DASTC4x4 final : public IBindGroup
{
    constructor(RWTexture3D<TextureFormat::ASTC4x4Unorm> compressedOutput [[Binding0]])
    {
    }
};

#endif
