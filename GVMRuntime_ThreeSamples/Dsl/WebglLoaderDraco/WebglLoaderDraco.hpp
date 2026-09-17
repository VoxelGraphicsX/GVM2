#ifndef GVM_THREE_WEBGL_LOADER_DRACO_HPP
#define GVM_THREE_WEBGL_LOADER_DRACO_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderDracoShadowMapSize = 512u;

/** Stores one position and generated smooth normal from the consolidated Draco Scene geometry. */
struct WebglLoaderDracoVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores one entity transform and the current main and spotlight shadow cameras. */
struct WebglLoaderDracoObjectData
{
    float4x4 model;
    float4x4 normalMatrix;
    float4x4 viewProjection;
    float4x4 shadowViewProjection;
    float4 cameraPositionAndFogNear;
    float4 cameraForwardAndFogFar;
};

/** Stores the non-instanced translation entry required by the common Scene schema. */
struct WebglLoaderDracoInstanceData
{
    float4 translation;
};

/** Stores one linear base color and material/shadow phase flags. */
struct WebglLoaderDracoMaterialData
{
    float4 baseColor;
    uint4 flags;
};

/** Defines the unique RenderSet reused by every geometry pass in the Draco Scene. */
struct WebglLoaderDracoSceneRenderSet : public IRenderSet
{
    /** Declares the unified geometry and per-entity component stores. */
    constructor(BufferComponent<WebglLoaderDracoVertex> vertices [[RenderSetVertexBuffer]], BufferComponent<uint> indices [[RenderSetIndexBuffer]], BufferComponent<WebglLoaderDracoObjectData> objects, BufferComponent<WebglLoaderDracoInstanceData> instances, BufferComponent<WebglLoaderDracoMaterialData> materials)
    {
    }
};

/** Binds the spotlight shadow depth texture sampled by the main Scene pass. */
struct WebglLoaderDracoMainResources final : public IBindGroup
{
    /** Declares the immutable current-frame spotlight shadow map. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowMap [[Binding0]])
    {
    }
};

/** Carries the caster flag from the RenderSet vertex stage to fragment discard. */
struct WebglLoaderDracoShadowVertexOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Defines the depth-only spotlight shadow framebuffer. */
struct WebglLoaderDracoShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries lighting, shadow, fog, and entity inputs to the main fragment stage. */
struct WebglLoaderDracoMainVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float4 shadowClip [[Attribute2]];
    float3 cameraPosition [[Attribute3]];
    float3 cameraForward [[Attribute4]];
    uint entityID [[Attribute5]];
};

/** Defines the single-sample final Scene color and depth framebuffer. */
struct WebglLoaderDracoMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's output transfer constants. */
float webglLoaderDracoLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglLoaderDracoFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Evaluates the roughness-one direct GGX term used by the bunny material. */
float3 webglLoaderDracoPhysicalSpecular(float3 lightDirection, float3 viewDirection, float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNormalView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotViewHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float visibility = 0.5f / max(dotNormalLight + dotNormalView, 0.000001f);
    return webglLoaderDracoFresnel(float3(0.04f), dotViewHalf) * (visibility * 0.3183098861837907f);
}

/** Reads one clamped texel from the current spotlight shadow map. */
float webglLoaderDracoReadShadow(IN BindGroup<WebglLoaderDracoMainResources> resources, uint2 texel)
{
    return resources->shadowMap->read(texel).x;
}

/** Reproduces one bilinear LessEqual comparison over raw shadow depth texels. */
float webglLoaderDracoCompareShadow(IN BindGroup<WebglLoaderDracoMainResources> resources, float2 uv, float compareDepth)
{
    const float2 texelPosition = uv * float(WebglLoaderDracoShadowMapSize) - 0.5f;
    const float2 lowerPosition = floor(texelPosition);
    const float2 fraction = texelPosition - lowerPosition;
    const uint x0 = uint(clamp(lowerPosition.x, 0.0f, float(WebglLoaderDracoShadowMapSize - 1u)));
    const uint y0 = uint(clamp(lowerPosition.y, 0.0f, float(WebglLoaderDracoShadowMapSize - 1u)));
    const uint x1 = min(x0 + 1u, WebglLoaderDracoShadowMapSize - 1u);
    const uint y1 = min(y0 + 1u, WebglLoaderDracoShadowMapSize - 1u);
    const float a = compareDepth <= webglLoaderDracoReadShadow(resources, uint2(x0, y0)) ? 1.0f : 0.0f;
    const float b = compareDepth <= webglLoaderDracoReadShadow(resources, uint2(x1, y0)) ? 1.0f : 0.0f;
    const float c = compareDepth <= webglLoaderDracoReadShadow(resources, uint2(x0, y1)) ? 1.0f : 0.0f;
    const float d = compareDepth <= webglLoaderDracoReadShadow(resources, uint2(x1, y1)) ? 1.0f : 0.0f;
    return lerp(lerp(a, b, fraction.x), lerp(c, d, fraction.x), fraction.y);
}

/** Returns one of Three r185's five rotated Vogel-disk shadow offsets. */
float2 webglLoaderDracoVogelDiskSample(uint sampleIndex, float phase)
{
    const float radius = sqrt((float(sampleIndex) + 0.5f) * 0.2f);
    const float theta = float(sampleIndex) * 2.399963229728653f + phase;
    return float2(cos(theta), sin(theta)) * radius;
}

/** Evaluates the exact five-sample randomized spotlight PCF kernel used by r185. */
float webglLoaderDracoShadow(IN BindGroup<WebglLoaderDracoMainResources> resources, float4 shadowClip, float2 finalPixelPosition)
{
    const float3 ndc = shadowClip.xyz / shadowClip.w;
    const float3 coordinate = float3(ndc.xy * 0.5f + 0.5f, ndc.z);
    if (coordinate.x < 0.0f || coordinate.x > 1.0f || coordinate.y < 0.0f || coordinate.y > 1.0f || coordinate.z < 0.0f || coordinate.z > 1.0f)
    {
        return 1.0f;
    }
    const float phase = frac(52.9829189f * frac(dot(finalPixelPosition, float2(0.06711056f, 0.00583715f)))) * 6.283185307179586f;
    const float radius = 8.0f / float(WebglLoaderDracoShadowMapSize);
    return (webglLoaderDracoCompareShadow(resources, coordinate.xy + webglLoaderDracoVogelDiskSample(0u, phase) * radius, coordinate.z) +
            webglLoaderDracoCompareShadow(resources, coordinate.xy + webglLoaderDracoVogelDiskSample(1u, phase) * radius, coordinate.z) +
            webglLoaderDracoCompareShadow(resources, coordinate.xy + webglLoaderDracoVogelDiskSample(2u, phase) * radius, coordinate.z) +
            webglLoaderDracoCompareShadow(resources, coordinate.xy + webglLoaderDracoVogelDiskSample(3u, phase) * radius, coordinate.z) +
            webglLoaderDracoCompareShadow(resources, coordinate.xy + webglLoaderDracoVogelDiskSample(4u, phase) * radius, coordinate.z)) * 0.2f;
}

/** Draws the bunny caster through the unique Scene RenderSet. */
class WebglLoaderDracoShadowPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and configures a conventional depth-only pass. */
    constructor(RenderSet<WebglLoaderDracoSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one entity through the spotlight camera and exposes its caster flag. */
    WebglLoaderDracoShadowVertexOutput vertex(WebglLoaderDracoVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]])
    {
        const WebglLoaderDracoObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderDracoInstanceData instanceData = sceneSet->instances->get(renderEntityID, 0u);
        const WebglLoaderDracoMaterialData materialData = sceneSet->materials->get(renderEntityID, 0u);
        const float4 worldPosition = mul(objectData.model, inputValue.position + float4(instanceData.translation.xyz, 0.0f));
        WebglLoaderDracoShadowVertexOutput outputValue;
        outputValue.position = mul(objectData.shadowViewProjection, worldPosition);
        outputValue.castShadow = materialData.flags.y;
        return outputValue;
    }

    /** Discards the authored non-casting ground while retaining hardware depth output. */
    WebglLoaderDracoShadowFrameBuffer fragment(WebglLoaderDracoShadowVertexOutput inputValue)
    {
        if (inputValue.castShadow == 0u)
        {
            discard_fragment();
        }
        WebglLoaderDracoShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Draws the Lambert ground and Standard bunny with spotlight shadow, hemisphere light, and fog. */
class WebglLoaderDracoMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and its DSL-produced spotlight shadow map. */
    constructor(RenderSet<WebglLoaderDracoSceneRenderSet> sceneSet [[Slot0]], BindGroup<WebglLoaderDracoMainResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity components and emits all world-space lighting inputs. */
    WebglLoaderDracoMainVertexOutput vertex(WebglLoaderDracoVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]])
    {
        const WebglLoaderDracoObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderDracoInstanceData instanceData = sceneSet->instances->get(renderEntityID, 0u);
        const float4 worldPosition = mul(objectData.model, inputValue.position + float4(instanceData.translation.xyz, 0.0f));
        WebglLoaderDracoMainVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = mul(objectData.normalMatrix, inputValue.normal).xyz;
        outputValue.shadowClip = mul(objectData.shadowViewProjection, worldPosition);
        outputValue.cameraPosition = objectData.cameraPositionAndFogNear.xyz;
        outputValue.cameraForward = objectData.cameraForwardAndFogFar.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the two materials, r185 light attenuation, output transfer, and linear fog. */
    WebglLoaderDracoMainFrameBuffer fragment(WebglLoaderDracoMainVertexOutput inputValue)
    {
        const WebglLoaderDracoMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 viewDirection = normalize(inputValue.cameraPosition - inputValue.worldPosition);
        const float3 lightVector = float3(-1.0f, -1.0f, 1.0f) - inputValue.worldPosition;
        const float lightDistance = length(lightVector);
        const float3 lightDirection = lightVector / lightDistance;
        const float angleCos = dot(lightDirection, normalize(float3(-1.0f, -1.0f, 1.0f)));
        const float spotAttenuation = smoothstep(0.9807852804f, 0.9951847267f, angleCos);
        const float shadow = materialData.flags.z == 0u ? 1.0f : webglLoaderDracoShadow(resources, inputValue.shadowClip, inputValue.position.xy);
        const float dotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
        const float distanceAttenuation = 1.0f / max(lightDistance * lightDistance, 0.01f);
        const float3 directIrradiance = float3(7.0f) * (distanceAttenuation * spotAttenuation * dotLight * shadow);
        const float hemisphereWeight = dot(normal, float3(0.0f, -1.0f, 0.0f)) * 0.5f + 0.5f;
        const float3 hemisphereIrradiance = lerp(float3(0.0666265f, 0.0666265f, 0.13286832f), float3(0.26635566f, 0.20155625f, 0.20155625f), hemisphereWeight) * 3.0f;
        float3 linearColor = (directIrradiance + hemisphereIrradiance) * materialData.baseColor.xyz * 0.3183098861837907f;
        if (materialData.flags.x == 1u)
        {
            linearColor += directIrradiance * webglLoaderDracoPhysicalSpecular(lightDirection, viewDirection, normal);
        }
        float3 displayColor = float3(webglLoaderDracoLinearToSrgb(linearColor.x), webglLoaderDracoLinearToSrgb(linearColor.y), webglLoaderDracoLinearToSrgb(linearColor.z));
        const float fogDepth = dot(inputValue.worldPosition - inputValue.cameraPosition, inputValue.cameraForward);
        displayColor = lerp(displayColor, float3(0.26666668f, 0.2f, 0.2f), smoothstep(1.0f, 4.0f, fogDepth));
        WebglLoaderDracoMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Draco Scene RenderSet and its shadow, main, and present resources. */
class WebglLoaderDracoRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderDracoSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderDracoShadowPass> shadowPass;
    RenderClass<WebglLoaderDracoMainPass> mainPass;
    BindGroup<WebglLoaderDracoMainResources> mainResources;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Set and fixed spotlight shadow resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderDracoSceneRenderSet>();
        shadowDepth = device->createTexture("WebglLoaderDracoShadowDepth", WebglLoaderDracoShadowMapSize, WebglLoaderDracoShadowMapSize, 1u);
        shadowPass = device->createRenderClass<WebglLoaderDracoShadowPass>(sceneSet);
    }

    /** Allocates single-sample capture targets and the main shadow binding. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("WebglLoaderDracoOutputRGBA8", width, height, 1u);
        sceneDepth = device->createTexture("WebglLoaderDracoSceneDepth32", width, height, 1u);
        mainResources = device->createBindGroup<WebglLoaderDracoMainResources>(shadowDepth->createView());
        mainPass = device->createRenderClass<WebglLoaderDracoMainPass>(sceneSet, mainResources);
    }

    /** Updates the Set and executes one shadow and one single-sample main draw. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderDracoShadowFrameBuffer shadowFrameBuffer;
        shadowFrameBuffer.depth = shadowDepth->createView();
        shadowFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLoaderDracoMainFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = outputColor->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Clear;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.color.clearValue = {0.26666668f, 0.2f, 0.2f, 1.0f};
        mainFrameBuffer.depth = sceneDepth->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        mainFrameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebglLoaderDracoShadowDepth", shadowFrameBuffer, shadowPass())
            ->renderPass("WebglLoaderDracoMain", mainFrameBuffer, mainPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 texture for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Set and every private shadow and capture resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(shadowDepth);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
