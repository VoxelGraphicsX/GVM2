#ifndef GVM_THREE_WEBGLGEOMETRYSPLINEEDITOR_HPP
#define GVM_THREE_WEBGLGEOMETRYSPLINEEDITOR_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglGeometrySplineEditorVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebglGeometrySplineEditorObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglGeometrySplineEditorInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglGeometrySplineEditorMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebglGeometrySplineEditorSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglGeometrySplineEditorVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometrySplineEditorObjectData> objects,
        BufferComponent<WebglGeometrySplineEditorInstanceData> instances,
        BufferComponent<WebglGeometrySplineEditorMaterialData> materials)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglGeometrySplineEditorVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglGeometrySplineEditorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglGeometrySplineEditorLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Writes the spline editor's shared Scene depth prepass. */
class WebglGeometrySplineEditorShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the unique spline Scene Set for depth ordering. */
    constructor(RenderSet<WebglGeometrySplineEditorSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the same Catmull-Rom ribbon transform as the color pass. */
    WebglGeometrySplineEditorVertexOutput vertex(
        WebglGeometrySplineEditorVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometrySplineEditorInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglGeometrySplineEditorVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits a neutral color while populating the depth attachment. */
    WebglGeometrySplineEditorFrameBuffer fragment(WebglGeometrySplineEditorVertexOutput inputValue)
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        // The TransformControls interaction plane is retained as an entity for
        // the single-RenderSet contract but is permanently invisible in r185.
        if (objectData.baseColorAndFlags.w < -0.5f)
            discard_fragment();
        WebglGeometrySplineEditorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.05f), half(1.0f));
        return frameBuffer;
    }
};

/** Draws opaque spline geometry through the unique Scene RenderSet. */
class WebglGeometrySplineEditorMainColorPass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglGeometrySplineEditorSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglGeometrySplineEditorVertexOutput vertex(
        WebglGeometrySplineEditorVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometrySplineEditorInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglGeometrySplineEditorVertexOutput outputValue;
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
    WebglGeometrySplineEditorFrameBuffer fragment(
        WebglGeometrySplineEditorVertexOutput inputValue)
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglGeometrySplineEditorMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        if (objectData.baseColorAndFlags.w < -0.5f)
        {
            discard_fragment();
        }
        const float3 normalColor = materialData.baseColorAndFlags.xyz;
        const float3 srgb = float3(
            webglGeometrySplineEditorLinearToSrgb(normalColor.x),
            webglGeometrySplineEditorLinearToSrgb(normalColor.y),
            webglGeometrySplineEditorLinearToSrgb(normalColor.z));
        WebglGeometrySplineEditorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebglGeometrySplineEditorControlOverlayPass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebglGeometrySplineEditorSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        // Curves, GridHelper, and TransformControls are screen overlays in
        // the source example.  The shared depth prepass contains the same
        // RenderSet geometry, so an unconditional read-only depth test keeps
        // the overlay visible without changing the public pass API.
        setDepthCompareFunction(CompareFunction::Always);
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
    WebglGeometrySplineEditorVertexOutput vertex(
        WebglGeometrySplineEditorVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometrySplineEditorInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglGeometrySplineEditorVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebglGeometrySplineEditorFrameBuffer fragment(
        WebglGeometrySplineEditorVertexOutput inputValue)
    {
        const WebglGeometrySplineEditorObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 0.5f)
        {
            discard_fragment();
        }
        const WebglGeometrySplineEditorMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglGeometrySplineEditorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometrySplineEditorLinearToSrgb(materialData.baseColorAndFlags.x)),
            half(webglGeometrySplineEditorLinearToSrgb(materialData.baseColorAndFlags.y)),
            half(webglGeometrySplineEditorLinearToSrgb(materialData.baseColorAndFlags.z)),
            half(materialData.baseColorAndFlags.w));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the three ordered geometry passes. */
class WebglGeometrySplineEditorRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometrySplineEditorSceneRenderSet> sceneSet;
    RenderClass<WebglGeometrySplineEditorShadowDepthPass> shadowDepthPass;
    RenderClass<WebglGeometrySplineEditorMainColorPass> mainColorPass;
    RenderClass<WebglGeometrySplineEditorControlOverlayPass> controlOverlayPass;
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
        sceneSet = device->createRenderSet<WebglGeometrySplineEditorSceneRenderSet>();
        shadowDepthPass = device->createRenderClass<WebglGeometrySplineEditorShadowDepthPass>(sceneSet);
        mainColorPass = device->createRenderClass<WebglGeometrySplineEditorMainColorPass>(sceneSet);
        controlOverlayPass = device->createRenderClass<WebglGeometrySplineEditorControlOverlayPass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglGeometrySplineEditorColor", width, height, 1u);
        outputDepth = device->createTexture("WebglGeometrySplineEditorDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebglGeometrySplineEditorFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.9411765f, 0.9411765f, 0.9411765f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglGeometrySplineEditorFrameBuffer wireframeFrame;
        wireframeFrame.color = outputColor->createView();
        wireframeFrame.color.loadOp = LoadOp::Load;
        wireframeFrame.color.storeOp = StoreOp::Store;
        wireframeFrame.depth = outputDepth->createView();
        wireframeFrame.depth.depthLoadOp = LoadOp::Load;
        wireframeFrame.depth.depthStoreOp = StoreOp::Store;
        WebglGeometrySplineEditorFrameBuffer shadowFrame = opaqueFrame;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglGeometrySplineEditorShadowDepth", shadowFrame, shadowDepthPass())
            ->renderPass("WebglGeometrySplineEditorMainColor", opaqueFrame, mainColorPass())
            ->renderPass("WebglGeometrySplineEditorControlOverlay", wireframeFrame, controlOverlayPass())
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
