#ifndef GBUFFER_HPP
#define GBUFFER_HPP

#include "UGL.h"
using namespace UGL;

struct GBufferBindGroup final : public IBindGroup
{
    constructor(Texture2D<UGL::TextureFormat::RGBA8Unorm> albedoTexture [[Binding0]], Texture2D<UGL::TextureFormat::R32Float> depthTexture [[Binding1]])
    {
    }
};

#endif // GBUFFER_HPP
