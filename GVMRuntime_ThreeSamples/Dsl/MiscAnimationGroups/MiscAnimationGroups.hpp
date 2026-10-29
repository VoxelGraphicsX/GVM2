#ifndef GVM_THREE_MISC_ANIMATION_GROUPS_HPP
#define GVM_THREE_MISC_ANIMATION_GROUPS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one position from the shared five-unit BoxGeometry payload. */
struct MiscAnimationGroupsVertex
{
    float4 position [[Attribute0]];
};

/** Stores one ordinary box model-view matrix and the shared projection. */
struct MiscAnimationGroupsObjectData
{
    float4x4 modelView;
    float4x4 projection;
};

/** Stores the mandatory one-record non-instanced component. */
struct MiscAnimationGroupsInstanceData
{
    float4 reserved;
};

/** Stores the discrete color key and linearly interpolated opacity. */
struct MiscAnimationGroupsMaterialData
{
    float4 colorAndOpacity;
};

/** Stores the transparent material phase used by generated-product lint. */
struct MiscAnimationGroupsRenderFlagData
{
    float4 phase;
};

/** Defines the unique 25-entity Scene RenderSet for the animation group. */
struct MiscAnimationGroupsSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry and per-entity animation components. */
    constructor(
        BufferComponent<MiscAnimationGroupsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<MiscAnimationGroupsObjectData> objects,
        BufferComponent<MiscAnimationGroupsInstanceData> instances,
        BufferComponent<MiscAnimationGroupsMaterialData> materials,
        BufferComponent<MiscAnimationGroupsRenderFlagData> renderFlags)
    {
    }
};

/** Carries clip position, entity identity, and phase to the fragment stage. */
struct MiscAnimationGroupsVertexOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
    float phase [[Attribute1]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct MiscAnimationGroupsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear working-space channel to the browser output transfer. */
float miscAnimationGroupsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all 25 ordinary boxes through the Scene's sole RenderSet. */
class MiscAnimationGroupsMainPass final : public IRenderClass
{
public:
    /** Configures MeshBasic transparent blending and the default depth state. */
    constructor(RenderSet<MiscAnimationGroupsSceneRenderSet> sceneSet [[Slot0]])
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
    /** Reads entity and instance builtins and transforms one box vertex. */
    MiscAnimationGroupsVertexOutput vertex(
        MiscAnimationGroupsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const MiscAnimationGroupsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const MiscAnimationGroupsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const MiscAnimationGroupsRenderFlagData renderFlag =
            sceneSet->renderFlags->get(renderEntityID, 0u);
        const float4 localPosition =
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        float4 clipPosition = mul(
            objectData.projection,
            mul(objectData.modelView, localPosition));
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        MiscAnimationGroupsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.entityID = renderEntityID;
        outputValue.phase = renderFlag.phase.x;
        return outputValue;
    }

    /** Emits the shared discrete color key and interpolated alpha value. */
    MiscAnimationGroupsFrameBuffer fragment(
        MiscAnimationGroupsVertexOutput inputValue)
    {
        if (inputValue.phase < 0.5f) discard_fragment();
        const MiscAnimationGroupsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 encoded = float3(
            miscAnimationGroupsLinearToSrgb(materialData.colorAndOpacity.x),
            miscAnimationGroupsLinearToSrgb(materialData.colorAndOpacity.y),
            miscAnimationGroupsLinearToSrgb(materialData.colorAndOpacity.z));
        MiscAnimationGroupsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(encoded), half(materialData.colorAndOpacity.w));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and its one transparent geometry pass. */
class MiscAnimationGroupsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<MiscAnimationGroupsSceneRenderSet> sceneSet;
    RenderClass<MiscAnimationGroupsMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the sole Scene RenderSet and dedicated main pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<MiscAnimationGroupsSceneRenderSet>();
        mainPass = device->createRenderClass<MiscAnimationGroupsMainPass>(sceneSet);
    }

    /** Allocates ordinary single-sample final color and depth textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "MiscAnimationGroupsRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "MiscAnimationGroupsDepth32", width, height, 1u);
    }

    /** Updates the one Set, draws all entities indirectly, and presents. */
    void render() override
    {
        sceneSet->update();
        MiscAnimationGroupsFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("MiscAnimationGroupsMain", frameBuffer, mainPass())
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
