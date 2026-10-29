#ifndef UGLC_TEST_RENDER_SET_BUFFER_CLAMP_REGRESSION_HPP
#define UGLC_TEST_RENDER_SET_BUFFER_CLAMP_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct RenderSetBufferClampRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

struct RenderSetBufferClampRegressionInner
{
    float4 basis[2];
    uint4 mask;
};

struct RenderSetBufferClampRegressionPayload
{
    RenderSetBufferClampRegressionInner inner;
    uint tag;
    float2 uv;
};

struct RenderSetBufferClampRegressionSet : public IRenderSet
{
    constructor(BufferComponent<RenderSetBufferClampRegressionPayload> payloads,
                BufferComponent<float4> transforms,
                BufferComponent<RenderSetBufferClampRegressionVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

struct RenderSetBufferClampRegressionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(4, 1, 1)]] RenderSetBufferClampRegressionPass final : public IComputeClass
{
public:
    constructor(RenderSet<RenderSetBufferClampRegressionSet> renderSet [[Slot0]],
                BindGroup<RenderSetBufferClampRegressionBindGroup> bindGroup [[Slot1]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint entity = threadID.x;
        const uint hugeSubIndex = threadID.x + 999999u;
        const RenderSetBufferClampRegressionPayload rawPayload = renderSet->payloads->getRaw(hugeSubIndex);
        const bool payloadValid = renderSet->payloads->checkValid(entity);
        const RenderSetBufferClampRegressionPayload payload = renderSet->payloads->get(entity, hugeSubIndex);
        const uint localIndex = renderSet->indices->get(entity, hugeSubIndex);
        const RenderSetBufferClampRegressionVertexInput vertexValue = renderSet->vertices->get(entity, localIndex + hugeSubIndex);
        const float4 transformValue = renderSet->transforms->get(entity, hugeSubIndex);

        uint value = rawPayload.tag + payload.tag + localIndex + uint(vertexValue.pos.x) + uint(transformValue.x);
        value += payloadValid ? 1u : 0u;
        value += rawPayload.inner.mask.x;
        bindGroup->values[entity] = value + uint(payload.inner.basis[0].x) + uint(payload.uv.x);
    }
};

#endif
