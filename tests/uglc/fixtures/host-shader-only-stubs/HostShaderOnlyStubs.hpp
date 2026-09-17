#ifndef UGLC_TEST_HOST_SHADER_ONLY_STUBS_HPP
#define UGLC_TEST_HOST_SHADER_ONLY_STUBS_HPP

#include "UGL.h"

using namespace UGL;

struct HostShaderOnlyBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> sampledTexture [[Binding0]],
                Sampler sampler0 [[Binding1]],
                RWTexture2D<TextureFormat::RGBA8Unorm> storageTexture [[Binding2]],
                RWStructuredBuffer<uint> values [[Binding3]])
    {
    }
};

namespace HostShaderOnlyHelpers
{
    inline float4 previewSample(BindGroup<HostShaderOnlyBindGroup> bindGroup, float2 uv)
    {
        return bindGroup->sampledTexture->sample(bindGroup->sampler0, uv);
    }

    inline float4 previewGather(BindGroup<HostShaderOnlyBindGroup> bindGroup, float2 uv)
    {
        return bindGroup->sampledTexture->gather(bindGroup->sampler0, uv);
    }

    inline float4 previewRead(BindGroup<HostShaderOnlyBindGroup> bindGroup, uint2 coord)
    {
        return float4(bindGroup->storageTexture->read(coord));
    }

    inline void previewWrite(BindGroup<HostShaderOnlyBindGroup> bindGroup, uint2 coord, float4 value)
    {
        bindGroup->storageTexture->write(coord, value);
    }

    inline void previewDimensions(BindGroup<HostShaderOnlyBindGroup> bindGroup, uint width, uint height)
    {
        bindGroup->storageTexture->getDimensions(width, height);
    }

    inline uint previewWave(uint value)
    {
        return WaveReadLaneAt(value, 0u) + WaveGetLaneCount();
    }
} // namespace HostShaderOnlyHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] HostShaderOnlyPass final : public IComputeClass
{
public:
    constructor(BindGroup<HostShaderOnlyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
