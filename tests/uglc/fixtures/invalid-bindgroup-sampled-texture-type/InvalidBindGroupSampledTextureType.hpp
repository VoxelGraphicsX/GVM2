#ifndef UGLC_TEST_INVALID_BINDGROUP_SAMPLED_TEXTURE_TYPE_HPP
#define UGLC_TEST_INVALID_BINDGROUP_SAMPLED_TEXTURE_TYPE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupSampledTextureTypeBindGroup final : public IBindGroup
{
    constructor(Texture2D<bool> invalidTexture [[Binding0]])
    {
    }
};

#endif
