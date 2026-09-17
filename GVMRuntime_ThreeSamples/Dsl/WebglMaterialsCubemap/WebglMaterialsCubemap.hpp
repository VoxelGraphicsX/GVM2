#ifndef GVM_THREE_WEBGL_MATERIALS_CUBEMAP_HPP
#define GVM_THREE_WEBGL_MATERIALS_CUBEMAP_HPP

#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebglMaterialsCubemap.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one OBJ-expanded position, normal, and UV. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Carries projected head data and the environment direction to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    uint entityID [[Attribute3]];
};

/** Stores one entity transform, camera position, and material mode. */
struct ThreeBasicObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 cameraPositionAndFlags;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Stores the required one-entry instance component. */
struct ThreeBasicInstanceData
{
    float4 reserved;
};

/** Stores one material tint and reflection/refraction parameters. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Binds the six explicit sRGB cube faces through one stable sampler. */
struct WebglMaterialsCubemapSamplerResources final : public IBindGroup
{
    /** Declares the linear filtered environment sampler. */
    constructor(Sampler environmentSampler [[Binding0]])
    {
    }
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglMaterialsCubemapSceneRenderSet : public IRenderSet
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
struct WebglMaterialsCubemapFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Selects a right-handed cube face and returns face id plus signed UV. */
float3 webglMaterialsCubemapCubeFaceUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            return direction.x > 0.0f
                ? float3(0.0f, direction.z, direction.y) / absoluteDirection.x
                : float3(3.0f, -direction.z, direction.y) / absoluteDirection.x;
        }
        return direction.y > 0.0f
            ? float3(1.0f, -direction.x, -direction.z) / absoluteDirection.y
            : float3(4.0f, -direction.x, direction.z) / absoluteDirection.y;
    }
    if (absoluteDirection.z > absoluteDirection.y)
    {
        return direction.z > 0.0f
            ? float3(2.0f, -direction.x, direction.y) / absoluteDirection.z
            : float3(5.0f, direction.x, direction.y) / absoluteDirection.z;
    }
    return direction.y > 0.0f
        ? float3(1.0f, -direction.x, -direction.z) / absoluteDirection.y
        : float3(4.0f, -direction.x, direction.z) / absoluteDirection.y;
}

/** Evaluates the GLSL refract equation explicitly for Experimental UGLIR parity. */
float3 webglMaterialsCubemapRefract(float3 incident, float3 normal, float eta)
{
    const float cosi = clamp(dot(incident, normal), -1.0f, 1.0f);
    const float3 orientedNormal = cosi < 0.0f ? normal : -normal;
    const float orientedCosine = abs(cosi);
    const float etaRatio = cosi < 0.0f ? eta : 1.0f / eta;
    const float k = 1.0f - etaRatio * etaRatio * (1.0f - orientedCosine * orientedCosine);
    return k < 0.0f
        ? reflect(incident, orientedNormal)
        : etaRatio * incident + (etaRatio * orientedCosine - sqrt(max(k, 0.0f))) * orientedNormal;
}

/** Converts one linear cube-map sample to the renderer's sRGB output space. */
float webglMaterialsCubemapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Samples the six Scene-owned cube faces using the locked r185 face convention. */
float3 webglMaterialsCubemapSampleEnvironment(
    IN RenderSet<WebglMaterialsCubemapSceneRenderSet> sceneSet,
    IN BindGroup<WebglMaterialsCubemapSamplerResources> samplerResources,
    float3 direction)
{
    const float3 faceUv = webglMaterialsCubemapCubeFaceUv(normalize(direction));
    const uint face = uint(faceUv.x);
    const float2 uv = clamp(faceUv.yz * 0.5f + 0.5f,
                             float2(0.000001f), float2(0.999999f));
    const uint textureSlot = face == 0u
        ? 0u
        : face == 1u
            ? 2u
            : face == 2u
                ? 4u
                : face == 3u
                    ? 1u
                    : face == 4u
                        ? 3u
                        : 5u;
    const float2 textureUv = uv;
    return textureSlot == 0u
        ? float3(sceneSet->textures->get(0u, 0u)->sample(samplerResources->environmentSampler, textureUv).xyz)
        : textureSlot == 1u
            ? float3(sceneSet->textures->get(0u, 1u)->sample(samplerResources->environmentSampler, textureUv).xyz)
            : textureSlot == 2u
                ? float3(sceneSet->textures->get(0u, 2u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                : textureSlot == 3u
                    ? float3(sceneSet->textures->get(0u, 3u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                    : textureSlot == 4u
                        ? float3(sceneSet->textures->get(0u, 4u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                        : float3(sceneSet->textures->get(0u, 5u)->sample(samplerResources->environmentSampler, textureUv).xyz);
}

/** Draws the locked SwedishRoyalCastle cubemap as the scene background. */
class WebglMaterialsCubemapBackgroundPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and disables depth writes for the fullscreen pass. */
    constructor(RenderSet<WebglMaterialsCubemapSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsCubemapSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits a fullscreen triangle without a standalone vertex buffer. */
    ThreeBasicVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        ThreeBasicVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.worldPosition = float3(uv, 0.0f);
        outputValue.worldNormal = float3(0.0f);
        outputValue.entityID = 0u;
        return outputValue;
    }

    /** Reconstructs a camera ray and displays the cubemap without MSAA. */
    WebglMaterialsCubemapFrameBuffer fragment(ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData cameraData = sceneSet->objects->get(0u, 0u);
        const float2 ndc = inputValue.worldPosition.xy * 2.0f - 1.0f;
        const float3 direction = normalize(
            cameraData.cameraForwardAndReserved.xyz +
            cameraData.cameraRightAndTanHalfFov.xyz *
                (ndc.x * cameraData.cameraUpAndAspect.w *
                 cameraData.cameraRightAndTanHalfFov.w) +
            cameraData.cameraUpAndAspect.xyz *
                (-ndc.y * cameraData.cameraRightAndTanHalfFov.w));
        const float3 color = webglMaterialsCubemapSampleEnvironment(
            sceneSet, samplerResources, direction);
        WebglMaterialsCubemapFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglMaterialsCubemapLinearToSrgb(color.x)),
            half(webglMaterialsCubemapLinearToSrgb(color.y)),
            half(webglMaterialsCubemapLinearToSrgb(color.z)),
            half(1.0f));
        return outputValue;
    }
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader evaluates the locked WaltHead Lambert/environment material through
 * the RenderSet texture component; no C++ drawing or texture sampling is used.
 */
class WebglMaterialsCubemapMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and environment sampler. */
    constructor(RenderSet<WebglMaterialsCubemapSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsCubemapSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
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
        const float4 localPosition = inputValue.position + instanceData.reserved;
        const float3 worldPosition = float3(mul(objectData.model, localPosition).xyz);
        const float3 worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        ThreeBasicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.worldPosition = worldPosition;
        outputValue.worldNormal = worldNormal;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies Lambert lighting and samples the six RenderSet cube faces. */
    WebglMaterialsCubemapFrameBuffer fragment(ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            0u);
        const float distanceToPointLight = max(length(inputValue.worldPosition), 0.0001f);
        const float3 lightDirection = -inputValue.worldPosition / distanceToPointLight;
        const float directLight = 200.0f / (distanceToPointLight * distanceToPointLight) *
            max(dot(inputValue.worldNormal, lightDirection), 0.0f);
        const float3 outgoingLight = materialData.baseColor.xyz *
            (3.0f + directLight) * 0.318309886f;
        const float3 cameraToFragment = normalize(
            inputValue.worldPosition - objectData.cameraPositionAndFlags.xyz);
        const float3 environmentDirection = objectData.cameraPositionAndFlags.w > 0.5f
            ? webglMaterialsCubemapRefract(cameraToFragment, inputValue.worldNormal, 0.95f)
            : reflect(cameraToFragment, inputValue.worldNormal);
        const float3 environment = webglMaterialsCubemapSampleEnvironment(
            sceneSet, samplerResources, environmentDirection);
        const float reflectivity = clamp(materialData.parameters.x, 0.0f, 1.0f);
        const bool mixEnvironment = materialData.parameters.y > 0.5f;
        const float3 combinedColor = mixEnvironment
            ? outgoingLight + (environment - outgoingLight) * reflectivity
            : outgoingLight * environment;
        const float3 linearColor = saturate(combinedColor);
        const float3 srgbColor = float3(
            webglMaterialsCubemapLinearToSrgb(linearColor.x),
            webglMaterialsCubemapLinearToSrgb(linearColor.y),
            webglMaterialsCubemapLinearToSrgb(linearColor.z));
        WebglMaterialsCubemapFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebglMaterialsCubemapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsCubemapSceneRenderSet> sceneSet;
    Sampler environmentSampler;
    BindGroup<WebglMaterialsCubemapSamplerResources> samplerResources;
    RenderClass<WebglMaterialsCubemapBackgroundPass> backgroundPass;
    RenderClass<WebglMaterialsCubemapMainPass> scenePass;
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
        sceneSet = device->createRenderSet<WebglMaterialsCubemapSceneRenderSet>();
        environmentSampler = device->createSampler({
            .label = "WebglMaterialsCubemapEnvironmentSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglMaterialsCubemapSamplerResources>(environmentSampler);
        backgroundPass = device->createRenderClass<WebglMaterialsCubemapBackgroundPass>(sceneSet, samplerResources);
        scenePass = device->createRenderClass<WebglMaterialsCubemapMainPass>(sceneSet, samplerResources);
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
        WebglMaterialsCubemapFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.035, 0.045, 0.075, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglMaterialsCubemapFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Load;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.depth = outputDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("WebglMaterialsCubemapBackground", frameBuffer, backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglMaterialsCubemapScene", sceneFrameBuffer, scenePass())
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

#undef WebglMaterialsCubemapRenderer
#undef WebglMaterialsCubemapFrameBuffer
#undef WebglMaterialsCubemapMainPass
#undef WebglMaterialsCubemapSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
