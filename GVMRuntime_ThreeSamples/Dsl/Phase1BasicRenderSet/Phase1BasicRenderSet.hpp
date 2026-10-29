#ifndef GVM_THREE_PHASE1_BASIC_RENDER_SET_HPP
#define GVM_THREE_PHASE1_BASIC_RENDER_SET_HPP

#ifndef __UGLC_HOST_SHADER_ONLY_STUBS_HPP
#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_PREFIX.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */
#define THREE_BASIC_JOIN_IMPL(left, right) left##right
#define THREE_BASIC_JOIN(left, right) THREE_BASIC_JOIN_IMPL(left, right)
#define THREE_BASIC_SCENE_SET THREE_BASIC_JOIN(THREE_BASIC_PREFIX, SceneRenderSet)
#define THREE_BASIC_PASS THREE_BASIC_JOIN(THREE_BASIC_PREFIX, ScenePass)
#define THREE_BASIC_FRAMEBUFFER THREE_BASIC_JOIN(THREE_BASIC_PREFIX, FrameBuffer)
#define THREE_BASIC_RENDERER THREE_BASIC_JOIN(THREE_BASIC_PREFIX, Renderer)

/** Stores one normalized triangle-list vertex packed by the host adapter. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
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
struct THREE_BASIC_SCENE_SET : public IRenderSet
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
struct THREE_BASIC_FRAMEBUFFER final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader contains the common Three-compatible vertex-color, lighting,
 * clipping, and deterministic animation operations used by this wave.
 */
class THREE_BASIC_PASS final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<THREE_BASIC_SCENE_SET> sceneSet [[Slot0]])
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
        return outputValue;
    }

    /** Applies the material component and a stable linear-to-sRGB transfer. */
    THREE_BASIC_FRAMEBUFFER fragment(
        ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            objectData.materialAndFlags.x);
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
        THREE_BASIC_FRAMEBUFFER frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class THREE_BASIC_RENDERER final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<THREE_BASIC_SCENE_SET> sceneSet;
    RenderClass<THREE_BASIC_PASS> scenePass;
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
        sceneSet = device->createRenderSet<THREE_BASIC_SCENE_SET>();
        scenePass = device->createRenderClass<THREE_BASIC_PASS>(sceneSet);
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
        THREE_BASIC_FRAMEBUFFER frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.035, 0.045, 0.075, 1.0};
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

#undef THREE_BASIC_RENDERER
#undef THREE_BASIC_FRAMEBUFFER
#undef THREE_BASIC_PASS
#undef THREE_BASIC_SCENE_SET
#undef THREE_BASIC_JOIN
#undef THREE_BASIC_JOIN_IMPL

#endif // __UGLC_HOST_SHADER_ONLY_STUBS_HPP

#endif
