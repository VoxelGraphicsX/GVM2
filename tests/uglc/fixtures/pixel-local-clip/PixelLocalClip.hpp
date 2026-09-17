#ifndef UGLC_TEST_PIXEL_LOCAL_CLIP_HPP
#define UGLC_TEST_PIXEL_LOCAL_CLIP_HPP

#include "UGL.h"

using namespace UGL;

/** Carries fullscreen triangle data into the pixel-local producer pass. */
struct PixelLocalClipVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Declares a transient pixel-local input and a visible color target for clip testing. */
struct PixelLocalClipFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;

    ColorAttachment<TextureFormat::BGRA8Unorm> present;
};

/** Writes pixel-local albedo data consumed by the later clip pass. */
class PixelLocalClipGBufferPass final : public IRenderClass
{
public:
    /** Creates a stateless producer pass for the pixel-local clip fixture. */
    constructor()
    {
    }

private:
    /** Emits a fullscreen triangle and stable UV coordinates. */
    PixelLocalClipVertexOutput vertex(uint vertexID [[VertexID]])
    {
        PixelLocalClipVertexOutput outputValue;
        float2 p;
        if (vertexID == 0u)
        {
            p = float2(-1.0f, -1.0f);
        }
        else if (vertexID == 1u)
        {
            p = float2(3.0f, -1.0f);
        }
        else
        {
            p = float2(-1.0f, 3.0f);
        }
        outputValue.position = float4(p, 0.0f, 1.0f);
        outputValue.uv = p * 0.5f + float2(0.5f, 0.5f);
        return outputValue;
    }

    /** Stores a simple albedo value into the pixel-local attachment. */
    PixelLocalClipFrame fragment(PixelLocalClipVertexOutput inputValue)
    {
        PixelLocalClipFrame outputValue;
        outputValue.albedo = half4(inputValue.uv.x, inputValue.uv.y, 0.25f, 1.0f);
        return outputValue;
    }
};

/** Reads a pixel-local attachment, clips the invocation, and writes the final color. */
class PixelLocalClipLightingPass final : public IPixelLocalRenderClass
{
public:
    /** Creates a stateless pixel-local clip pass. */
    constructor()
    {
    }

private:
    /** Clips the current pixel invocation using scalar and vector clip operands. */
    PixelLocalClipFrame pixel(PixelLocalClipFrame inputValue [[PixelLocalInput]])
    {
        PixelLocalClipFrame outputValue;
        half4 albedoValue = inputValue.albedo.read();
        UGL::clip(float(albedoValue.x) - 0.2f);
        clip(float4(float(albedoValue.x) - 0.1f, float(albedoValue.y) - 0.1f, 1.0f, 1.0f));
        outputValue.present = half4(albedoValue.x, albedoValue.y, albedoValue.z, 1.0f);
        return outputValue;
    }
};

/** Drives the pixel-local clip fixture through the host renderPass lowering path. */
class PixelLocalClipRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue queue;
    PixelLocalClipFrame frame;
    RenderClass<PixelLocalClipGBufferPass> gbufferPass;
    RenderClass<PixelLocalClipLightingPass> lightingPass;

public:
    /** Creates the render classes used by the pixel-local clip fixture. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        queue = device->graphicsQueue(0);
        gbufferPass = device->createRenderClass<PixelLocalClipGBufferPass>();
        lightingPass = device->createRenderClass<PixelLocalClipLightingPass>();
    }

    /** Submits a two-phase pixel-local render pass with clip in the pixel phase. */
    void render() override
    {
        queue->renderPass("PixelLocalClip",
                          frame,
                          pixelLocalPass(gbufferPass(3u, 1u, 0u, 0u),
                                         nextPixelLocalPass(),
                                         lightingPass()));
    }

    /** Releases no resources because the fixture owns only generated handles. */
    void destroy() override
    {
    }
};

#endif
