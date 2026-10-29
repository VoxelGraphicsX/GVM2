#ifndef GVM_THREE_WEBGPULIGHTSDYNAMIC_HPP
#define GVM_THREE_WEBGPULIGHTSDYNAMIC_HPP

#include "UGL.h"

using namespace UGL;

/** Describes one dynamic light color, range, and intensity. */
struct WebgpuLightsDynamicLightData
{
    float4 colorAndRange;
};

/** Stores visibility and phase flags used by the selective redraws. */
struct WebgpuLightsDynamicRenderFlags
{
    uint4 values;
};

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebgpuLightsDynamicVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebgpuLightsDynamicObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebgpuLightsDynamicInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebgpuLightsDynamicMaterialData
{
    float4 baseColorAndFlags;
    float4 lightColorAndRange;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebgpuLightsDynamicSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebgpuLightsDynamicVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuLightsDynamicObjectData> objects,
        BufferComponent<WebgpuLightsDynamicInstanceData> instances,
        BufferComponent<WebgpuLightsDynamicMaterialData> materials,
        BufferComponent<WebgpuLightsDynamicLightData> lightData,
        BufferComponent<WebgpuLightsDynamicRenderFlags> renderFlags)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebgpuLightsDynamicVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebgpuLightsDynamicFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webgpuLightsDynamicLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebgpuLightsDynamicMainPass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebgpuLightsDynamicSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebgpuLightsDynamicVertexOutput vertex(
        WebgpuLightsDynamicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuLightsDynamicObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuLightsDynamicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuLightsDynamicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float3 transformedNormal = mul(objectData.normalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebgpuLightsDynamicFrameBuffer fragment(
        WebgpuLightsDynamicVertexOutput inputValue)
    {
        const WebgpuLightsDynamicObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuLightsDynamicMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const float3 baseColor = inputValue.entityID == 0u
            ? inputValue.viewNormal * 0.5f + float3(0.5f)
            : materialData.baseColorAndFlags.xyz;
        const float lightFactor = max(
            dot(normalize(inputValue.viewNormal), normalize(
                float3(0.35f, 0.65f, 0.7f))), 0.0f);
        const float3 dynamicLight = materialData.lightColorAndRange.xyz *
            (lightFactor * materialData.lightColorAndRange.w);
        const float3 normalColor = baseColor * (0.18f + dynamicLight);
        const float3 srgb = float3(
            webgpuLightsDynamicLinearToSrgb(normalColor.x),
            webgpuLightsDynamicLinearToSrgb(normalColor.y),
            webgpuLightsDynamicLinearToSrgb(normalColor.z));
        WebgpuLightsDynamicFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebgpuLightsDynamicWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebgpuLightsDynamicSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Reuses the exact object/instance transform path of the opaque pass. */
    WebgpuLightsDynamicVertexOutput vertex(
        WebgpuLightsDynamicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuLightsDynamicObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuLightsDynamicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuLightsDynamicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebgpuLightsDynamicFrameBuffer fragment(
        WebgpuLightsDynamicVertexOutput inputValue)
    {
        const WebgpuLightsDynamicObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 0.5f)
        {
            discard_fragment();
        }
        const float edge = min(inputValue.barycentric.x,
                               min(inputValue.barycentric.y, inputValue.barycentric.z));
        if (edge > 0.035f)
        {
            discard_fragment();
        }
        WebgpuLightsDynamicFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.8f), half(0.3f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
class WebgpuLightsDynamicRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuLightsDynamicSceneRenderSet> sceneSet;
    RenderClass<WebgpuLightsDynamicMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates this example's unique RenderSet and both generated RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuLightsDynamicSceneRenderSet>();
        mainPass = device->createRenderClass<WebgpuLightsDynamicMainPass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuLightsDynamicColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuLightsDynamicDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebgpuLightsDynamicFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuLightsDynamicMain", opaqueFrame, mainPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used by deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and single-sample targets. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
