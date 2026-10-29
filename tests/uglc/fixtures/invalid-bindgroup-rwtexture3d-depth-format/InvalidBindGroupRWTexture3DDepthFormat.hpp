#ifndef UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_DEPTH_FORMAT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_DEPTH_FORMAT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupRWTexture3DDepthFormatBindGroup final : public IBindGroup
{
    constructor(RWTexture3D<TextureFormat::Depth32Float> depthVolume [[Binding0]])
    {
    }
};

#endif
