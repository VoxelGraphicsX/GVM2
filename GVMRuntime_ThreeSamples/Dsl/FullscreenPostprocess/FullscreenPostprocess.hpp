#ifndef GVM_THREE_FULLSCREEN_POSTPROCESS_HPP
#define GVM_THREE_FULLSCREEN_POSTPROCESS_HPP

#include "UGL.h"

using namespace UGL;

/** Carries procedural scene geometry values into the offscreen fragment stage. */
struct FullscreenPostprocessSceneVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
};

/** Carries fullscreen triangle coordinates into the screen fragment stage. */
struct FullscreenPostprocessScreenVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the shared RGBA8 attachment contract for scene and screen passes. */
struct FullscreenPostprocessFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds the offscreen scene texture and sampler used only by the screen pass. */
struct FullscreenPostprocessScreenBindGroup final : public IBindGroup
{
    /** Declares the screen pass texture and sampler bindings. */
    constructor(Texture2D<float4> sceneTexture [[Binding0]], Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Draws one simple object into the intermediate scene texture without a RenderSet. */
class FullscreenPostprocessScenePass final : public IRenderClass
{
public:
    /** Creates the stateless one-object scene pass. */
    constructor()
    {
    }

private:
    /** Generates one deterministic triangle using the ordinary RenderClass VertexID path. */
    FullscreenPostprocessSceneVertexOutput vertex(uint vertexID [[VertexID]])
    {
        float2 position = float2(-0.72f, -0.66f);
        float3 color = float3(0.90f, 0.12f, 0.16f);
        if (vertexID == 1u)
        {
            position = float2(0.0f, 0.72f);
            color = float3(0.08f, 0.82f, 0.34f);
        }
        else if (vertexID == 2u)
        {
            position = float2(0.72f, -0.66f);
            color = float3(0.10f, 0.28f, 0.92f);
        }

        FullscreenPostprocessSceneVertexOutput outputValue;
        outputValue.position = float4(position, 0.0f, 1.0f);
        outputValue.color = color;
        return outputValue;
    }

    /** Writes the one-object scene color into the offscreen texture. */
    FullscreenPostprocessFrameBuffer fragment(FullscreenPostprocessSceneVertexOutput inputValue)
    {
        FullscreenPostprocessFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Applies a deterministic fullscreen color transform without binding any RenderSet. */
class FullscreenPostprocessScreenPass final : public IRenderClass
{
public:
    /** Binds only the scene texture and sampler required by the screen pass. */
    constructor(BindGroup<FullscreenPostprocessScreenBindGroup> screenBindGroup [[Slot0]])
    {
    }

private:
    /** Generates a fullscreen triangle and its corresponding texture coordinates. */
    FullscreenPostprocessScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        FullscreenPostprocessScreenVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.texCoord = texCoord;
        return outputValue;
    }

    /** Samples the offscreen scene and applies the screen-only color transform. */
    FullscreenPostprocessFrameBuffer fragment(FullscreenPostprocessScreenVertexOutput inputValue)
    {
        const float4 sceneValue = screenBindGroup->sceneTexture->sample(
            screenBindGroup->sceneSampler,
            inputValue.texCoord);
        const float3 transformedColor = sceneValue.xyz * float3(0.72f, 0.88f, 1.06f) +
                                        float3(inputValue.texCoord.x * 0.06f,
                                               inputValue.texCoord.y * 0.035f,
                                               0.018f);
        FullscreenPostprocessFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(transformedColor), half(1.0f));
        return frameBuffer;
    }
};

/** Runs one offscreen scene pass followed by one RenderSet-free fullscreen screen pass. */
class FullscreenPostprocessRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    RenderClass<FullscreenPostprocessScenePass> scenePass;
    RenderClass<FullscreenPostprocessScreenPass> screenPass;
    BindGroup<FullscreenPostprocessScreenBindGroup> screenBindGroup;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Sampler sceneSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the scene pass and sampler while deferring extent-dependent targets to configuration. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        scenePass = device->createRenderClass<FullscreenPostprocessScenePass>();
        sceneSampler = device->createSampler({
            .label = "FullscreenPostprocessSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
    }

    /** Allocates both targets and binds the scene output to the RenderSet-free screen pass. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture("FullscreenPostprocessSceneRGBA8", width, height, 1u);
        outputTexture = device->createTexture("FullscreenPostprocessOutputRGBA8", width, height, 1u);
        screenBindGroup = device->createBindGroup<FullscreenPostprocessScreenBindGroup>(
            sceneTexture->createView(),
            sceneSampler);
        screenPass = device->createRenderClass<FullscreenPostprocessScreenPass>(screenBindGroup);
    }

    /** Submits the offscreen scene and fullscreen screen passes before presenting the final texture. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();

        FullscreenPostprocessFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = sceneTexture->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {0.025, 0.045, 0.085, 1.0};

        FullscreenPostprocessFrameBuffer screenFrameBuffer;
        screenFrameBuffer.color = outputTexture->createView();
        screenFrameBuffer.color.loadOp = LoadOp::Clear;
        screenFrameBuffer.color.storeOp = StoreOp::Store;
        screenFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        graphicsQueue->renderPass(
                "FullscreenPostprocessScene",
                sceneFrameBuffer,
                scenePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "FullscreenPostprocessScreen",
                screenFrameBuffer,
                screenPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 texture for test-only readback. */
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

    /** Releases the intermediate and final textures after host capture completes. */
    void destroy() override
    {
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
