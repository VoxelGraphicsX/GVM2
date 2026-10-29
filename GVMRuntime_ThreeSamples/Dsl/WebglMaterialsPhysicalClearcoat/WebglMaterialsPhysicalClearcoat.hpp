#ifndef GVM_THREE_WEBGL_MATERIALS_PHYSICAL_CLEARCOAT_HPP
#define GVM_THREE_WEBGL_MATERIALS_PHYSICAL_CLEARCOAT_HPP

#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebglMaterialsPhysicalClearcoat.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one normalized triangle-list vertex packed by the host adapter. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    float3 viewDirection [[Attribute1]];
    uint entityID [[Attribute2]];
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
    float4 physicalParameters;
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglMaterialsPhysicalClearcoatSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, and material storage. */
    constructor(BufferComponent<ThreeBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ThreeBasicObjectData> objects,
                BufferComponent<ThreeBasicInstanceData> instances,
                BufferComponent<ThreeBasicMaterialData> materials,
                (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Defines the RGBA8 color and depth attachments used for deterministic capture. */
struct WebglMaterialsPhysicalClearcoatFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader contains the common Three-compatible vertex-color, lighting,
 * clipping, and deterministic animation operations used by this wave.
 */
class WebglMaterialsPhysicalClearcoatScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<WebglMaterialsPhysicalClearcoatSceneRenderSet> sceneSet [[Slot0]])
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
        outputValue.normal = normalize(float3(inputValue.normal.xyz));
        outputValue.viewDirection = normalize(float3(0.0f, 0.0f, 1.0f));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates a compact clearcoat/metallic BRDF and stable sRGB transfer. */
    WebglMaterialsPhysicalClearcoatFrameBuffer fragment(
        ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            objectData.materialAndFlags.x);
        const float3 normal = normalize(inputValue.normal);
        const float3 viewDirection = normalize(inputValue.viewDirection);
        const float3 lightDirection = normalize(float3(-0.55f, 0.75f, 1.0f));
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float ndotl = max(dot(normal, lightDirection), 0.0f);
        const float ndoth = max(dot(normal, halfDirection), 0.0f);
        const float roughness = clamp(materialData.physicalParameters.x, 0.04f, 1.0f);
        const float metalness = clamp(materialData.physicalParameters.y, 0.0f, 1.0f);
        const float clearcoat = clamp(materialData.physicalParameters.z, 0.0f, 1.0f);
        const float coatRoughness = clamp(materialData.physicalParameters.w, 0.04f, 1.0f);
        const float baseSpecular = pow(max(ndoth, 0.001f), (1.0f - roughness) * 96.0f + 2.0f);
        const float coatSpecular = pow(max(ndoth, 0.001f), (1.0f - coatRoughness) * 256.0f + 2.0f);
        const float3 diffuse = materialData.baseColor.xyz * (1.0f - metalness) * (0.18f + 0.82f * ndotl);
        const float3 specular = float3(0.04f) * (1.0f - metalness) * baseSpecular + materialData.baseColor.xyz * metalness * baseSpecular;
        const float3 coat = float3(1.0f) * clearcoat * coatSpecular;
        const float3 linearColor = saturate(diffuse + specular + coat);
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
        WebglMaterialsPhysicalClearcoatFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebglMaterialsPhysicalClearcoatRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsPhysicalClearcoatSceneRenderSet> sceneSet;
    RenderClass<WebglMaterialsPhysicalClearcoatScenePass> scenePass;
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
        sceneSet = device->createRenderSet<WebglMaterialsPhysicalClearcoatSceneRenderSet>();
        scenePass = device->createRenderClass<WebglMaterialsPhysicalClearcoatScenePass>(sceneSet);
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
        WebglMaterialsPhysicalClearcoatFrameBuffer frameBuffer;
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

#undef WebglMaterialsPhysicalClearcoatRenderer
#undef WebglMaterialsPhysicalClearcoatFrameBuffer
#undef WebglMaterialsPhysicalClearcoatScenePass
#undef WebglMaterialsPhysicalClearcoatSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
