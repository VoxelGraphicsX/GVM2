#ifndef GVM_THREE_WEBGL_INSTANCING_MORPH_HPP
#define GVM_THREE_WEBGL_INSTANCING_MORPH_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglInstancingMorphHorseVertexCount = 2952u;
static const uint WebglInstancingMorphTargetCount = 15u;
static const uint WebglInstancingMorphShadowSize = 512u;

/** Stores the union vertex layout for the ground and Horse entities. */
struct WebglInstancingMorphVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 localVertexAndReserved [[Attribute2]];
    float4 normal [[Attribute3]];
};

/** Stores camera, shadow, lighting, and entity classification state. */
struct WebglInstancingMorphObjectData
{
    float4x4 viewProjection;
    float4x4 shadowViewProjection;
    float4 cameraPositionAndFogNear;
    float4 cameraForwardAndFogFar;
    float4 directionalLightAndEntityKind;
};

/** Stores one transform, instance color, and all 15 morph influences. */
struct WebglInstancingMorphInstanceData
{
    float4x4 model;
    float4 color;
    float4 morphWeights0;
    float4 morphWeights1;
    float4 morphWeights2;
    float4 morphWeights3;
};

/** Stores the base color and Standard-material surface parameters. */
struct WebglInstancingMorphMaterialData
{
    float4 baseColor;
    float4 roughnessMetalnessAndFlags;
};

/** Stores cast/receive shadow flags without adding per-pass draw lists. */
struct WebglInstancingMorphShadowFlags
{
    uint4 values;
};

/** Defines the only RenderSet owned by the webgpu_instancing_morph Scene. */
struct WebglInstancingMorphSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, instances, materials, morphs, and shadow flags. */
    constructor(BufferComponent<WebglInstancingMorphVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebglInstancingMorphObjectData> objects,
                BufferComponent<WebglInstancingMorphInstanceData> instances,
                BufferComponent<WebglInstancingMorphMaterialData> materials,
                BufferComponent<float4> morphTargets,
                BufferComponent<WebglInstancingMorphShadowFlags> shadowFlags)
    {
    }
};

/** Binds the filtered directional VSM distribution used by Scene lighting. */
struct WebglInstancingMorphLightingResources final : public IBindGroup
{
    /** Declares the filtered moments and Three's linear clamp sampler. */
    constructor(Texture2D<half2> shadowDistribution [[Binding0]],
                Sampler shadowSampler [[Binding1]])
    {
    }
};

/** Binds native shadow depth for the first VSM filter direction. */
struct WebglInstancingMorphVsmVerticalResources final : public IBindGroup
{
    /** Declares the depth attachment sampled with exact integer texels. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowDepth [[Binding0]])
    {
    }
};

/** Binds vertical moments for the second VSM filter direction. */
struct WebglInstancingMorphVsmHorizontalResources final : public IBindGroup
{
    /** Declares half-float moments and the linear clamp sampler. */
    constructor(Texture2D<half2> verticalMoments [[Binding0]],
                Sampler shadowSampler [[Binding1]])
    {
    }
};

/** Carries the shadow clip position and caster flag. */
struct WebglInstancingMorphShadowVertexOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Defines the depth-only directional shadow attachment. */
struct WebglInstancingMorphShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries world-space Standard-material inputs to the fragment stage. */
struct WebglInstancingMorphMainVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float4 shadowClip [[Attribute1]];
    float4 instanceColor [[Attribute2]];
    float3 cameraPosition [[Attribute3]];
    float3 cameraForward [[Attribute4]];
    float2 fogRange [[Attribute5]];
    uint2 entityAndShadow [[Attribute6]];
    float3 surfaceNormal [[Attribute7]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglInstancingMorphMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries one fullscreen VSM triangle through both filtering directions. */
struct WebglInstancingMorphVsmVertexOutput
{
    float4 position [[Position]];
};

/** Defines one half-float VSM moment attachment. */
struct WebglInstancingMorphVsmFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RG16Float> moments;
};

/** Returns one indexed morph influence from four packed vectors. */
float webglInstancingMorphWeight(WebglInstancingMorphInstanceData instanceData,
                                  uint target)
{
    if (target < 4u) return instanceData.morphWeights0[target];
    if (target < 8u) return instanceData.morphWeights1[target - 4u];
    if (target < 12u) return instanceData.morphWeights2[target - 8u];
    return instanceData.morphWeights3[target - 12u];
}

/** Resolves the ground position or all relative Horse morph targets. */
float3 webglInstancingMorphPosition(
    IN RenderSet<WebglInstancingMorphSceneRenderSet> sceneSet,
    WebglInstancingMorphVertex inputValue,
    WebglInstancingMorphObjectData objectData,
    WebglInstancingMorphInstanceData instanceData,
    uint renderEntityID,
    uint vertexID)
{
    float3 position = inputValue.position.xyz;
    if (objectData.directionalLightAndEntityKind.w > 0.5f)
    {
        for (uint target = 0u; target < WebglInstancingMorphTargetCount; ++target)
        {
            position += sceneSet->morphTargets->get(
                renderEntityID,
                target * WebglInstancingMorphHorseVertexCount +
                    uint(inputValue.localVertexAndReserved.x)).xyz *
                webglInstancingMorphWeight(instanceData, target);
        }
    }
    return mul(instanceData.model, float4(position, 1.0f)).xyz;
}

/** Converts one linear-light channel using Three r185 output transfer constants. */
float webglInstancingMorphLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three r185 VSM with its hard test and light-bleed reduction. */
float webglInstancingMorphShadow(
    IN BindGroup<WebglInstancingMorphLightingResources> resources,
    float4 shadowClip)
{
    const float3 coordinate = float3(
        shadowClip.xy / shadowClip.w * 0.5f + 0.5f,
        shadowClip.z / shadowClip.w - 0.01f);
    if (coordinate.x < 0.0f || coordinate.x > 1.0f ||
        coordinate.y < 0.0f || coordinate.y > 1.0f ||
        coordinate.z < 0.0f || coordinate.z > 1.0f)
    {
        return 1.0f;
    }
    const float distributionMean = float(
        resources->shadowDistribution->sample(
            resources->shadowSampler, coordinate.xy).x);
    const float distributionDeviation = float(
        resources->shadowDistribution->sample(
            resources->shadowSampler, coordinate.xy).y);
    const float hardShadow = coordinate.z <= distributionMean ? 1.0f : 0.0f;
    if (hardShadow == 1.0f) return 1.0f;
    const float variance = max(
        distributionDeviation * distributionDeviation, 0.0000001f);
    const float distance = coordinate.z - distributionMean;
    float probability = variance / (variance + distance * distance);
    probability = clamp((probability - 0.3f) / 0.65f, 0.0f, 1.0f);
    return max(hardShadow, probability);
}

/** Draws both Scene entities into the shared directional shadow map. */
class WebglInstancingMorphVsmShadowPass final : public IRenderClass
{
public:
    /** Binds only the unique Scene RenderSet. */
    constructor(RenderSet<WebglInstancingMorphSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Morphs every Horse instance and retains per-entity shadow flags. */
    WebglInstancingMorphShadowVertexOutput vertex(
        WebglInstancingMorphVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingMorphObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglInstancingMorphInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglInstancingMorphShadowVertexOutput outputValue;
        outputValue.position = mul(
            objectData.shadowViewProjection,
            float4(webglInstancingMorphPosition(
                sceneSet, inputValue, objectData, instanceData,
                renderEntityID, vertexID), 1.0f));
        const uint2 shadowParticipation =
            sceneSet->shadowFlags->get(renderEntityID, 0u).values.xy;
        outputValue.castShadow = max(
            shadowParticipation.x, shadowParticipation.y);
        return outputValue;
    }

    /** Rejects the non-casting ground while preserving automatic depth output. */
    WebglInstancingMorphShadowFrameBuffer fragment(
        WebglInstancingMorphShadowVertexOutput inputValue)
    {
        if (inputValue.castShadow == 0u) discard_fragment();
        WebglInstancingMorphShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Draws ground and all 1,024 independently morphed Horse instances. */
class WebglInstancingMorphMainLitPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and its DSL-owned shadow texture. */
    constructor(RenderSet<WebglInstancingMorphSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglInstancingMorphLightingResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance components for the main camera. */
    WebglInstancingMorphMainVertexOutput vertex(
        WebglInstancingMorphVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingMorphObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglInstancingMorphInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float3 worldPosition = webglInstancingMorphPosition(
            sceneSet, inputValue, objectData, instanceData,
            renderEntityID, vertexID);
        WebglInstancingMorphMainVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection,
                                   float4(worldPosition, 1.0f));
        outputValue.worldPosition = worldPosition;
        outputValue.shadowClip = mul(objectData.shadowViewProjection,
                                     float4(worldPosition, 1.0f));
        outputValue.instanceColor = inputValue.color * instanceData.color;
        outputValue.cameraPosition = objectData.cameraPositionAndFogNear.xyz;
        outputValue.cameraForward = objectData.cameraForwardAndFogFar.xyz;
        outputValue.fogRange = float2(objectData.cameraPositionAndFogNear.w,
                                      objectData.cameraForwardAndFogFar.w);
        outputValue.entityAndShadow = uint2(
            renderEntityID,
            sceneSet->shadowFlags->get(renderEntityID, 0u).values.y);
        outputValue.surfaceNormal = normalize(float3(inputValue.normal.xyz));
        return outputValue;
    }

    /** Evaluates flat Standard lighting, shadowing, fog, and output transfer. */
    WebglInstancingMorphMainFrameBuffer fragment(
        WebglInstancingMorphMainVertexOutput inputValue)
    {
        const WebglInstancingMorphObjectData objectData =
            sceneSet->objects->get(inputValue.entityAndShadow.x, 0u);
        const WebglInstancingMorphMaterialData materialData =
            sceneSet->materials->get(inputValue.entityAndShadow.x, 0u);
        float3 normal = normalize(inputValue.surfaceNormal);
        const float3 viewDirection = normalize(
            inputValue.cameraPosition - inputValue.worldPosition);
        if (dot(normal, viewDirection) < 0.0f) normal = -normal;
        const float3 lightDirection = normalize(
            float3(objectData.directionalLightAndEntityKind.xyz));
        const float directWeight = max(dot(normal, lightDirection), 0.0f);
        const float shadow = inputValue.entityAndShadow.y == 0u
            ? 1.0f
            : webglInstancingMorphShadow(resources, inputValue.shadowClip);
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = lerp(
            float3(0.13286832f, 0.31854680f, 0.03310477f),
            float3(0.31854680f, 0.72305513f, 1.0f),
            hemisphereWeight) * 0.3333333333f;
        const float3 albedo = materialData.baseColor.xyz *
                              inputValue.instanceColor.xyz;
        float3 linearColor = albedo *
            (hemisphere + float3(directWeight * shadow)) *
            0.31830988618f;
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float roughness = materialData.roughnessMetalnessAndFlags.x;
        const float shininess = max(2.0f, 2.0f / max(roughness * roughness, 0.001f) - 2.0f);
        linearColor += float3(0.04f) *
            ((shininess + 2.0f) * 0.15915494309f *
             pow(max(dot(normal, halfDirection), 0.0f), shininess) *
             directWeight * shadow * 0.25f);
        float fogDepth = dot(
            inputValue.worldPosition - inputValue.cameraPosition,
            inputValue.cameraForward);
        const float fogFactor = smoothstep(
            inputValue.fogRange.x, inputValue.fogRange.y, fogDepth);
        const float3 surfaceDisplayColor = float3(
            webglInstancingMorphLinearToSrgb(linearColor.x),
            webglInstancingMorphLinearToSrgb(linearColor.y),
            webglInstancingMorphLinearToSrgb(linearColor.z));
        const float3 displayColor = lerp(
            surfaceDisplayColor,
            float3(0.6f, 0.86666667f, 1.0f),
            fogFactor);
        WebglInstancingMorphMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Converts native depth into vertically filtered Three r185 VSM moments. */
class WebglInstancingMorphVsmVerticalBlurPass final : public IRenderClass
{
public:
    /** Binds native depth and disables fullscreen triangle culling. */
    constructor(BindGroup<WebglInstancingMorphVsmVerticalResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Emits one fullscreen triangle without adding Scene geometry. */
    WebglInstancingMorphVsmVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglInstancingMorphVsmVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Applies the exact eight depth taps across the default radius one. */
    WebglInstancingMorphVsmFrameBuffer fragment(
        WebglInstancingMorphVsmVertexOutput inputValue)
    {
        float mean = 0.0f;
        float squaredMean = 0.0f;
        for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
        {
            const float offset = -1.0f + float(sampleIndex) * (2.0f / 7.0f);
            const float2 position =
                inputValue.position.xy + float2(0.0f, offset);
            const uint2 texel = uint2(
                uint(clamp(floor(position.x), 0.0f, 511.0f)),
                uint(clamp(floor(position.y), 0.0f, 511.0f)));
            const float depth = resources->shadowDepth->read(texel).x;
            mean += depth;
            squaredMean += depth * depth;
        }
        mean *= 0.125f;
        squaredMean *= 0.125f;
        WebglInstancingMorphVsmFrameBuffer frameBuffer;
        frameBuffer.moments = half2(
            mean, sqrt(max(0.0f, squaredMean - mean * mean)));
        return frameBuffer;
    }
};

/** Horizontally filters vertical moments into the final VSM distribution. */
class WebglInstancingMorphVsmHorizontalBlurPass final : public IRenderClass
{
public:
    /** Binds intermediate moments and Three's linear clamp sampler. */
    constructor(BindGroup<WebglInstancingMorphVsmHorizontalResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Emits one fullscreen triangle without adding Scene geometry. */
    WebglInstancingMorphVsmVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglInstancingMorphVsmVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Reconstructs squared moments for the exact eight horizontal taps. */
    WebglInstancingMorphVsmFrameBuffer fragment(
        WebglInstancingMorphVsmVertexOutput inputValue)
    {
        float mean = 0.0f;
        float squaredMean = 0.0f;
        for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
        {
            const float offset = -1.0f + float(sampleIndex) * (2.0f / 7.0f);
            const float2 uv =
                (inputValue.position.xy + float2(offset, 0.0f)) /
                float(WebglInstancingMorphShadowSize);
            const float distributionMean = float(
                resources->verticalMoments->sample(
                    resources->shadowSampler, uv).x);
            const float distributionDeviation = float(
                resources->verticalMoments->sample(
                    resources->shadowSampler, uv).y);
            mean += distributionMean;
            squaredMean += distributionDeviation * distributionDeviation +
                           distributionMean * distributionMean;
        }
        mean *= 0.125f;
        squaredMean *= 0.125f;
        WebglInstancingMorphVsmFrameBuffer frameBuffer;
        frameBuffer.moments = half2(
            mean, sqrt(max(0.0f, squaredMean - mean * mean)));
        return frameBuffer;
    }
};

/** Owns the dedicated webgl_instancing_morph Scene and single-sample output. */
class WebglInstancingMorphRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInstancingMorphSceneRenderSet> sceneSet;
    BindGroup<WebglInstancingMorphLightingResources> lightingResources;
    BindGroup<WebglInstancingMorphVsmVerticalResources> vsmVerticalResources;
    BindGroup<WebglInstancingMorphVsmHorizontalResources> vsmHorizontalResources;
    RenderClass<WebglInstancingMorphVsmShadowPass> shadowPass;
    RenderClass<WebglInstancingMorphVsmVerticalBlurPass> vsmVerticalPass;
    RenderClass<WebglInstancingMorphVsmHorizontalBlurPass> vsmHorizontalPass;
    RenderClass<WebglInstancingMorphMainLitPass> mainPass;
    Sampler shadowSampler;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth;
    Texture<TextureFormat::RG16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowVerticalMoments;
    Texture<TextureFormat::RG16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDistribution;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and default 512-square r185 VSM chain. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInstancingMorphSceneRenderSet>();
        shadowDepth = device->createTexture(
            "WebglInstancingMorphShadowDepth",
            WebglInstancingMorphShadowSize,
            WebglInstancingMorphShadowSize, 1u);
        shadowVerticalMoments = device->createTexture(
            "WebglInstancingMorphShadowVerticalMoments",
            WebglInstancingMorphShadowSize,
            WebglInstancingMorphShadowSize, 1u);
        shadowDistribution = device->createTexture(
            "WebglInstancingMorphShadowDistribution",
            WebglInstancingMorphShadowSize,
            WebglInstancingMorphShadowSize, 1u);
        shadowSampler = device->createSampler({
            .label = "WebglInstancingMorphShadowLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        lightingResources = device->createBindGroup<
            WebglInstancingMorphLightingResources>(
                shadowDistribution->createView(), shadowSampler);
        vsmVerticalResources = device->createBindGroup<
            WebglInstancingMorphVsmVerticalResources>(
                shadowDepth->createView());
        vsmHorizontalResources = device->createBindGroup<
            WebglInstancingMorphVsmHorizontalResources>(
                shadowVerticalMoments->createView(), shadowSampler);
        shadowPass = device->createRenderClass<
            WebglInstancingMorphVsmShadowPass>(sceneSet);
        vsmVerticalPass = device->createRenderClass<
            WebglInstancingMorphVsmVerticalBlurPass>(vsmVerticalResources);
        vsmHorizontalPass = device->createRenderClass<
            WebglInstancingMorphVsmHorizontalBlurPass>(vsmHorizontalResources);
        mainPass = device->createRenderClass<WebglInstancingMorphMainLitPass>(
            sceneSet, lightingResources);
    }

    /** Allocates single-sample color and depth attachments for the fixed capture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglInstancingMorphOutput", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglInstancingMorphSceneDepth", width, height, 1u);
    }

    /** Preserves the common host adapter hook; WebGL has no Inspector overlay. */
    void configureInspector(float enabled)
    {
        (void)enabled;
    }

    /** Updates the Set, renders shadow and main passes, and presents RGBA8. */
    void render() override
    {
        sceneSet->update();
        WebglInstancingMorphShadowFrameBuffer shadowFrame;
        shadowFrame.depth = shadowDepth->createView();
        shadowFrame.depth.depthLoadOp = LoadOp::Clear;
        shadowFrame.depth.depthStoreOp = StoreOp::Store;
        shadowFrame.depth.depthClearValue = 1.0f;
        WebglInstancingMorphVsmFrameBuffer verticalVsmFrame;
        verticalVsmFrame.moments = shadowVerticalMoments->createView();
        verticalVsmFrame.moments.loadOp = LoadOp::Clear;
        verticalVsmFrame.moments.storeOp = StoreOp::Store;
        verticalVsmFrame.moments.clearValue = {1.0f, 0.0f, 0.0f, 0.0f};
        WebglInstancingMorphVsmFrameBuffer horizontalVsmFrame;
        horizontalVsmFrame.moments = shadowDistribution->createView();
        horizontalVsmFrame.moments.loadOp = LoadOp::Clear;
        horizontalVsmFrame.moments.storeOp = StoreOp::Store;
        horizontalVsmFrame.moments.clearValue = {1.0f, 0.0f, 0.0f, 0.0f};
        WebglInstancingMorphMainFrameBuffer mainFrame;
        mainFrame.color = outputColor->createView();
        mainFrame.color.loadOp = LoadOp::Clear;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.color.clearValue = {0.6f, 0.86666667f, 1.0f, 1.0f};
        mainFrame.depth = sceneDepth->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Clear;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        mainFrame.depth.depthClearValue = 1.0f;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglInstancingMorphVsmShadow", shadowFrame, shadowPass())
            ->renderPass("WebglInstancingMorphVsmVerticalBlur", verticalVsmFrame,
                         vsmVerticalPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglInstancingMorphVsmHorizontalBlur", horizontalVsmFrame,
                         vsmHorizontalPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglInstancingMorphMainLit", mainFrame, mainPass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 readback texture. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and every private attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(shadowDepth);
        device->freeTexture(shadowVerticalMoments);
        device->freeTexture(shadowDistribution);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};

#endif
