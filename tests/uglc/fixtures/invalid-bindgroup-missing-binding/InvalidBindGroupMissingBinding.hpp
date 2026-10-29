#ifndef UGLC_TEST_INVALID_BINDGROUP_MISSING_BINDING_HPP
#define UGLC_TEST_INVALID_BINDGROUP_MISSING_BINDING_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupMissingBinding final : public IBindGroup
{
    constructor(Texture2D<float4> albedoTexture,
                Sampler linearSampler [[Binding1]])
    {
    }
};

#endif
