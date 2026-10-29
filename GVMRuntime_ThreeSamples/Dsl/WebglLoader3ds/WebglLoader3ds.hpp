#ifndef GVM_THREE_WEBGL_LOADER_3DS_HPP
#define GVM_THREE_WEBGL_LOADER_3DS_HPP

#include "UGL.h"

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebglLoader3ds.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one normalized triangle-list vertex packed by the host adapter. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Stores one entity transform and material index in the Scene RenderSet. */
struct ThreeBasicObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 lightDirectionAndIntensity;
    float4 directionalLightColorAndIntensity;
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
    float4 specularColorAndShininess;
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglLoader3dsSceneRenderSet : public IRenderSet
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

/** Binds the trilinear sampler used by the decoded portalgun diffuse image. */
struct WebglLoader3dsSamplerResources final : public IBindGroup
{
    /** Declares the one immutable diffuse sampler. */
    constructor(Sampler diffuseSampler [[Binding0]])
    {
    }
};

/** Defines the RGBA8 color and depth attachments used for deterministic capture. */
struct WebglLoader3dsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader contains the common Three-compatible vertex-color, lighting,
 * clipping, and deterministic animation operations used by this wave.
 */
class WebglLoader3dsScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<WebglLoader3dsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglLoader3dsSamplerResources> samplerResources [[Slot1]])
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
        const float4 localPosition = inputValue.position + float4(instanceData.offsetAndScale.xyz, 0.0f);
        const float4 viewPosition = mul(objectData.modelView, localPosition);
        float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        const float3 transformedNormal = mul(
            objectData.modelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.uv = inputValue.uv.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies the material component and a stable linear-to-sRGB transfer. */
    WebglLoader3dsFrameBuffer fragment(
        ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID,
            0u);
        const float3 albedo = materialData.baseColor.xyz * float3(
            sceneSet->textures->get(inputValue.entityID, 0u)
                ->sample(samplerResources->diffuseSampler, inputValue.uv).xyz);
        const float3 geometricNormal = normalize(inputValue.viewNormal);
        const float3 positionDx = ddx(inputValue.viewPosition);
        const float3 positionDy = ddy(inputValue.viewPosition);
        const float2 uvDx = ddx(inputValue.uv);
        const float2 uvDy = ddy(inputValue.uv);
        // Three's getTangentFrame receives eye-space position as `-vViewPosition`.
        // Keep that sign in the derivative basis so normal-map orientation remains
        // identical when TrackballControls changes the camera azimuth.
        const float3 q1Perpendicular = cross(-positionDy, geometricNormal);
        const float3 q0Perpendicular = cross(geometricNormal, -positionDx);
        const float3 tangentUnscaled =
            q1Perpendicular * uvDx.x + q0Perpendicular * uvDy.x;
        const float3 bitangentUnscaled =
            q1Perpendicular * uvDx.y + q0Perpendicular * uvDy.y;
        const float tangentDet = max(
            dot(tangentUnscaled, tangentUnscaled),
            dot(bitangentUnscaled, bitangentUnscaled));
        const float tangentScale = rsqrt(max(tangentDet, 0.000001f));
        const float3 tangent = tangentUnscaled * tangentScale;
        const float3 bitangent = bitangentUnscaled * tangentScale;
        const float3 normalSample = float3(
            sceneSet->textures->get(inputValue.entityID, 1u)
                ->sample(samplerResources->diffuseSampler, inputValue.uv).xyz) * 2.0f - 1.0f;
        const float3 mappedNormal = normalize(
            tangent * normalSample.x +
            bitangent * normalSample.y +
            geometricNormal * normalSample.z);
        const float3 lightDirection = normalize(float3(
            objectData.lightDirectionAndIntensity.x,
            objectData.lightDirectionAndIntensity.y,
            objectData.lightDirectionAndIntensity.z));
        const float diffuse = max(dot(mappedNormal, lightDirection), 0.0f);
        const float3 directionalIrradiance =
            objectData.directionalLightColorAndIntensity.xyz *
            objectData.directionalLightColorAndIntensity.w * diffuse;
        const float3 ambientIrradiance = float3(3.0f);
        const float inversePi = 0.3183098861837907f;
        const float3 directDiffuse = albedo * directionalIrradiance * inversePi;
        const float3 indirectDiffuse = albedo * ambientIrradiance * inversePi;
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float dotNormalHalf = max(dot(mappedNormal, halfDirection), 0.0f);
        const float dotViewHalf = max(dot(viewDirection, halfDirection), 0.0f);
        // Three r185 uses the optimized Epic/SIGGRAPH Schlick approximation.
        const float fresnelFactor = exp2(
            (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
        const float3 fresnel = materialData.specularColorAndShininess.xyz *
            (1.0f - fresnelFactor) + float3(fresnelFactor);
        const float shininess = materialData.specularColorAndShininess.w;
        const float distribution = inversePi *
            (shininess * 0.5f + 1.0f) * pow(dotNormalHalf, shininess);
        const float directSpecular = diffuse * 0.25f * distribution *
            objectData.directionalLightColorAndIntensity.w;
        const float3 linearColor = saturate(
            indirectDiffuse + directDiffuse +
            fresnel * directSpecular *
                objectData.directionalLightColorAndIntensity.xyz);
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
        WebglLoader3dsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebglLoader3dsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoader3dsSceneRenderSet> sceneSet;
    Sampler diffuseSampler;
    BindGroup<WebglLoader3dsSamplerResources> samplerResources;
    RenderClass<WebglLoader3dsScenePass> scenePass;
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
        sceneSet = device->createRenderSet<WebglLoader3dsSceneRenderSet>();
        diffuseSampler = device->createSampler({
            .label = "WebglLoader3dsDiffuseSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglLoader3dsSamplerResources>(diffuseSampler);
        scenePass = device->createRenderClass<WebglLoader3dsScenePass>(sceneSet, samplerResources);
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
        WebglLoader3dsFrameBuffer frameBuffer;
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

#undef WebglLoader3dsRenderer
#undef WebglLoader3dsFrameBuffer
#undef WebglLoader3dsScenePass
#undef WebglLoader3dsSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
