#ifndef GVM_THREE_WEBGLGEOMETRYCSG_HPP
#define GVM_THREE_WEBGLGEOMETRYCSG_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometryCsgTextureCapacity = 2u;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglGeometryCsgVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebglGeometryCsgObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglGeometryCsgInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglGeometryCsgMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebglGeometryCsgSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglGeometryCsgVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryCsgObjectData> objects,
        BufferComponent<WebglGeometryCsgInstanceData> instances,
        BufferComponent<WebglGeometryCsgMaterialData> materials,
        (TextureComponent<half4, WebglGeometryCsgTextureCapacity> textures))
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglGeometryCsgVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglGeometryCsgFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglGeometryCsgLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Writes the shared Scene Set depth prepass used by the CSG lighting phase. */
class WebglGeometryCsgShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the unique CSG Scene Set for depth-only ordering. */
    constructor(RenderSet<WebglGeometryCsgSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the same entity transform as the color pass. */
    WebglGeometryCsgVertexOutput vertex(
        WebglGeometryCsgVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryCsgObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryCsgInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglGeometryCsgVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Keeps the depth prepass color neutral while preserving its depth writes. */
    WebglGeometryCsgFrameBuffer fragment(WebglGeometryCsgVertexOutput inputValue)
    {
        WebglGeometryCsgFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.05f), half(1.0f));
        return frameBuffer;
    }
};

/** Draws opaque CSG geometry through the unique Scene RenderSet. */
class WebglGeometryCsgMainColorPass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglGeometryCsgSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglGeometryCsgVertexOutput vertex(
        WebglGeometryCsgVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryCsgObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryCsgInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglGeometryCsgVertexOutput outputValue;
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
    WebglGeometryCsgFrameBuffer fragment(
        WebglGeometryCsgVertexOutput inputValue)
    {
        const WebglGeometryCsgObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglGeometryCsgMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const float3 normalColor = materialData.baseColorAndFlags.xyz;
        const float3 srgb = float3(
            webglGeometryCsgLinearToSrgb(normalColor.x),
            webglGeometryCsgLinearToSrgb(normalColor.y),
            webglGeometryCsgLinearToSrgb(normalColor.z));
        WebglGeometryCsgFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebglGeometryCsgWireframeOverlayPass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebglGeometryCsgSceneRenderSet> sceneSet [[Slot0]])
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
    WebglGeometryCsgVertexOutput vertex(
        WebglGeometryCsgVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryCsgObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryCsgInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglGeometryCsgVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebglGeometryCsgFrameBuffer fragment(
        WebglGeometryCsgVertexOutput inputValue)
    {
        const WebglGeometryCsgObjectData objectData =
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
        const WebglGeometryCsgMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglGeometryCsgFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryCsgLinearToSrgb(materialData.baseColorAndFlags.x)),
            half(webglGeometryCsgLinearToSrgb(materialData.baseColorAndFlags.y)),
            half(webglGeometryCsgLinearToSrgb(materialData.baseColorAndFlags.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the three ordered geometry passes. */
class WebglGeometryCsgRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryCsgSceneRenderSet> sceneSet;
    RenderClass<WebglGeometryCsgShadowDepthPass> shadowDepthPass;
    RenderClass<WebglGeometryCsgMainColorPass> mainColorPass;
    RenderClass<WebglGeometryCsgWireframeOverlayPass> wireframeOverlayPass;
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
        sceneSet = device->createRenderSet<WebglGeometryCsgSceneRenderSet>();
        shadowDepthPass = device->createRenderClass<WebglGeometryCsgShadowDepthPass>(sceneSet);
        mainColorPass = device->createRenderClass<WebglGeometryCsgMainColorPass>(sceneSet);
        wireframeOverlayPass = device->createRenderClass<WebglGeometryCsgWireframeOverlayPass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglGeometryCsgColor", width, height, 1u);
        outputDepth = device->createTexture("WebglGeometryCsgDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryCsgFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.9882353f, 0.89411765f, 0.9254902f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglGeometryCsgFrameBuffer wireframeFrame;
        wireframeFrame.color = outputColor->createView();
        wireframeFrame.color.loadOp = LoadOp::Load;
        wireframeFrame.color.storeOp = StoreOp::Store;
        wireframeFrame.depth = outputDepth->createView();
        wireframeFrame.depth.depthLoadOp = LoadOp::Load;
        wireframeFrame.depth.depthStoreOp = StoreOp::Store;
        WebglGeometryCsgFrameBuffer shadowFrame = opaqueFrame;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglGeometryCsgShadowDepth", shadowFrame, shadowDepthPass())
            ->renderPass("WebglGeometryCsgMainColor", opaqueFrame, mainColorPass())
            ->renderPass("WebglGeometryCsgWireframeOverlay", wireframeFrame, wireframeOverlayPass())
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
