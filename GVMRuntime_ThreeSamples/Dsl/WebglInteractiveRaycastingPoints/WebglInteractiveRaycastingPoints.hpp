#ifndef GVM_THREE_WEBGL_INTERACTIVE_RAYCASTING_POINTS_HPP
#define GVM_THREE_WEBGL_INTERACTIVE_RAYCASTING_POINTS_HPP

#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebglInteractiveRaycastingPoints.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one normalized triangle-list vertex packed by the host adapter. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
    float2 uv [[Attribute2]];
};

/** Stores one entity transform and material index in the Scene RenderSet. */
struct ThreeBasicObjectData
{
    float4 offsetAndScale;
    uint4 materialAndFlags;
};

/** Stores one per-instance transform and tint selected by the entity builtin. */
struct ThreeBasicInstanceData
{
    float4 offsetAndScale;
    float4 tint;
};

/** Stores one material color and the private semantic mode selected by C++. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglInteractiveRaycastingPointsSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, and material storage. */
    constructor(BufferComponent<ThreeBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ThreeBasicObjectData> objects,
                BufferComponent<ThreeBasicInstanceData> instances,
                BufferComponent<ThreeBasicMaterialData> materials)
    {
    }
};

/** Defines the RGBA8 color and depth attachments used for deterministic capture. */
struct WebglInteractiveRaycastingPointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader contains the common Three-compatible vertex-color, lighting,
 * clipping, and deterministic animation operations used by this wave.
 */
class WebglInteractiveRaycastingPointsScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<WebglInteractiveRaycastingPointsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves per-entity and per-instance data through the RenderEntity builtins. */
    ThreeBasicVertexOutput vertex(
        ThreeBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const ThreeBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        ThreeBasicVertexOutput outputValue;
        const float2 localPosition = inputValue.position.xy * objectData.offsetAndScale.zw;
        outputValue.position = float4(
            localPosition * instanceData.offsetAndScale.zw
                + objectData.offsetAndScale.xy
                + instanceData.offsetAndScale.xy,
            inputValue.position.z,
            1.0f);
        outputValue.color = inputValue.color * instanceData.tint;
        outputValue.entityID = renderEntityID;
        outputValue.uv = inputValue.uv.xy;
        return outputValue;
    }

    /** Applies the material component and a stable linear-to-sRGB transfer. */
    WebglInteractiveRaycastingPointsFrameBuffer fragment(
        ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            objectData.materialAndFlags.x);
        // PointsMaterial's fragment shader keeps only the circular point
        // sprite footprint.  The host expands points to ordinary triangles,
        // so this explicit UV test preserves that core GPU semantic without
        // using native point primitives.
        const float2 pointCoordinate = inputValue.uv;
        if (objectData.materialAndFlags.y != 0u &&
            length(pointCoordinate - float2(0.5f)) > 0.5f)
            discard_fragment();
        const float3 linearColor = saturate(inputValue.color.xyz * materialData.baseColor.xyz);
        const float3 srgbColor = float3(
            linearColor.x <= 0.0031308f
                ? linearColor.x * 12.92f
                : pow(linearColor.x, 0.41666f) * 1.055f - 0.055f,
            linearColor.y <= 0.0031308f
                ? linearColor.y * 12.92f
                : pow(linearColor.y, 0.41666f) * 1.055f - 0.055f,
            linearColor.z <= 0.0031308f
                ? linearColor.z * 12.92f
                : pow(linearColor.z, 0.41666f) * 1.055f - 0.055f);
        WebglInteractiveRaycastingPointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebglInteractiveRaycastingPointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractiveRaycastingPointsSceneRenderSet> sceneSet;
    RenderClass<WebglInteractiveRaycastingPointsScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        outputDepth;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates this shard's RenderSet and its generated Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractiveRaycastingPointsSceneRenderSet>();
        scenePass = device->createRenderClass<WebglInteractiveRaycastingPointsScenePass>(sceneSet);
    }

    /** Recreates the explicit single-sample capture attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("ThreeBasicRenderSetColor", width, height, 1u);
        outputDepth = device->createTexture("ThreeBasicRenderSetDepth", width, height, 1u);
    }

    /** Applies pending RenderSet commands and submits one indexed-indirect Scene pass. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglInteractiveRaycastingPointsFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("ThreeBasicRenderSetScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the explicit capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicit capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the single-sample attachments and RenderSet after capture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglInteractiveRaycastingPointsRenderer
#undef WebglInteractiveRaycastingPointsFrameBuffer
#undef WebglInteractiveRaycastingPointsScenePass
#undef WebglInteractiveRaycastingPointsSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
