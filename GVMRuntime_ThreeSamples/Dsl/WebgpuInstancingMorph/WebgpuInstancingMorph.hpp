#ifndef GVM_THREE_WEBGPU_INSTANCING_MORPH_HPP
#define GVM_THREE_WEBGPU_INSTANCING_MORPH_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuInstancingMorphHorseVertexCount = 796u;
static const uint WebgpuInstancingMorphTargetCount = 15u;
static const uint WebgpuInstancingMorphShadowSize = 2048u;

/** Stores the union vertex layout for the ground and Horse entities. */
struct WebgpuInstancingMorphVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 localVertexAndReserved [[Attribute2]];
};

/** Stores camera, shadow, lighting, and entity classification state. */
struct WebgpuInstancingMorphObjectData
{
    float4x4 viewProjection;
    float4x4 shadowViewProjection;
    float4 cameraPositionAndFogNear;
    float4 cameraForwardAndFogFar;
    float4 directionalLightAndEntityKind;
};

/** Stores one transform, instance color, and all 15 morph influences. */
struct WebgpuInstancingMorphInstanceData
{
    float4x4 model;
    float4 color;
    float4 morphWeights0;
    float4 morphWeights1;
    float4 morphWeights2;
    float4 morphWeights3;
};

/** Stores the base color and Standard-material surface parameters. */
struct WebgpuInstancingMorphMaterialData
{
    float4 baseColor;
    float4 roughnessMetalnessAndFlags;
};

/** Stores cast/receive shadow flags without adding per-pass draw lists. */
struct WebgpuInstancingMorphShadowFlags
{
    uint4 values;
};

/** Defines the only RenderSet owned by the webgpu_instancing_morph Scene. */
struct WebgpuInstancingMorphSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, instances, materials, morphs, and shadow flags. */
    constructor(BufferComponent<WebgpuInstancingMorphVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebgpuInstancingMorphObjectData> objects,
                BufferComponent<WebgpuInstancingMorphInstanceData> instances,
                BufferComponent<WebgpuInstancingMorphMaterialData> materials,
                BufferComponent<float4> morphTargets,
                BufferComponent<WebgpuInstancingMorphShadowFlags> shadowFlags)
    {
    }
};

/** Binds the DSL-created raw directional shadow depth texture. */
struct WebgpuInstancingMorphLightingResources final : public IBindGroup
{
    /** Declares the sole shadow depth input. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowDepth [[Binding0]])
    {
    }
};

/** Carries the shadow clip position and caster flag. */
struct WebgpuInstancingMorphShadowVertexOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Defines the depth-only directional shadow attachment. */
struct WebgpuInstancingMorphShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries world-space Standard-material inputs to the fragment stage. */
struct WebgpuInstancingMorphMainVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float4 shadowClip [[Attribute1]];
    float4 instanceColor [[Attribute2]];
    float3 cameraPosition [[Attribute3]];
    float3 cameraForward [[Attribute4]];
    float2 fogRange [[Attribute5]];
    uint entityID [[Attribute6]];
    uint receiveShadow [[Attribute7]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebgpuInstancingMorphMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Stores fixed viewport dimensions and Inspector visibility. */
struct WebgpuInstancingMorphInspectorState
{
    float4 viewportAndEnabled;
};

/** Binds the screen-only Inspector state. */
struct WebgpuInstancingMorphInspectorResources final : public IBindGroup
{
    /** Declares the one immutable viewport and visibility record. */
    constructor(UniformBuffer<WebgpuInstancingMorphInspectorState> state [[Binding0]])
    {
    }
};

/** Carries fullscreen coordinates into the Inspector pass. */
struct WebgpuInstancingMorphInspectorVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the color-only Inspector attachment. */
struct WebgpuInstancingMorphInspectorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Returns one indexed morph influence from four packed vectors. */
float webgpuInstancingMorphWeight(WebgpuInstancingMorphInstanceData instanceData,
                                  uint target)
{
    if (target < 4u) return instanceData.morphWeights0[target];
    if (target < 8u) return instanceData.morphWeights1[target - 4u];
    if (target < 12u) return instanceData.morphWeights2[target - 8u];
    return instanceData.morphWeights3[target - 12u];
}

/** Resolves the ground position or all relative Horse morph targets. */
float3 webgpuInstancingMorphPosition(
    IN RenderSet<WebgpuInstancingMorphSceneRenderSet> sceneSet,
    WebgpuInstancingMorphVertex inputValue,
    WebgpuInstancingMorphObjectData objectData,
    WebgpuInstancingMorphInstanceData instanceData,
    uint renderEntityID,
    uint vertexID)
{
    float3 position = inputValue.position.xyz;
    if (objectData.directionalLightAndEntityKind.w > 0.5f)
    {
        for (uint target = 0u; target < WebgpuInstancingMorphTargetCount; ++target)
        {
            position += sceneSet->morphTargets->get(
                renderEntityID,
                target * WebgpuInstancingMorphHorseVertexCount +
                    uint(inputValue.localVertexAndReserved.x)).xyz *
                webgpuInstancingMorphWeight(instanceData, target);
        }
    }
    return mul(instanceData.model, float4(position, 1.0f)).xyz;
}

/** Converts one linear-light channel using Three r185 output transfer constants. */
float webgpuInstancingMorphLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces bilinear LessEqual comparison for the raw shadow depth texture. */
float webgpuInstancingMorphShadowCompare(
    IN BindGroup<WebgpuInstancingMorphLightingResources> resources,
    float2 uv,
    float compareDepth)
{
    const float2 texelPosition =
        uv * float(WebgpuInstancingMorphShadowSize) - 0.5f;
    const float2 lower = floor(texelPosition);
    const float2 fraction = texelPosition - lower;
    const uint x0 = uint(clamp(lower.x, 0.0f,
                               float(WebgpuInstancingMorphShadowSize - 1u)));
    const uint y0 = uint(clamp(lower.y, 0.0f,
                               float(WebgpuInstancingMorphShadowSize - 1u)));
    const uint x1 = min(x0 + 1u, WebgpuInstancingMorphShadowSize - 1u);
    const uint y1 = min(y0 + 1u, WebgpuInstancingMorphShadowSize - 1u);
    const float d00 = resources->shadowDepth->read(uint2(x0, y0)).x;
    const float d10 = resources->shadowDepth->read(uint2(x1, y0)).x;
    const float d01 = resources->shadowDepth->read(uint2(x0, y1)).x;
    const float d11 = resources->shadowDepth->read(uint2(x1, y1)).x;
    return lerp(lerp(compareDepth <= d00 ? 1.0f : 0.0f,
                     compareDepth <= d10 ? 1.0f : 0.0f, fraction.x),
                lerp(compareDepth <= d01 ? 1.0f : 0.0f,
                     compareDepth <= d11 ? 1.0f : 0.0f, fraction.x), fraction.y);
}

/** Evaluates the current directional shadow coordinate with fixed bias. */
float webgpuInstancingMorphShadow(
    IN BindGroup<WebgpuInstancingMorphLightingResources> resources,
    float4 shadowClip)
{
    const float3 coordinate = float3(
        shadowClip.xy / shadowClip.w * 0.5f + 0.5f,
        shadowClip.z / shadowClip.w);
    if (coordinate.x < 0.0f || coordinate.x > 1.0f ||
        coordinate.y < 0.0f || coordinate.y > 1.0f ||
        coordinate.z < 0.0f || coordinate.z > 1.0f)
    {
        return 1.0f;
    }
    return webgpuInstancingMorphShadowCompare(
        resources, coordinate.xy, coordinate.z - 0.0005f);
}

/** Draws both Scene entities into the shared directional shadow map. */
class WebgpuInstancingMorphShadowPass final : public IRenderClass
{
public:
    /** Binds only the unique Scene RenderSet. */
    constructor(RenderSet<WebgpuInstancingMorphSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Morphs every Horse instance and retains per-entity shadow flags. */
    WebgpuInstancingMorphShadowVertexOutput vertex(
        WebgpuInstancingMorphVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuInstancingMorphObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuInstancingMorphInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebgpuInstancingMorphShadowVertexOutput outputValue;
        outputValue.position = mul(
            objectData.shadowViewProjection,
            float4(webgpuInstancingMorphPosition(
                sceneSet, inputValue, objectData, instanceData,
                renderEntityID, vertexID), 1.0f));
        outputValue.castShadow =
            sceneSet->shadowFlags->get(renderEntityID, 0u).values.x;
        return outputValue;
    }

    /** Rejects the non-casting ground while preserving automatic depth output. */
    WebgpuInstancingMorphShadowFrameBuffer fragment(
        WebgpuInstancingMorphShadowVertexOutput inputValue)
    {
        if (inputValue.castShadow == 0u) discard_fragment();
        WebgpuInstancingMorphShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Draws ground and all 1,024 independently morphed Horse instances. */
class WebgpuInstancingMorphMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and its DSL-owned shadow texture. */
    constructor(RenderSet<WebgpuInstancingMorphSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebgpuInstancingMorphLightingResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance components for the main camera. */
    WebgpuInstancingMorphMainVertexOutput vertex(
        WebgpuInstancingMorphVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuInstancingMorphObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuInstancingMorphInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float3 worldPosition = webgpuInstancingMorphPosition(
            sceneSet, inputValue, objectData, instanceData,
            renderEntityID, vertexID);
        WebgpuInstancingMorphMainVertexOutput outputValue;
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
        outputValue.entityID = renderEntityID;
        outputValue.receiveShadow =
            sceneSet->shadowFlags->get(renderEntityID, 0u).values.y;
        return outputValue;
    }

    /** Evaluates flat Standard lighting, shadowing, fog, and output transfer. */
    WebgpuInstancingMorphMainFrameBuffer fragment(
        WebgpuInstancingMorphMainVertexOutput inputValue)
    {
        const WebgpuInstancingMorphObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuInstancingMorphMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        float3 normal = normalize(cross(ddx(inputValue.worldPosition),
                                        ddy(inputValue.worldPosition)));
        const float3 viewDirection = normalize(
            inputValue.cameraPosition - inputValue.worldPosition);
        if (dot(normal, viewDirection) < 0.0f) normal = -normal;
        const float3 lightDirection = normalize(
            float3(objectData.directionalLightAndEntityKind.xyz));
        const float directWeight = max(dot(normal, lightDirection), 0.0f);
        const float shadow = inputValue.receiveShadow == 0u
            ? 1.0f
            : webgpuInstancingMorphShadow(resources, inputValue.shadowClip);
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
        const float fogDepth = dot(
            inputValue.worldPosition - inputValue.cameraPosition,
            inputValue.cameraForward);
        linearColor = lerp(
            linearColor,
            float3(0.31854680f, 0.72305513f, 1.0f),
            smoothstep(inputValue.fogRange.x, inputValue.fogRange.y, fogDepth));
        const float3 displayColor = float3(
            webgpuInstancingMorphLinearToSrgb(linearColor.x),
            webgpuInstancingMorphLinearToSrgb(linearColor.y),
            webgpuInstancingMorphLinearToSrgb(linearColor.z));
        WebgpuInstancingMorphMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Composites the deterministic minimized WebGPU Inspector chrome. */
class WebgpuInstancingMorphInspectorPass final : public IRenderClass
{
public:
    /** Binds only screen state and enables source-over composition. */
    constructor(BindGroup<WebgpuInstancingMorphInspectorResources> resources [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuInstancingMorphInspectorVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstancingMorphInspectorVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked rounded bar, antialiased edge, and drop shadow. */
    WebgpuInstancingMorphInspectorFrameBuffer fragment(
        WebgpuInstancingMorphInspectorVertexOutput inputValue)
    {
        if (resources->state->viewportAndEnabled.z < 0.5f)
            discard_fragment();
        const float2 pixel = inputValue.uv *
            resources->state->viewportAndEnabled.xy;
        const float2 center = float2(723.5f, 33.5f);
        const float2 halfExtent = float2(60.5f, 18.5f);
        const float cornerRadius = pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta = abs(pixel - center) -
                             (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) - cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowCenter = float2(723.5f, 37.5f);
            const float2 shadowDelta = abs(pixel - shadowCenter) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) - cornerRadius;
            const float shadowAlpha = 0.13f * exp(
                -max(shadowDistance, 0.0f) *
                 max(shadowDistance, 0.0f) / 72.0f);
            if (shadowAlpha < 0.004f) discard_fragment();
            WebgpuInstancingMorphInspectorFrameBuffer shadowFrame;
            shadowFrame.color = half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadowFrame;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (roundedDistance > -1.0f)
        {
            color = float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuInstancingMorphInspectorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated webgpu_instancing_morph Scene and single-sample output. */
class WebgpuInstancingMorphRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuInstancingMorphSceneRenderSet> sceneSet;
    BindGroup<WebgpuInstancingMorphLightingResources> lightingResources;
    Buffer<WebgpuInstancingMorphInspectorState, BufferUsage<Uniform, CopyDst>> inspectorBuffer;
    BindGroup<WebgpuInstancingMorphInspectorResources> inspectorResources;
    RenderClass<WebgpuInstancingMorphShadowPass> shadowPass;
    RenderClass<WebgpuInstancingMorphMainPass> mainPass;
    RenderClass<WebgpuInstancingMorphInspectorPass> inspectorPass;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and its fixed 2,048-square shadow target. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuInstancingMorphSceneRenderSet>();
        shadowDepth = device->createTexture(
            "WebgpuInstancingMorphShadowDepth",
            WebgpuInstancingMorphShadowSize,
            WebgpuInstancingMorphShadowSize, 1u);
        lightingResources =
            device->createBindGroup<WebgpuInstancingMorphLightingResources>(
                shadowDepth->createView());
        shadowPass =
            device->createRenderClass<WebgpuInstancingMorphShadowPass>(sceneSet);
        mainPass =
            device->createRenderClass<WebgpuInstancingMorphMainPass>(
                sceneSet, lightingResources);
        inspectorBuffer = device->createBuffer(
            "WebgpuInstancingMorphInspectorState", 1u);
        inspectorResources =
            device->createBindGroup<WebgpuInstancingMorphInspectorResources>(
                inspectorBuffer);
        inspectorPass =
            device->createRenderClass<WebgpuInstancingMorphInspectorPass>(
                inspectorResources);
    }

    /** Allocates single-sample color and depth attachments for the fixed capture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuInstancingMorphOutput", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuInstancingMorphSceneDepth", width, height, 1u);
    }

    /** Uploads the scenario-specific Inspector visibility without changing Scene data. */
    void configureInspector(float enabled)
    {
        WebgpuInstancingMorphInspectorState state;
        state.viewportAndEnabled =
            float4(float(width), float(height), enabled, 0.0f);
        graphicsQueue->writeBuffer(
            BufferRange(inspectorBuffer), &state, sizeof(state))->submit();
    }

    /** Updates the Set, renders shadow and main passes, and presents RGBA8. */
    void render() override
    {
        sceneSet->update();
        WebgpuInstancingMorphShadowFrameBuffer shadowFrame;
        shadowFrame.depth = shadowDepth->createView();
        shadowFrame.depth.depthLoadOp = LoadOp::Clear;
        shadowFrame.depth.depthStoreOp = StoreOp::Store;
        shadowFrame.depth.depthClearValue = 1.0f;
        WebgpuInstancingMorphMainFrameBuffer mainFrame;
        mainFrame.color = outputColor->createView();
        mainFrame.color.loadOp = LoadOp::Clear;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.color.clearValue = {0.6f, 0.86666667f, 1.0f, 1.0f};
        mainFrame.depth = sceneDepth->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Clear;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        mainFrame.depth.depthClearValue = 1.0f;
        WebgpuInstancingMorphInspectorFrameBuffer inspectorFrame;
        inspectorFrame.color = outputColor->createView();
        inspectorFrame.color.loadOp = LoadOp::Load;
        inspectorFrame.color.storeOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuInstancingMorphShadow", shadowFrame, shadowPass())
            ->renderPass("WebgpuInstancingMorphMain", mainFrame, mainPass())
            ->renderPass("WebgpuInstancingMorphInspector", inspectorFrame,
                         inspectorPass(3u, 1u, 0u, 0u))
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
        device->freeBuffer(inspectorBuffer);
        device->freeTexture(shadowDepth);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};

#endif
