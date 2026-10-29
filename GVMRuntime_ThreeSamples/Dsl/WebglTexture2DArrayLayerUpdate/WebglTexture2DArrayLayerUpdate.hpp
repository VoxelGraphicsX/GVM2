#pragma once

#include "UGL.h"

using namespace UGL;

static const uint WebglTexture2DArrayLayerUpdateTextureCapacity = 4u;

/** Stores one plane corner in normalized local coordinates. */
struct WebglTexture2DArrayLayerUpdateVertex
{
    float4 positionAndUv [[Attribute0]];
};

/** Stores the camera projection shared by the texture-array entity. */
struct WebglTexture2DArrayLayerUpdateObjectData
{
    float4 projectionScaleAndReserved;
};

/** Stores the vertical placement and texture slot for one plane instance. */
struct WebglTexture2DArrayLayerUpdateInstanceData
{
    float4 verticalOffsetAndTextureSlot;
};

/** Stores the identity tint required by the Scene material component. */
struct WebglTexture2DArrayLayerUpdateMaterialData
{
    float4 tint;
};

/** Defines the only Scene RenderSet used by the three texture-array planes. */
struct WebglTexture2DArrayLayerUpdateSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, material, and texture components. */
    constructor(
        BufferComponent<WebglTexture2DArrayLayerUpdateVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglTexture2DArrayLayerUpdateObjectData> objects,
        BufferComponent<WebglTexture2DArrayLayerUpdateInstanceData> instances,
        BufferComponent<WebglTexture2DArrayLayerUpdateMaterialData> materials,
        (TextureComponent<half4, WebglTexture2DArrayLayerUpdateTextureCapacity> textures))
    {
    }
};

/** Binds the linear sampler used for each decoded movie layer. */
struct WebglTexture2DArrayLayerUpdateSamplerResources final : public IBindGroup
{
    /** Declares the immutable sampler layout. */
    constructor(Sampler movieSampler [[Binding0]])
    {
    }
};

/** Carries projected coordinates and RenderSet identity to the fragment stage. */
struct WebglTexture2DArrayLayerUpdateVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    uint2 entityAndTextureSlot [[Attribute1]];
};

/** Defines the single-sample RGBA8 readback attachment. */
struct WebglTexture2DArrayLayerUpdateFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws all three planes through the RenderSet indexed-indirect entry. */
class WebglTexture2DArrayLayerUpdateScenePass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and the movie sampler. */
    constructor(
        RenderSet<WebglTexture2DArrayLayerUpdateSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglTexture2DArrayLayerUpdateSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Projects one shared quad for the selected RenderEntity instance. */
    WebglTexture2DArrayLayerUpdateVertexOutput vertex(
        WebglTexture2DArrayLayerUpdateVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglTexture2DArrayLayerUpdateObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglTexture2DArrayLayerUpdateInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebglTexture2DArrayLayerUpdateMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        WebglTexture2DArrayLayerUpdateVertexOutput outputValue;
        outputValue.position = float4(
            inputValue.positionAndUv.x * objectData.projectionScaleAndReserved.x,
            (inputValue.positionAndUv.y + instanceData.verticalOffsetAndTextureSlot.x) *
                objectData.projectionScaleAndReserved.y,
            0.5f + materialData.tint.x * 0.0f,
            1.0f);
        outputValue.uv = inputValue.positionAndUv.zw;
        outputValue.entityAndTextureSlot = uint2(
            renderEntityID,
            uint(instanceData.verticalOffsetAndTextureSlot.y + 0.5f));
        return outputValue;
    }

    /** Samples the instance-selected TextureComponent slot without a standalone array binding. */
    WebglTexture2DArrayLayerUpdateFrameBuffer fragment(
        WebglTexture2DArrayLayerUpdateVertexOutput inputValue)
    {
        auto movieLayer = sceneSet->textures->get(
            inputValue.entityAndTextureSlot.x,
            inputValue.entityAndTextureSlot.y);
        const float4 color = float4(movieLayer->sample(
            samplerResources->movieSampler,
            inputValue.uv));
        WebglTexture2DArrayLayerUpdateFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the dedicated texture-layer RenderSet and deterministic output target. */
class WebglTexture2DArrayLayerUpdateRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglTexture2DArrayLayerUpdateSceneRenderSet> sceneSet;
    Sampler movieSampler;
    BindGroup<WebglTexture2DArrayLayerUpdateSamplerResources> samplerResources;
    RenderClass<WebglTexture2DArrayLayerUpdateScenePass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and immutable linear clamp sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglTexture2DArrayLayerUpdateSceneRenderSet>();
        movieSampler = device->createSampler({
            .label = "WebglTexture2DArrayLayerUpdateSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        samplerResources =
            device->createBindGroup<WebglTexture2DArrayLayerUpdateSamplerResources>(
                movieSampler);
        mainPass = device->createRenderClass<WebglTexture2DArrayLayerUpdateScenePass>(
            sceneSet,
            samplerResources);
        outputColor = device->createTexture(
            "WebglTexture2DArrayLayerUpdateOutput",
            width,
            height,
            1u);
    }

    /** Recreates the final target for the explicit host extent. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        device->freeTexture(outputColor);
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglTexture2DArrayLayerUpdateOutput",
            width,
            height,
            1u);
    }

    /** Applies pending entity allocations and draws all instances once. */
    void render() override
    {
        sceneSet->update();
        WebglTexture2DArrayLayerUpdateFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglTexture2DArrayLayerUpdateMain",
                frameBuffer,
                mainPass())
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final texture for strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the RenderSet and output resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
    }
};
