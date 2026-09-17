#ifndef GVM_THREE_MISC_ANIMATION_KEYS_HPP
#define GVM_THREE_MISC_ANIMATION_KEYS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one box vertex or one screen-expanded axis segment corner. */
struct MiscAnimationKeysVertex
{
    float4 position [[Attribute0]];
    float4 lineEnd [[Attribute1]];
    float4 startColor [[Attribute2]];
    float4 endColor [[Attribute3]];
    float4 lineCorner [[Attribute4]];
};

/** Stores one entity transform, projection, viewport, and material phase. */
struct MiscAnimationKeysObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
};

/** Stores the mandatory non-instanced component value. */
struct MiscAnimationKeysInstanceData
{
    float4 reserved;
};

/** Stores one MeshBasic or vertex-color material value. */
struct MiscAnimationKeysMaterialData
{
    float4 colorAndOpacity;
};

/** Selects the opaque helper or transparent box material phase. */
struct MiscAnimationKeysRenderFlagData
{
    float4 phase;
};

/** Defines the unique two-entity Scene RenderSet for the keyframe example. */
struct MiscAnimationKeysSceneRenderSet : public IRenderSet
{
    /** Declares packed box, axis, object, instance, and material components. */
    constructor(
        BufferComponent<MiscAnimationKeysVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<MiscAnimationKeysObjectData> objects,
        BufferComponent<MiscAnimationKeysInstanceData> instances,
        BufferComponent<MiscAnimationKeysMaterialData> materials,
        BufferComponent<MiscAnimationKeysRenderFlagData> renderFlags)
    {
    }
};

/** Carries entity phase, interpolated color, and identity to fragments. */
struct MiscAnimationKeysVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float phase [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the ordinary single-sample Scene attachments. */
struct MiscAnimationKeysFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear working-space channel to the browser output transfer. */
float miscAnimationKeysLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms a box vertex or expands one axis segment in screen space. */
MiscAnimationKeysVertexOutput miscAnimationKeysTransformVertex(
    IN RenderSet<MiscAnimationKeysSceneRenderSet> sceneSet,
    MiscAnimationKeysVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const MiscAnimationKeysObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const MiscAnimationKeysInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const MiscAnimationKeysRenderFlagData renderFlag =
        sceneSet->renderFlags->get(renderEntityID, 0u);
    const float phase = renderFlag.phase.x;
    float4 localPosition =
        inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
    float4 viewPosition = mul(objectData.modelView, localPosition);
    float4 clipPosition = mul(objectData.projection, viewPosition);
    float3 color = inputValue.startColor.xyz;
    if (phase > 0.5f)
    {
        const float4 endView = mul(objectData.modelView, inputValue.lineEnd);
        const float4 endClip = mul(objectData.projection, endView);
        const float2 startNdc = clipPosition.xy / clipPosition.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 direction =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 lineNormal =
            float2(-direction.y, direction.x) /
            max(length(direction), 0.0001f);
        const bool useEnd = inputValue.lineCorner.x > 0.5f;
        clipPosition = useEnd ? endClip : clipPosition;
        clipPosition.xy +=
            lineNormal * inputValue.lineCorner.y /
            objectData.viewport.xy * clipPosition.w;
        color = lerp(
            inputValue.startColor.xyz,
            inputValue.endColor.xyz,
            inputValue.lineCorner.x);
    }
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    MiscAnimationKeysVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.color = color;
    outputValue.phase = phase;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws the opaque AxesHelper entity through the unique Scene Set. */
class MiscAnimationKeysAxesPass final : public IRenderClass
{
public:
    /** Configures the opaque line-helper depth and raster state. */
    constructor(RenderSet<MiscAnimationKeysSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands only the axes entity through RenderSet builtins. */
    MiscAnimationKeysVertexOutput vertex(
        MiscAnimationKeysVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return miscAnimationKeysTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Emits tone-map-independent interpolated helper colors. */
    MiscAnimationKeysFrameBuffer fragment(
        MiscAnimationKeysVertexOutput inputValue)
    {
        if (inputValue.phase < 0.5f) discard_fragment();
        MiscAnimationKeysFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                miscAnimationKeysLinearToSrgb(inputValue.color.x),
                miscAnimationKeysLinearToSrgb(inputValue.color.y),
                miscAnimationKeysLinearToSrgb(inputValue.color.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the animated transparent box entity through the same Scene Set. */
class MiscAnimationKeysBoxPass final : public IRenderClass
{
public:
    /** Configures the transparent MeshBasicMaterial blend and depth state. */
    constructor(RenderSet<MiscAnimationKeysSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
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
    /** Transforms only the animated box entity through RenderSet builtins. */
    MiscAnimationKeysVertexOutput vertex(
        MiscAnimationKeysVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return miscAnimationKeysTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Emits the discrete keyframe color and linearly interpolated opacity. */
    MiscAnimationKeysFrameBuffer fragment(
        MiscAnimationKeysVertexOutput inputValue)
    {
        if (inputValue.phase > 0.5f) discard_fragment();
        const MiscAnimationKeysMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 encoded = float3(
            miscAnimationKeysLinearToSrgb(materialData.colorAndOpacity.x),
            miscAnimationKeysLinearToSrgb(materialData.colorAndOpacity.y),
            miscAnimationKeysLinearToSrgb(materialData.colorAndOpacity.z));
        MiscAnimationKeysFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(encoded),
            half(materialData.colorAndOpacity.w));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and its two ordered geometry passes. */
class MiscAnimationKeysRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<MiscAnimationKeysSceneRenderSet> sceneSet;
    RenderClass<MiscAnimationKeysAxesPass> axesPass;
    RenderClass<MiscAnimationKeysBoxPass> boxPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one Scene Set and both dedicated Scene passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<MiscAnimationKeysSceneRenderSet>();
        axesPass = device->createRenderClass<MiscAnimationKeysAxesPass>(sceneSet);
        boxPass = device->createRenderClass<MiscAnimationKeysBoxPass>(sceneSet);
    }

    /** Allocates ordinary single-sample color and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "MiscAnimationKeysRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "MiscAnimationKeysDepth32", width, height, 1u);
    }

    /** Updates one Set and submits axes then transparent box drawing. */
    void render() override
    {
        sceneSet->update();
        MiscAnimationKeysFrameBuffer axesFrame;
        axesFrame.color = outputTexture->createView();
        axesFrame.color.loadOp = LoadOp::Clear;
        axesFrame.color.storeOp = StoreOp::Store;
        axesFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        axesFrame.depth = depthTexture->createView();
        axesFrame.depth.depthLoadOp = LoadOp::Clear;
        axesFrame.depth.depthStoreOp = StoreOp::Store;
        axesFrame.depth.depthClearValue = 1.0f;
        MiscAnimationKeysFrameBuffer boxFrame;
        boxFrame.color = outputTexture->createView();
        boxFrame.color.loadOp = LoadOp::Load;
        boxFrame.color.storeOp = StoreOp::Store;
        boxFrame.depth = depthTexture->createView();
        boxFrame.depth.depthLoadOp = LoadOp::Load;
        boxFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("MiscAnimationKeysAxes", axesFrame, axesPass())
            ->renderPass("MiscAnimationKeysBox", boxFrame, boxPass())
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Scene Set and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
