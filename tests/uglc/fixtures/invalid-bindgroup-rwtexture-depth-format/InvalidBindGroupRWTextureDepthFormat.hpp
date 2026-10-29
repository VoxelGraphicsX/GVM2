#ifndef UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE_DEPTH_FORMAT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE_DEPTH_FORMAT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupRWTextureDepthFormatBindGroup final : public IBindGroup
{
    constructor(RWTexture2D<TextureFormat::Depth32Float> depthTexture [[Binding0]])
    {
    }
};

#endif
