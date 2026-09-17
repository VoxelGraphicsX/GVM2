#ifndef UGLC_TEST_INVALID_BINDGROUP_DUPLICATE_BINDING_HPP
#define UGLC_TEST_INVALID_BINDGROUP_DUPLICATE_BINDING_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupDuplicateBinding final : public IBindGroup
{
    constructor(Texture2D<float4> albedoTexture [[Binding0]],
                Sampler linearSampler [[Binding0]])
    {
    }
};

#endif
