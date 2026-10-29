#ifndef GVM_THREE_SIMPLE_RENDER_CLASS_HPP
#define GVM_THREE_SIMPLE_RENDER_CLASS_HPP

#include "UGL.h"

using namespace UGL;

/** Carries the procedural single-object vertex position and color to the fragment stage. */
struct SimpleRenderClassVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
};

/** Defines the deterministic RGBA8 output used by the simple RenderClass slice. */
struct SimpleRenderClassFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws one non-instanced, hierarchy-free, single-material triangle without a RenderSet. */
class SimpleRenderClassScenePass final : public IRenderClass
{
public:
    /** Creates the stateless scene pass without resource bindings. */
    constructor()
    {
    }

private:
    /** Generates the single object's triangle geometry and material color from VertexID. */
    SimpleRenderClassVertexOutput vertex(uint vertexID [[VertexID]])
    {
        float2 position = float2(-0.78f, -0.62f);
        float3 color = float3(0.95f, 0.20f, 0.12f);
        if (vertexID == 1u)
        {
            position = float2(0.0f, 0.76f);
            color = float3(0.12f, 0.92f, 0.32f);
        }
        else if (vertexID == 2u)
        {
            position = float2(0.78f, -0.62f);
            color = float3(0.16f, 0.32f, 0.98f);
        }

        SimpleRenderClassVertexOutput outputValue;
        outputValue.position = float4(position, 0.0f, 1.0f);
        outputValue.color = color;
        return outputValue;
    }

    /** Writes the interpolated color of the slice's only material. */
    SimpleRenderClassFrameBuffer fragment(SimpleRenderClassVertexOutput inputValue)
    {
        SimpleRenderClassFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns and submits the ordinary RenderClass slice to a deterministic offscreen target. */
class SimpleRenderClassRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    RenderClass<SimpleRenderClassScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the ordinary scene RenderClass while deferring output allocation to explicit configuration. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        scenePass = device->createRenderClass<SimpleRenderClassScenePass>();
    }

    /** Allocates the deterministic readback target at the explicit host-requested extent. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("SimpleRenderClassRGBA8", width, height, 1u);
    }

    /** Draws exactly one ordinary RenderClass object and presents its offscreen result. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        SimpleRenderClassFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.015, 0.025, 0.055, 1.0};

        graphicsQueue->renderPass("SimpleRenderClassScene", frameBuffer, scenePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output for test-only readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the deterministic output texture after host capture completes. */
    void destroy() override
    {
        device->freeTexture(outputTexture);
    }
};

#endif
