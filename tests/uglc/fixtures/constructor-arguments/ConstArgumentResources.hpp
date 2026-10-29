#pragma once
#include "UGL.h"
using namespace UGL;

/** Supplies an ordinary uniform value whose shader access remains readonly. */
struct ConstArgumentCamera { float4x4 transform; };

/** Updates a writable scalar resource or local value using the existing value-spelled output mode. */
inline void writeConstArgumentOutput(uint value OUT) { value = 7; }

/** Combines ordinary input values with resource handles that retain their content permissions. */
struct ConstArgumentMaterial
{
    float scale;
    float4x4 transform;
    RWStructuredBuffer<uint4> output;
    RWTexture2D<TextureFormat::RGBA8Unorm> texture;
    Texture2D<float4> sampled;
    Sampler sampler;
    UniformBuffer<ConstArgumentCamera> camera;

    /** Changes ordinary fields only when this material is an invocation-local copy. */
    void changeScale() { scale += 1; }

    /** Writes resource contents while preserving this input object's ordinary values and handles. */
    void store(uint index) const
    {
        output[index].x = uint(scale);
        output[index][1] = 3;
        atomicAdd(output[index].x, 1u);
        writeConstArgumentOutput(output[index].z);
        auto local = transform[0];
        local[0] = scale;
        const float4 projected = mul(camera->transform, float4(1, 2, 3, 1));
        const half4 color = half4(sampled->sample(sampler, float2(0.5f, 0.5f)));
        texture->write(uint2(index, 0), color);
    }
};

/** Exercises readonly mixed structures, local copies, atomics, and resource writes. */
class [[LocalWorkGroupSize(1, 1, 1)]] ConstArgumentResources final : public IComputeClass
{
public:
    /** Receives a mixed material. */
    constructor(ConstArgumentMaterial material [[Args0]]) {}
private:
    /** Keeps resource stores legal after first invoking the same method on a local copy. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = material;
        local.changeScale();
        local.store(id.x);
        material.store(id.x);
    }
};
