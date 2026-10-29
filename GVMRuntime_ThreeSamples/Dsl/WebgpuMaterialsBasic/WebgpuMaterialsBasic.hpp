#ifndef GVM_THREE_WEBGPU_MATERIALS_BASIC_HPP
#define GVM_THREE_WEBGPU_MATERIALS_BASIC_HPP

#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebgpuMaterialsBasic.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one asset-expanded position, normal, and UV. */
struct WebgpuMaterialsBasicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Carries projected head data and the environment direction to the fragment stage. */
struct WebgpuMaterialsBasicVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    uint entityID [[Attribute3]];
};

/** Stores one entity transform, camera position, and material mode. */
struct WebgpuMaterialsBasicObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 cameraPositionAndFlags;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Carries the camera state shared by the fullscreen background and Scene material passes. */
struct WebgpuMaterialsBasicCameraData
{
    float4 positionAndFlags;
    float4 rightAndTanHalfFov;
    float4 upAndAspect;
    float4 forwardAndReserved;
};

/** Stores the required one-entry instance component. */
struct WebgpuMaterialsBasicInstanceData
{
    float4 reserved;
};

/** Stores one material tint and reflection/refraction parameters. */
struct WebgpuMaterialsBasicMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Binds the six explicit sRGB cube faces through one stable sampler. */
struct WebgpuMaterialsBasicSamplerResources final : public IBindGroup
{
    /** Declares the linear filtered environment sampler. */
    constructor(Sampler environmentSampler [[Binding0]],
                UniformBuffer<WebgpuMaterialsBasicCameraData> camera [[Binding1]])
    {
    }
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebgpuMaterialsBasicSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, and material storage. */
    constructor(BufferComponent<WebgpuMaterialsBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebgpuMaterialsBasicObjectData> objects,
                BufferComponent<WebgpuMaterialsBasicInstanceData> instances,
                BufferComponent<WebgpuMaterialsBasicMaterialData> materials,
                (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Defines the RGBA8 color and depth attachments used for deterministic capture. */
struct WebgpuMaterialsBasicFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Selects a right-handed cube face and returns face id plus signed UV. */
float3 webgpuMaterialsBasicCubeFaceUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            return direction.x > 0.0f
                ? float3(0.0f, direction.z / absoluteDirection.x,
                         direction.y / absoluteDirection.x)
                : float3(3.0f, -direction.z / absoluteDirection.x,
                         direction.y / absoluteDirection.x);
        }
        return direction.y > 0.0f
            ? float3(1.0f, -direction.x / absoluteDirection.y,
                     -direction.z / absoluteDirection.y)
            : float3(4.0f, -direction.x / absoluteDirection.y,
                     direction.z / absoluteDirection.y);
    }
    if (absoluteDirection.z > absoluteDirection.y)
    {
        return direction.z > 0.0f
            ? float3(2.0f, -direction.x / absoluteDirection.z,
                     direction.y / absoluteDirection.z)
            : float3(5.0f, direction.x / absoluteDirection.z,
                     direction.y / absoluteDirection.z);
    }
    return direction.y > 0.0f
        ? float3(1.0f, -direction.x / absoluteDirection.y,
                 -direction.z / absoluteDirection.y)
        : float3(4.0f, -direction.x / absoluteDirection.y,
                 direction.z / absoluteDirection.y);
}

/** Evaluates the GLSL refract equation explicitly for Experimental UGLIR parity. */
float3 webgpuMaterialsBasicRefract(float3 incident, float3 normal, float eta)
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
float webgpuMaterialsBasicLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Samples the six Scene-owned cube faces using the locked r185 face convention. */
float2 webgpuMaterialsBasicCubeUvForFace(float3 direction, uint face)
{
    const float3 absoluteDirection = abs(direction);
    if (face == 0u)
        return (float2(direction.z, direction.y) /
                max(absoluteDirection.x, 0.000001f)) * 0.5f + 0.5f;
    if (face == 1u)
        return (float2(-direction.x, -direction.z) /
                max(absoluteDirection.y, 0.000001f)) * 0.5f + 0.5f;
    if (face == 2u)
        return (float2(-direction.x, direction.y) /
                max(absoluteDirection.z, 0.000001f)) * 0.5f + 0.5f;
    if (face == 3u)
        return (float2(-direction.z, direction.y) /
                max(absoluteDirection.x, 0.000001f)) * 0.5f + 0.5f;
    if (face == 4u)
        return (float2(-direction.x, direction.z) /
                max(absoluteDirection.y, 0.000001f)) * 0.5f + 0.5f;
    return (float2(direction.x, direction.y) /
            max(absoluteDirection.z, 0.000001f)) * 0.5f + 0.5f;
}

/** Samples one cube face at the derivative-selected mip level. */
float4 webgpuMaterialsBasicSampleEnvironmentFace(
    IN RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet,
    IN BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources,
    float2 uv,
    uint textureSlot,
    float lod)
{
    if (textureSlot == 0u)
        return float4(sceneSet->textures->get(0u, 0u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
    if (textureSlot == 1u)
        return float4(sceneSet->textures->get(0u, 1u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
    if (textureSlot == 2u)
        return float4(sceneSet->textures->get(0u, 2u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
    if (textureSlot == 3u)
        return float4(sceneSet->textures->get(0u, 3u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
    if (textureSlot == 4u)
        return float4(sceneSet->textures->get(0u, 4u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
    return float4(sceneSet->textures->get(0u, 5u)->sampleLevel(samplerResources->environmentSampler, uv, lod));
}

/** Samples the six Scene-owned cube faces using the locked r185 face convention. */
float3 webgpuMaterialsBasicSampleEnvironment(
    IN RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet,
    IN BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources,
    float3 direction)
{
    const float3 faceUv = webgpuMaterialsBasicCubeFaceUv(normalize(direction));
    const uint face = uint(faceUv.x);
    const float2 uv = clamp(faceUv.yz * 0.5f + 0.5f,
                             float2(0.000001f), float2(0.999999f));
    const float2 textureUv = uv;
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
    const float2 derivativeX =
        webgpuMaterialsBasicCubeUvForFace(direction + ddx(direction), face) - textureUv;
    const float2 derivativeY =
        webgpuMaterialsBasicCubeUvForFace(direction + ddy(direction), face) - textureUv;
    const float footprint = max(
        max(length(derivativeX * 256.0f), length(derivativeY * 256.0f)),
        1.0f);
    const float lod = clamp(log2(footprint), 0.0f, 8.0f);
    return float3(webgpuMaterialsBasicSampleEnvironmentFace(
        sceneSet, samplerResources, textureUv, textureSlot, lod).xyz);
}

/** Draws the locked pisa cubemap as the scene background. */
class WebgpuMaterialsBasicBackgroundPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and disables depth writes for the fullscreen pass. */
    constructor(RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits a fullscreen triangle without a standalone vertex buffer. */
    WebgpuMaterialsBasicVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsBasicVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.worldPosition = float3(uv, 0.0f);
        outputValue.worldNormal = float3(0.0f);
        outputValue.entityID = 0u;
        return outputValue;
    }

    /** Reconstructs a camera ray and displays the cubemap without MSAA. */
    WebgpuMaterialsBasicFrameBuffer fragment(WebgpuMaterialsBasicVertexOutput inputValue)
    {
        const float2 ndc = inputValue.worldPosition.xy * 2.0f - 1.0f;
        const float3 direction = normalize(
            samplerResources->camera->forwardAndReserved.xyz +
            samplerResources->camera->rightAndTanHalfFov.xyz *
                (ndc.x * samplerResources->camera->upAndAspect.w *
                 samplerResources->camera->rightAndTanHalfFov.w) +
            samplerResources->camera->upAndAspect.xyz *
                (-ndc.y * samplerResources->camera->rightAndTanHalfFov.w));
        const float3 color = webgpuMaterialsBasicSampleEnvironment(
            sceneSet, samplerResources, direction);
        WebgpuMaterialsBasicFrameBuffer outputValue;
        outputValue.color = half4(
            half(webgpuMaterialsBasicLinearToSrgb(color.x)),
            half(webgpuMaterialsBasicLinearToSrgb(color.y)),
            half(webgpuMaterialsBasicLinearToSrgb(color.z)),
            half(1.0f));
        return outputValue;
    }
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader evaluates the locked WebgpuMaterialsBasicSphere Lambert/environment material through
 * the RenderSet texture component; no C++ drawing or texture sampling is used.
 */
class WebgpuMaterialsBasicOpaquePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and environment sampler. */
    constructor(RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources [[Slot1]])
    {
        // The generated clip-space conversion flips Y to match the host
        // framebuffer convention, so the authored counter-clockwise sphere
        // winding reaches the backend as Front-facing geometry.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves per-entity and per-instance data through the RenderEntity builtins. */
    WebgpuMaterialsBasicVertexOutput vertex(
        WebgpuMaterialsBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMaterialsBasicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMaterialsBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + instanceData.reserved;
        const float3 worldPosition = float3(mul(objectData.model, localPosition).xyz);
        const float3 worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        WebgpuMaterialsBasicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.worldPosition = worldPosition;
        outputValue.worldNormal = worldNormal;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies Lambert lighting and samples the six RenderSet cube faces. */
    WebgpuMaterialsBasicFrameBuffer fragment(WebgpuMaterialsBasicVertexOutput inputValue)
    {
        const WebgpuMaterialsBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMaterialsBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            0u);
        const float3 cameraToFragment = normalize(
            inputValue.worldPosition - samplerResources->camera->positionAndFlags.xyz);
        if (materialData.parameters.z > 0.5f) discard_fragment();
        const float3 environmentDirection = samplerResources->camera->positionAndFlags.w > 0.5f
            ? webgpuMaterialsBasicRefract(cameraToFragment, inputValue.worldNormal, materialData.parameters.y)
            : reflect(cameraToFragment, inputValue.worldNormal);
        const float3 environment = webgpuMaterialsBasicSampleEnvironment(
            sceneSet, samplerResources,
            float3(-environmentDirection.x, environmentDirection.y, environmentDirection.z));
        const float reflectivity = clamp(materialData.parameters.x, 0.0f, 1.0f);
        // MeshBasicMaterial's MultiplyOperation mixes the authored diffuse
        // contribution with its environment-modulated value.  Keeping the
        // unlit term is important when refractionRatio is below one.
        const float3 combinedColor = materialData.baseColor.xyz *
            lerp(float3(1.0f), environment, reflectivity);
        const float3 linearColor = saturate(combinedColor);
        const float3 srgbColor = float3(
            webgpuMaterialsBasicLinearToSrgb(linearColor.x),
            webgpuMaterialsBasicLinearToSrgb(linearColor.y),
            webgpuMaterialsBasicLinearToSrgb(linearColor.z));
        WebgpuMaterialsBasicFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws transparent entities from the same Scene RenderSet with source-alpha blending. */
class WebgpuMaterialsBasicTransparentPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and configures transparent compositing. */
    constructor(RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources [[Slot1]])
    {
        // Keep the transparent refraction pass on the same winding contract
        // as the opaque pass after the explicit clip-space Y conversion.
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
    /** Resolves the object and instance records through RenderEntity builtins. */
    WebgpuMaterialsBasicVertexOutput vertex(
        WebgpuMaterialsBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuMaterialsBasicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuMaterialsBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + instanceData.reserved;
        WebgpuMaterialsBasicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
        outputValue.worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies the same environment material as the opaque pass, retaining alpha. */
    WebgpuMaterialsBasicFrameBuffer fragment(WebgpuMaterialsBasicVertexOutput inputValue)
    {
        const WebgpuMaterialsBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuMaterialsBasicMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        if (materialData.parameters.z < 0.5f) discard_fragment();
        const float3 viewDirection = normalize(inputValue.worldPosition - samplerResources->camera->positionAndFlags.xyz);
        const float3 environmentDirection = samplerResources->camera->positionAndFlags.w > 0.5f
            ? webgpuMaterialsBasicRefract(viewDirection, inputValue.worldNormal, materialData.parameters.y)
            : reflect(viewDirection, inputValue.worldNormal);
        const float3 environment = webgpuMaterialsBasicSampleEnvironment(
            sceneSet, samplerResources,
            float3(-environmentDirection.x, environmentDirection.y, environmentDirection.z));
        const float reflectivity = clamp(materialData.parameters.x, 0.0f, 1.0f);
        const float3 linearColor = saturate(materialData.baseColor.xyz *
            lerp(float3(1.0f), environment, reflectivity));
        WebgpuMaterialsBasicFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(
            webgpuMaterialsBasicLinearToSrgb(linearColor.x),
            webgpuMaterialsBasicLinearToSrgb(linearColor.y),
            webgpuMaterialsBasicLinearToSrgb(linearColor.z)), half(materialData.baseColor.w));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebgpuMaterialsBasicRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuMaterialsBasicSceneRenderSet> sceneSet;
    Buffer<WebgpuMaterialsBasicCameraData, BufferUsage<Uniform, CopyDst>> cameraBuffer;
    Sampler environmentSampler;
    BindGroup<WebgpuMaterialsBasicSamplerResources> samplerResources;
    RenderClass<WebgpuMaterialsBasicBackgroundPass> backgroundPass;
    RenderClass<WebgpuMaterialsBasicOpaquePass> opaquePass;
    RenderClass<WebgpuMaterialsBasicTransparentPass> transparentPass;
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
    uint activeScenePass = 0u;

public:
    /** Creates this shard's RenderSet and its generated Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuMaterialsBasicSceneRenderSet>();
        cameraBuffer = device->createBuffer("WebgpuMaterialsBasicCamera", 1u);
        environmentSampler = device->createSampler({
            .label = "WebgpuMaterialsBasicEnvironmentSampler",
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
        samplerResources = device->createBindGroup<WebgpuMaterialsBasicSamplerResources>(environmentSampler, cameraBuffer);
        backgroundPass = device->createRenderClass<WebgpuMaterialsBasicBackgroundPass>(sceneSet, samplerResources);
        opaquePass = device->createRenderClass<WebgpuMaterialsBasicOpaquePass>(sceneSet, samplerResources);
        transparentPass = device->createRenderClass<WebgpuMaterialsBasicTransparentPass>(sceneSet, samplerResources);
    }

    /** Uploads the camera state used by all Scene and fullscreen passes. */
    void configureCameraData(
        float positionX, float positionY, float positionZ, float refractionFlag,
        float rightX, float rightY, float rightZ, float tanHalfFov,
        float upX, float upY, float upZ, float aspect,
        float forwardX, float forwardY, float forwardZ, float reserved)
    {
        WebgpuMaterialsBasicCameraData cameraData;
        cameraData.positionAndFlags = float4(positionX, positionY, positionZ, refractionFlag);
        cameraData.rightAndTanHalfFov = float4(rightX, rightY, rightZ, tanHalfFov);
        cameraData.upAndAspect = float4(upX, upY, upZ, aspect);
        cameraData.forwardAndReserved = float4(forwardX, forwardY, forwardZ, reserved);
        graphicsQueue->writeBuffer(BufferRange(cameraBuffer), &cameraData, sizeof(cameraData))->submit();
    }

    /** Recreates the explicit single-sample capture attachments. */
    void configureOutput(uint width, uint height, uint scenePass)
    {
        readbackWidth = width;
        readbackHeight = height;
        activeScenePass = scenePass;
        outputColor = device->createTexture("WebgpuMaterialsBasicRenderSetColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuMaterialsBasicRenderSetDepth", width, height, 1u);
    }

    /** Preserves the host-selected Scene pass when the generated host reapplies target dimensions. */
    void configureOutput(uint width, uint height)
    {
        configureOutput(width, height, activeScenePass);
    }

    /** Applies pending RenderSet commands and submits one indexed-indirect Scene pass. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebgpuMaterialsBasicFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.035, 0.045, 0.075, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebgpuMaterialsBasicFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Load;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.depth = outputDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuMaterialsBasicFrameBuffer transparentFrameBuffer = sceneFrameBuffer;
        if (activeScenePass == 0u)
        {
            graphicsQueue
                ->renderPass("WebgpuMaterialsBasicBackground", frameBuffer, backgroundPass(3u, 1u, 0u, 0u))
                ->renderPass("WebgpuMaterialsBasicOpaque", sceneFrameBuffer, opaquePass())
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue
                ->renderPass("WebgpuMaterialsBasicBackground", frameBuffer, backgroundPass(3u, 1u, 0u, 0u))
                ->renderPass("WebgpuMaterialsBasicTransparent", transparentFrameBuffer, transparentPass())
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
                ->submit();
        }
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
        device->freeBuffer(cameraBuffer);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebgpuMaterialsBasicRenderer
#undef WebgpuMaterialsBasicFrameBuffer
#undef WebgpuMaterialsBasicOpaquePass
#undef WebgpuMaterialsBasicTransparentPass
#undef WebgpuMaterialsBasicSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
