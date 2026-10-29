#ifndef UGLC_TEST_INVALID_BINDGROUP_LEGACY_SLOT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_LEGACY_SLOT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupLegacySlot final : public IBindGroup
{
    constructor(Texture2D<float4> albedoTexture [[Slot0]],
                Sampler linearSampler [[Binding1]])
    {
    }
};

#endif
