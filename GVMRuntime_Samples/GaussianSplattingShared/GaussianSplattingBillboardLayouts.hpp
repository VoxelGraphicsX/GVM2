#pragma once

/**
 * Stores one projected regular Gaussian billboard payload.
 *
 * Include this header inside namespace GsViewer for samples that render
 * opacity-only billboard payloads with GaussianSplattingBillboardRenderPass.
 */
struct ProjectedGaussianRender
{
    float4 centerOpacitySupportScale;
    float4 color;
    float4 quadAxis01;
};

/**
 * Carries regular Gaussian billboard attributes from the vertex stage to the fragment stage.
 */
struct GaussianRasterVertexOutput
{
    float4 pos [[Position]];
    float4 colorOpacity [[Attribute0]];
    float2 localFragPos [[Attribute1]];
};

/**
 * Defines the regular Gaussian billboard color attachment used by graphics samples.
 */
struct GaussianRasterFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::RGBA16Float> color;
};
