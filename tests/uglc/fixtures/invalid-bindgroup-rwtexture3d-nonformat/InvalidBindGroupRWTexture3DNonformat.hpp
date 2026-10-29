#ifndef UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_NONFORMAT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_RWTEXTURE3D_NONFORMAT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupRWTexture3DNonformatBindGroup final : public IBindGroup
{
    constructor(RWTexture3D<float4> storageVolume [[Binding0]])
    {
    }
};

#endif
