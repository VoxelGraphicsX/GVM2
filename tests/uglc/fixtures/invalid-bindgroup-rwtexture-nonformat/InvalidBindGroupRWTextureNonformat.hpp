#ifndef UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE_NONFORMAT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE_NONFORMAT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupRWTextureNonformatBindGroup final : public IBindGroup
{
    constructor(RWTexture2D<float4> storageImage [[Binding0]])
    {
    }
};

#endif
