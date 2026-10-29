#ifndef UGLC_TEST_ASTC4X4_UNORM_TEXTURE_FORMAT_HPP
#define UGLC_TEST_ASTC4X4_UNORM_TEXTURE_FORMAT_HPP

#include "UGL.h"

using namespace UGL;

struct Astc4x4UnormTextureFormatBindGroup final : public IBindGroup
{
    constructor(Texture2D<TextureFormat::ASTC4x4Unorm> texture2D [[Binding0]],
                Texture2DArray<TextureFormat::ASTC4x4Unorm> textureArray [[Binding1]],
                Texture3D<TextureFormat::ASTC4x4Unorm> texture3D [[Binding2]],
                Sampler sampler0 [[Binding3]],
                RWStructuredBuffer<float4> output [[Binding4]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] Astc4x4UnormTextureFormatPass final : public IComputeClass
{
public:
    constructor(BindGroup<Astc4x4UnormTextureFormatBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float2 uv = float2(0.25f, 0.5f);
        const float3 uvw = float3(0.25f, 0.5f, 0.75f);
        const uint3 coord3D = uint3(threadID.x, threadID.y, threadID.z);
        const float4 sampled2D = float4(bindGroup->texture2D->sample(bindGroup->sampler0, uv));
        const float4 sampledArray = float4(bindGroup->textureArray->sample(bindGroup->sampler0, uv, 0u));
        const float4 loaded3D = float4(bindGroup->texture3D->read(coord3D, 0u));
        const float4 sampled3D = float4(bindGroup->texture3D->sample(bindGroup->sampler0, uvw));
        bindGroup->output[threadID.x] = sampled2D + sampledArray + loaded3D + sampled3D;
    }
};

class Astc4x4UnormTextureFormatRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Texture<TextureFormat::ASTC4x4Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e3D> cloudVolume;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        cloudVolume = device->createTexture("AstcCloudVolume", 128, 128, 64, 8);
    }

    void render() override
    {
    }

    void destroy() override
    {
    }
};

#endif
