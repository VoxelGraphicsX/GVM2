#ifndef UGLC_TEST_TEXTURE3D_RWTEXTURE3D_LOWERING_HPP
#define UGLC_TEST_TEXTURE3D_RWTEXTURE3D_LOWERING_HPP

#include "UGL.h"

using namespace UGL;

struct Texture3DRWTexture3DBindGroup final : public IBindGroup
{
    constructor(Texture3D<float4> volumeTexture [[Binding0]],
                RWTexture3D<TextureFormat::RGBA8Unorm> outputVolume [[Binding1]],
                Sampler sampler0 [[Binding2]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] Texture3DRWTexture3DPass final : public IComputeClass
{
public:
    constructor(BindGroup<Texture3DRWTexture3DBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint width = 0u;
        uint height = 0u;
        uint depth = 0u;
        bindGroup->volumeTexture->getDimensions(width, height, depth);

        const uint3 coord = uint3(threadID.x, threadID.y, threadID.z);
        const float3 uvw = float3(0.25f, 0.5f, 0.75f);
        const float4 loadedValue = bindGroup->volumeTexture->read(coord, 0u);
        const float4 sampledValue = bindGroup->volumeTexture->sample(bindGroup->sampler0, uvw);
        const float4 levelValue = bindGroup->volumeTexture->sampleLevel(bindGroup->sampler0, uvw, 0.0f);
        const float4 gradValue = bindGroup->volumeTexture->sampleGrad(bindGroup->sampler0, uvw, float3(0.01f, 0.0f, 0.0f), float3(0.0f, 0.01f, 0.0f));
        bindGroup->outputVolume->write(coord, loadedValue + sampledValue + levelValue + gradValue);
        const half4 storedValue = bindGroup->outputVolume->read(coord);
        bindGroup->outputVolume->write(coord, storedValue);
    }
};

#endif
