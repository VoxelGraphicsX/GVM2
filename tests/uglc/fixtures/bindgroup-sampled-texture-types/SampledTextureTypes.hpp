#ifndef UGLC_TEST_BINDGROUP_SAMPLED_TEXTURE_TYPES_HPP
#define UGLC_TEST_BINDGROUP_SAMPLED_TEXTURE_TYPES_HPP

#include "UGL.h"

using namespace UGL;

struct SampledTextureTypesBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> floatTexture [[Binding0]],
                Texture2D<TextureFormat::RG32Uint> uintTexture [[Binding1]],
                Texture2DArray<TextureFormat::RGBA8Sint> sintTextureArray [[Binding2]],
                Texture2D<TextureFormat::Depth32Float> depthTexture [[Binding3]],
                Sampler sampler0 [[Binding4]])
    {
    }
};

#endif
