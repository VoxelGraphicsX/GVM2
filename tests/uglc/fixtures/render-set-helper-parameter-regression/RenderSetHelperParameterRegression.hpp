#ifndef UGLC_TEST_RENDER_SET_HELPER_PARAMETER_REGRESSION_HPP
#define UGLC_TEST_RENDER_SET_HELPER_PARAMETER_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the vertex attributes needed to exercise RenderSet-backed vertex input generation. */
struct RenderSetHelperParameterRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

/** Carries position and debug values through the RenderSet helper-parameter regression pass. */
struct RenderSetHelperParameterRegressionVertexOutput
{
    float4 pos [[Position]];
    float4 debugValue [[Attribute0]];
};

/** Stores payload data read through RenderSet helper parameters in HLSL and MSL. */
struct RenderSetHelperParameterRegressionPayload
{
    float4 value;
    uint tag;
};

/** Defines the single color target used by the RenderSet helper-parameter regression pass. */
struct RenderSetHelperParameterRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Describes a RenderSet with texture, payload, vertex, and index components for helper-parameter lowering tests. */
struct RenderSetHelperParameterRegressionSet : public IRenderSet
{
    /** Declares the RenderSet components used by the helper global-resource rebinding regression. */
    constructor((TextureComponent<half4, 4> albedo), BufferComponent<RenderSetHelperParameterRegressionPayload> payloads, BufferComponent<RenderSetHelperParameterRegressionVertexInput> vertices [[RenderSetVertexBuffer]], BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

namespace RenderSetHelperParameterRegressionHelpers
{
    /** Reads one payload through a RenderSet parameter so HLSL can rebind it to global resources. */
    inline RenderSetHelperParameterRegressionPayload loadPayload(RenderSet<RenderSetHelperParameterRegressionSet> rs IN, uint entity)
    {
        return rs->payloads->get(entity, 0u);
    }

    /** Reads RenderEntity metadata through a RenderSet parameter so HLSL can use the global entity helper. */
    inline uint loadIndexCount(RenderSet<RenderSetHelperParameterRegressionSet> rs IN, uint entity)
    {
        return rs->getRenderEntityIndexCount(entity);
    }

    /** Reads a texture component through a RenderSet parameter to verify texture helper rebinding. */
    inline float4 loadAlbedo(RenderSet<RenderSetHelperParameterRegressionSet> rs IN, uint entity, uint slot)
    {
        auto albedoTexture = rs->albedo->get(entity, slot);
        return float4(albedoTexture->read(uint2(0u, 0u), 0u));
    }

    /** Forwards a RenderSet parameter to another helper so nested HLSL rebinding stays stable. */
    inline RenderSetHelperParameterRegressionPayload forwardPayload(RenderSet<RenderSetHelperParameterRegressionSet> rs IN, uint entity)
    {
        return loadPayload(rs, entity);
    }
} // namespace RenderSetHelperParameterRegressionHelpers

/** Exercises RenderSet helper parameters from a render pass that binds a single global RenderSet. */
class RenderSetHelperParameterRegressionPass final : public IRenderClass
{
public:
    /** Binds the RenderSet used by all helper-parameter calls in this pass. */
    constructor(RenderSet<RenderSetHelperParameterRegressionSet> renderSet [[Slot0]])
    {
    }

private:
    /** Builds vertex output from RenderSet helper calls that HLSL must specialize. */
    RenderSetHelperParameterRegressionVertexOutput vertex(RenderSetHelperParameterRegressionVertexInput inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]])
    {
        const RenderSetHelperParameterRegressionPayload payload = RenderSetHelperParameterRegressionHelpers::forwardPayload(renderSet, renderEntityID);
        const uint indexCount = RenderSetHelperParameterRegressionHelpers::loadIndexCount(renderSet, renderEntityID);
        const float4 albedo = RenderSetHelperParameterRegressionHelpers::loadAlbedo(renderSet, renderEntityID, 0u);

        RenderSetHelperParameterRegressionVertexOutput outputValue;
        outputValue.pos = inputValue.pos + payload.value;
        outputValue.debugValue = albedo + float4(float(indexCount + payload.tag), 0.0, 0.0, 0.0);
        return outputValue;
    }

    /** Writes the debug value produced by the vertex stage into the framebuffer. */
    RenderSetHelperParameterRegressionFrameBuffer fragment(RenderSetHelperParameterRegressionVertexOutput inputValue)
    {
        RenderSetHelperParameterRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.debugValue);
        return frameBuffer;
    }
};

#endif
