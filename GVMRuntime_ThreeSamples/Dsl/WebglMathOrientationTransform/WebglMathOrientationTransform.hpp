#ifndef GVM_THREE_WEBGL_MATH_ORIENTATION_TRANSFORM_HPP
#define GVM_THREE_WEBGL_MATH_ORIENTATION_TRANSFORM_HPP

#include "UGL.h"

using namespace UGL;


/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglMathOrientationTransformVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebglMathOrientationTransformObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglMathOrientationTransformInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglMathOrientationTransformMaterialData
{
    float4 baseColorAndFlags;
};

/** Stores visibility and orientation-selection flags for the two geometry passes. */
struct WebglMathOrientationTransformRenderFlags
{
    uint4 values;
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebglMathOrientationSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglMathOrientationTransformVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMathOrientationTransformObjectData> objects,
        BufferComponent<WebglMathOrientationTransformInstanceData> instances,
        BufferComponent<WebglMathOrientationTransformMaterialData> materials,
        BufferComponent<WebglMathOrientationTransformRenderFlags> renderFlags)
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglMathOrientationTransformVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglMathOrientationTransformFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglMathOrientationTransformLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebglMathOrientationOpaquePass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet for the opaque MeshNormal and MeshBasic phases. */
    constructor(RenderSet<WebglMathOrientationSceneRenderSet> sceneSet [[Slot0]])
    {
        // UGL's clip-space Y flip reverses the rasterizer winding relative to
        // Three.js's WebGL path, so Front selects the same visible cone sides.
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMathOrientationTransformVertexOutput vertex(
        WebglMathOrientationTransformVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMathOrientationTransformObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMathOrientationTransformInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMathOrientationTransformVertexOutput outputValue;
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
    WebglMathOrientationTransformFrameBuffer fragment(
        WebglMathOrientationTransformVertexOutput inputValue)
    {
        const WebglMathOrientationTransformObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglMathOrientationTransformMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            discard_fragment();
        }
        const float3 normalColor = inputValue.entityID == 0u
            ? inputValue.viewNormal * 0.5f + float3(0.5f)
            : materialData.baseColorAndFlags.xyz;
        WebglMathOrientationTransformFrameBuffer frameBuffer;
        // MeshNormalMaterial writes its normalized view-space color directly;
        // Three's WebGL RGBA8 target does not apply an additional transfer to
        // this material.  Applying an sRGB curve here would brighten the cone
        // channels (notably Z) and diverge from the reference capture.
        frameBuffer.color = half4(half3(normalColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebglMathOrientationWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebglMathOrientationSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        // MeshBasicMaterial keeps its default depthWrite=true even when
        // transparent.  Keeping the write enabled preserves the renderer's
        // front-to-back coverage when triangle edges overlap.
        setDepthWriteEnabled(true);
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
    WebglMathOrientationTransformVertexOutput vertex(
        WebglMathOrientationTransformVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMathOrientationTransformObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMathOrientationTransformInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMathOrientationTransformVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates barycentric edge coverage and the locked 0.3 wireframe alpha. */
    WebglMathOrientationTransformFrameBuffer fragment(
        WebglMathOrientationTransformVertexOutput inputValue)
    {
        const WebglMathOrientationTransformObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w < 0.5f)
        {
            discard_fragment();
        }
        WebglMathOrientationTransformFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.8f), half(0.3f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
class WebglMathOrientationTransformRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMathOrientationSceneRenderSet> sceneSet;
    RenderClass<WebglMathOrientationOpaquePass> opaquePass;
    RenderClass<WebglMathOrientationWireframePass> wireframePass;
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
        sceneSet = device->createRenderSet<WebglMathOrientationSceneRenderSet>();
        opaquePass = device->createRenderClass<WebglMathOrientationOpaquePass>(sceneSet);
        wireframePass = device->createRenderClass<WebglMathOrientationWireframePass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMathOrientationTransformColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMathOrientationTransformDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebglMathOrientationTransformFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglMathOrientationTransformFrameBuffer wireframeFrame;
        wireframeFrame.color = outputColor->createView();
        wireframeFrame.color.loadOp = LoadOp::Load;
        wireframeFrame.color.storeOp = StoreOp::Store;
        wireframeFrame.depth = outputDepth->createView();
        wireframeFrame.depth.depthLoadOp = LoadOp::Load;
        wireframeFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglMathOrientationTransformOpaque", opaqueFrame, opaquePass())
            ->renderPass("WebglMathOrientationTransformWireframe", wireframeFrame, wireframePass())
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
