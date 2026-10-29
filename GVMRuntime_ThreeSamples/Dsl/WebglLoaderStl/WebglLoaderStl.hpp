#ifndef GVM_THREE_WEBGL_LOADER_STL_HPP
#define GVM_THREE_WEBGL_LOADER_STL_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderStlShadowMapSize = 1024u;

/** Stores one position, facet normal, and optional linear vertex color from the consolidated STL scene geometry. */
struct WebglLoaderStlVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
};

/** Stores one entity transform plus the current camera and both directional shadow cameras. */
struct WebglLoaderStlObjectData
{
    float4x4 model;
    float4x4 normalMatrix;
    float4x4 viewProjection;
    float4x4 shadowViewProjection0;
    float4x4 shadowViewProjection1;
    float4 cameraPositionAndFogNear;
    float4 cameraForwardAndFogFar;
};

/** Stores the non-instanced translation entry required by the unified Scene component schema. */
struct WebglLoaderStlInstanceData
{
    float4 translation;
};

/** Stores the linear base color and Phong specular parameters for one entity. */
struct WebglLoaderStlMaterialData
{
    float4 baseColor;
    float4 specularAndSurfaceParameter;
};

/** Stores vertex-color, cast-shadow, receive-shadow, and logical entity phase flags. */
struct WebglLoaderStlRenderFlags
{
    uint4 values;
};

/** Defines the unique RenderSet shared by every geometry pass for the STL Scene. */
struct WebglLoaderStlSceneRenderSet : public IRenderSet
{
    /** Declares the single vertex/index stores and all per-entity Scene components. */
    constructor(BufferComponent<WebglLoaderStlVertex> vertices [[RenderSetVertexBuffer]], BufferComponent<uint> indices [[RenderSetIndexBuffer]], BufferComponent<WebglLoaderStlObjectData> objects, BufferComponent<WebglLoaderStlInstanceData> instances, BufferComponent<WebglLoaderStlMaterialData> materials, BufferComponent<WebglLoaderStlRenderFlags> renderFlags)
    {
    }
};

/** Selects one of the two frozen directional-light matrices for a shadow invocation. */
struct WebglLoaderStlShadowInvocationData
{
    uint4 lightIndexAndReserved;
};

/** Binds only the shadow invocation selector in addition to the unique Scene RenderSet. */
struct WebglLoaderStlShadowInvocationBindGroup final : public IBindGroup
{
    /** Declares the immutable per-invocation selector uniform. */
    constructor(UniformBuffer<WebglLoaderStlShadowInvocationData> invocation [[Binding0]])
    {
    }
};

/** Binds the two depth textures produced by the Scene shadow invocations. */
struct WebglLoaderStlLitResources final : public IBindGroup
{
    /** Declares both current-frame directional shadow maps as sampled depth textures. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowMap0 [[Binding0]], Texture2D<TextureFormat::Depth32Float> shadowMap1 [[Binding1]])
    {
    }
};

/** Carries caster phase metadata from the shadow vertex stage to fragment discard. */
struct WebglLoaderStlShadowVertexOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Defines a depth-only framebuffer for one directional shadow invocation. */
struct WebglLoaderStlShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries world-space lighting, camera, shadow, material, and phase inputs. */
struct WebglLoaderStlLitVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 smoothWorldNormal [[Attribute1]];
    float4 shadowClip0 [[Attribute2]];
    float4 shadowClip1 [[Attribute3]];
    float3 cameraPosition [[Attribute4]];
    float3 cameraForward [[Attribute5]];
    uint2 entityAndPackedPhase [[Attribute6]];
    float3 vertexColor [[Attribute7]];
};

/** Defines the single-sample color and depth framebuffer for the main Scene invocation. */
struct WebglLoaderStlLitFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear-light channel with the exact Three r185 output transfer constants. */
float webglLoaderStlLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation for an RGB F0. */
float3 webglLoaderStlFresnelSchlick(float3 f0, float f90, float dotViewHalf)
{
    const float fresnel = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(f90) * fresnel;
}

/** Returns one exact half-float row from Three r185's 16x16 DFG LUT at roughness one. */
float2 webglLoaderStlDfgRoughnessOneRow(uint row)
{
    if (row == 0u)
        return float2(0.85986328125f, 0.039031982421875f);
    if (row == 1u)
        return float2(0.7548828125f, 0.0269775390625f);
    if (row == 2u)
        return float2(0.68359375f, 0.019439697265625f);
    if (row == 3u)
        return float2(0.62841796875f, 0.01419830322265625f);
    if (row == 4u)
        return float2(0.583984375f, 0.01041412353515625f);
    if (row == 5u)
        return float2(0.54638671875f, 0.007633209228515625f);
    if (row == 6u)
        return float2(0.51416015625f, 0.005565643310546875f);
    if (row == 7u)
        return float2(0.486083984375f, 0.00402069091796875f);
    if (row == 8u)
        return float2(0.46142578125f, 0.0028629302978515625f);
    if (row == 9u)
        return float2(0.439453125f, 0.0020046234130859375f);
    if (row == 10u)
        return float2(0.419677734375f, 0.00136566162109375f);
    if (row == 11u)
        return float2(0.402099609375f, 0.0008993148803710938f);
    if (row == 12u)
        return float2(0.385986328125f, 0.0005650520324707031f);
    if (row == 13u)
        return float2(0.37109375f, 0.0003311634063720703f);
    if (row == 14u)
        return float2(0.357666015625f, 0.00017201900482177734f);
    return float2(0.34521484375f, 0.00007051229476928711f);
}

/** Reproduces normalized linear sampling of the DFG LUT's roughness-one edge column. */
float2 webglLoaderStlDfgRoughnessOne(float dotNormalView)
{
    const float texelCoordinate = clamp(dotNormalView, 0.0f, 1.0f) * 16.0f - 0.5f;
    if (texelCoordinate <= 0.0f)
        return webglLoaderStlDfgRoughnessOneRow(0u);
    if (texelCoordinate >= 15.0f)
        return webglLoaderStlDfgRoughnessOneRow(15u);
    const float lowerCoordinate = floor(texelCoordinate);
    const uint lowerRow = uint(lowerCoordinate);
    return lerp(webglLoaderStlDfgRoughnessOneRow(lowerRow), webglLoaderStlDfgRoughnessOneRow(lowerRow + 1u), texelCoordinate - lowerCoordinate);
}

/** Evaluates Three r185's roughness-one GGX BRDF including its DFG multiscatter term. */
float3 webglLoaderStlPhysicalSpecular(float3 lightDirection, float3 viewDirection, float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNormalView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotViewHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 f0 = float3(0.04f);
    const float3 fresnel = webglLoaderStlFresnelSchlick(f0, 1.0f, dotViewHalf);
    const float visibility = 0.5f / max(dotNormalLight + dotNormalView, 0.000001f);
    const float distribution = 0.3183098861837907f;
    const float3 singleScatter = fresnel * (visibility * distribution);

    const float2 dfgView = webglLoaderStlDfgRoughnessOne(dotNormalView);
    const float2 dfgLight = webglLoaderStlDfgRoughnessOne(dotNormalLight);
    const float3 viewEnergy = f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy = f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissingEnergy = 1.0f - dfgView.x - dfgView.y;
    const float lightMissingEnergy = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel = f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multipleFresnel = viewEnergy * lightEnergy * averageFresnel / (float3(1.0f) - averageFresnel * (viewMissingEnergy * lightMissingEnergy) + float3(0.000001f));
    return singleScatter + multipleFresnel * (viewMissingEnergy * lightMissingEnergy);
}

/** Evaluates Three r185's default MeshPhong Blinn-Phong BRDF for the ground. */
float3 webglLoaderStlPhongSpecular(float3 lightDirection, float3 viewDirection, float3 normal, float3 specularColor, float shininess)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotViewHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 fresnel = webglLoaderStlFresnelSchlick(specularColor, 1.0f, dotViewHalf);
    const float distribution = 0.3183098861837907f * (shininess * 0.5f + 1.0f) * pow(dotNormalHalf, shininess);
    return fresnel * (0.25f * distribution);
}

/** Reads and clamps one depth texel from either current directional shadow map. */
float webglLoaderStlReadShadowDepth(IN BindGroup<WebglLoaderStlLitResources> resources, uint lightIndex, uint2 texel)
{
    if (lightIndex == 0u)
    {
        return resources->shadowMap0->read(texel).x;
    }
    return resources->shadowMap1->read(texel).x;
}

/** Reproduces one hardware bilinear LessEqual shadow comparison from raw depth texels. */
float webglLoaderStlBilinearShadowCompare(IN BindGroup<WebglLoaderStlLitResources> resources, uint lightIndex, float2 uv, float compareDepth)
{
    const float2 texelPosition = uv * float(WebglLoaderStlShadowMapSize) - 0.5f;
    const float2 lowerPosition = floor(texelPosition);
    const float2 fraction = texelPosition - lowerPosition;
    const uint x0 = uint(clamp(lowerPosition.x, 0.0f, float(WebglLoaderStlShadowMapSize - 1u)));
    const uint y0 = uint(clamp(lowerPosition.y, 0.0f, float(WebglLoaderStlShadowMapSize - 1u)));
    const uint x1 = min(x0 + 1u, WebglLoaderStlShadowMapSize - 1u);
    const uint y1 = min(y0 + 1u, WebglLoaderStlShadowMapSize - 1u);
    const float compare00 = compareDepth <= webglLoaderStlReadShadowDepth(resources, lightIndex, uint2(x0, y0)) ? 1.0f : 0.0f;
    const float compare10 = compareDepth <= webglLoaderStlReadShadowDepth(resources, lightIndex, uint2(x1, y0)) ? 1.0f : 0.0f;
    const float compare01 = compareDepth <= webglLoaderStlReadShadowDepth(resources, lightIndex, uint2(x0, y1)) ? 1.0f : 0.0f;
    const float compare11 = compareDepth <= webglLoaderStlReadShadowDepth(resources, lightIndex, uint2(x1, y1)) ? 1.0f : 0.0f;
    return lerp(lerp(compare00, compare10, fraction.x), lerp(compare01, compare11, fraction.x), fraction.y);
}

/** Returns one of Three r185's five rotated Vogel-disk shadow offsets. */
float2 webglLoaderStlVogelDiskSample(uint sampleIndex, float phase)
{
    const float radius = sqrt((float(sampleIndex) + 0.5f) * 0.2f);
    const float theta = float(sampleIndex) * 2.399963229728653f + phase;
    return float2(cos(theta), sin(theta)) * radius;
}

/** Evaluates Three r185's five-tap PCF directional shadow for one Scene fragment. */
float webglLoaderStlDirectionalShadow(IN BindGroup<WebglLoaderStlLitResources> resources, uint lightIndex, float4 shadowClip, float2 finalPixelPosition)
{
    const float3 shadowNdc = shadowClip.xyz / shadowClip.w;
    const float3 shadowCoordinate = float3(shadowNdc.xy * 0.5f + 0.5f, shadowNdc.z);
    const bool inFrustum = shadowCoordinate.x >= 0.0f && shadowCoordinate.x <= 1.0f && shadowCoordinate.y >= 0.0f && shadowCoordinate.y <= 1.0f && shadowCoordinate.z >= 0.0f && shadowCoordinate.z <= 1.0f;
    if (!inFrustum)
    {
        return 1.0f;
    }
    const float phase = frac(52.9829189f * frac(dot(finalPixelPosition, float2(0.06711056f, 0.00583715f)))) * 6.283185307179586f;
    const float radius = 1.0f / float(WebglLoaderStlShadowMapSize);
    const float compareDepth = shadowCoordinate.z - 0.001f;
    return (webglLoaderStlBilinearShadowCompare(resources, lightIndex, shadowCoordinate.xy + webglLoaderStlVogelDiskSample(0u, phase) * radius, compareDepth) + webglLoaderStlBilinearShadowCompare(resources, lightIndex, shadowCoordinate.xy + webglLoaderStlVogelDiskSample(1u, phase) * radius, compareDepth) + webglLoaderStlBilinearShadowCompare(resources, lightIndex, shadowCoordinate.xy + webglLoaderStlVogelDiskSample(2u, phase) * radius, compareDepth) +
            webglLoaderStlBilinearShadowCompare(resources, lightIndex, shadowCoordinate.xy + webglLoaderStlVogelDiskSample(3u, phase) * radius, compareDepth) + webglLoaderStlBilinearShadowCompare(resources, lightIndex, shadowCoordinate.xy + webglLoaderStlVogelDiskSample(4u, phase) * radius, compareDepth)) *
           0.2f;
}

/** Draws one directional shadow invocation through the Scene's unique RenderSet. */
class WebglLoaderStlShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and one immutable directional-light selector. */
    constructor(RenderSet<WebglLoaderStlSceneRenderSet> sceneSet [[Slot0]], BindGroup<WebglLoaderStlShadowInvocationBindGroup> invocationResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity components and transforms every caster through the selected light camera. */
    WebglLoaderStlShadowVertexOutput vertex(WebglLoaderStlVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]])
    {
        const WebglLoaderStlObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderStlInstanceData instanceData = sceneSet->instances->get(renderEntityID, 0u);
        const WebglLoaderStlRenderFlags flags = sceneSet->renderFlags->get(renderEntityID, 0u);
        const float4 localPosition = inputValue.position + float4(instanceData.translation.xyz, 0.0f);
        const float4 worldPosition = mul(objectData.model, localPosition);
        const uint lightIndex = invocationResources->invocation->lightIndexAndReserved.x;

        WebglLoaderStlShadowVertexOutput outputValue;
        outputValue.position = lightIndex == 0u ? mul(objectData.shadowViewProjection0, worldPosition) : mul(objectData.shadowViewProjection1, worldPosition);
        outputValue.castShadow = flags.values.y;
        return outputValue;
    }

    /** Discards the non-casting ground while retaining automatic hardware depth writes. */
    WebglLoaderStlShadowFrameBuffer fragment(WebglLoaderStlShadowVertexOutput inputValue)
    {
        if (inputValue.castShadow == 0u)
        {
            discard_fragment();
        }
        WebglLoaderStlShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Draws the ground and four STL meshes with r185 Phong lighting, shadows, vertex color, and fog. */
class WebglLoaderStlPhongPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and the two DSL-created shadow depth textures. */
    constructor(RenderSet<WebglLoaderStlSceneRenderSet> sceneSet [[Slot0]], BindGroup<WebglLoaderStlLitResources> lightingResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the six-component entity schema and emits both shadow coordinates. */
    WebglLoaderStlLitVertexOutput vertex(WebglLoaderStlVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]])
    {
        const WebglLoaderStlObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderStlInstanceData instanceData = sceneSet->instances->get(renderEntityID, 0u);
        const WebglLoaderStlRenderFlags flags = sceneSet->renderFlags->get(renderEntityID, 0u);
        const float4 localPosition = inputValue.position + float4(instanceData.translation.xyz, 0.0f);
        const float4 worldPosition = mul(objectData.model, localPosition);

        WebglLoaderStlLitVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.smoothWorldNormal = mul(objectData.normalMatrix, inputValue.normal).xyz;
        outputValue.shadowClip0 = mul(objectData.shadowViewProjection0, worldPosition);
        outputValue.shadowClip1 = mul(objectData.shadowViewProjection1, worldPosition);
        outputValue.cameraPosition = objectData.cameraPositionAndFogNear.xyz;
        outputValue.cameraForward = objectData.cameraForwardAndFogFar.xyz;
        outputValue.entityAndPackedPhase = uint2(renderEntityID, flags.values.x | (flags.values.z << 8u));
        outputValue.vertexColor = inputValue.color.xyz;
        return outputValue;
    }

    /** Evaluates r185 Phong light accumulation, PCF shadows, vertex color, output transfer, and fog. */
    WebglLoaderStlLitFrameBuffer fragment(WebglLoaderStlLitVertexOutput inputValue)
    {
        const WebglLoaderStlMaterialData materialData = sceneSet->materials->get(inputValue.entityAndPackedPhase.x, 0u);
        const uint useVertexColor = inputValue.entityAndPackedPhase.y & 0xffu;
        const uint receiveShadow = inputValue.entityAndPackedPhase.y >> 8u;
        const float3 normal = normalize(inputValue.smoothWorldNormal);
        const float3 viewDirection = normalize(inputValue.cameraPosition - inputValue.worldPosition);
        const float3 lightDirection0 = normalize(float3(1.0f, -1.0f, 1.0f));
        const float3 lightDirection1 = normalize(float3(0.5f, -1.0f, -1.0f));
        const float shadow0 = receiveShadow == 0u ? 1.0f : webglLoaderStlDirectionalShadow(lightingResources, 0u, inputValue.shadowClip0, inputValue.position.xy * 0.5f);
        const float shadow1 = receiveShadow == 0u ? 1.0f : webglLoaderStlDirectionalShadow(lightingResources, 1u, inputValue.shadowClip1, inputValue.position.xy * 0.5f);
        const float dotLight0 = clamp(dot(normal, lightDirection0), 0.0f, 1.0f);
        const float dotLight1 = clamp(dot(normal, lightDirection1), 0.0f, 1.0f);
        const float3 directIrradiance0 = float3(3.5f) * (dotLight0 * shadow0);
        const float3 directIrradiance1 = float3(3.0f, 1.99616194f, 0.0f) * (dotLight1 * shadow1);
        const float hemisphereWeight = dot(normal, float3(0.0f, -1.0f, 0.0f)) * 0.5f + 0.5f;
        const float3 hemisphereIrradiance = lerp(float3(0.19987781f, 0.19987781f, 0.39860496f), float3(0.79906684f, 0.60466874f, 0.60466874f), hemisphereWeight);
        const float inversePi = 0.3183098861837907f;
        const float3 diffuseColor = materialData.baseColor.xyz *
            (useVertexColor == 0u ? float3(1.0f) : inputValue.vertexColor);
        float3 linearColor = (directIrradiance0 + directIrradiance1 + hemisphereIrradiance) * diffuseColor * inversePi;
        linearColor += directIrradiance0 * webglLoaderStlPhongSpecular(lightDirection0, viewDirection, normal, materialData.specularAndSurfaceParameter.xyz, materialData.specularAndSurfaceParameter.w) + directIrradiance1 * webglLoaderStlPhongSpecular(lightDirection1, viewDirection, normal, materialData.specularAndSurfaceParameter.xyz, materialData.specularAndSurfaceParameter.w);

        float3 displayColor = float3(webglLoaderStlLinearToSrgb(linearColor.x), webglLoaderStlLinearToSrgb(linearColor.y), webglLoaderStlLinearToSrgb(linearColor.z));
        const float fogDepth = dot(inputValue.worldPosition - inputValue.cameraPosition, inputValue.cameraForward);
        const float fogFactor = smoothstep(2.0f, 15.0f, fogDepth);
        displayColor = lerp(displayColor, float3(0.44705883f, 0.39215687f, 0.35686275f), fogFactor);

        WebglLoaderStlLitFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique STL Scene RenderSet and all shadow, lighting, and present work. */
class WebglLoaderStlRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderStlSceneRenderSet> sceneSet;
    Buffer<WebglLoaderStlShadowInvocationData, BufferUsage<Uniform, CopyDst>> shadowInvocationBuffer0;
    Buffer<WebglLoaderStlShadowInvocationData, BufferUsage<Uniform, CopyDst>> shadowInvocationBuffer1;
    BindGroup<WebglLoaderStlShadowInvocationBindGroup> shadowInvocationResources0;
    BindGroup<WebglLoaderStlShadowInvocationBindGroup> shadowInvocationResources1;
    BindGroup<WebglLoaderStlLitResources> litResources;
    RenderClass<WebglLoaderStlShadowDepthPass> shadowPass0;
    RenderClass<WebglLoaderStlShadowDepthPass> shadowPass1;
    RenderClass<WebglLoaderStlPhongPass> litPass;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth0;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth1;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Set, both shadow invocations, and immutable DSL resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderStlSceneRenderSet>();
        shadowInvocationBuffer0 = device->createBuffer("WebglLoaderStlShadowInvocation0", 1u);
        shadowInvocationBuffer1 = device->createBuffer("WebglLoaderStlShadowInvocation1", 1u);
        shadowInvocationResources0 = device->createBindGroup<WebglLoaderStlShadowInvocationBindGroup>(shadowInvocationBuffer0);
        shadowInvocationResources1 = device->createBindGroup<WebglLoaderStlShadowInvocationBindGroup>(shadowInvocationBuffer1);
        shadowPass0 = device->createRenderClass<WebglLoaderStlShadowDepthPass>(sceneSet, shadowInvocationResources0);
        shadowPass1 = device->createRenderClass<WebglLoaderStlShadowDepthPass>(sceneSet, shadowInvocationResources1);
        shadowDepth0 = device->createTexture("WebglLoaderStlShadowDepth0", WebglLoaderStlShadowMapSize, WebglLoaderStlShadowMapSize, 1u);
        shadowDepth1 = device->createTexture("WebglLoaderStlShadowDepth1", WebglLoaderStlShadowMapSize, WebglLoaderStlShadowMapSize, 1u);
    }

    /** Allocates the fixed single-sample Scene color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("WebglLoaderStlOutputRGBA8", width, height, 1u);
        sceneDepth = device->createTexture("WebglLoaderStlSceneDepth32", width, height, 1u);
        litResources = device->createBindGroup<WebglLoaderStlLitResources>(shadowDepth0->createView(), shadowDepth1->createView());
        litPass = device->createRenderClass<WebglLoaderStlPhongPass>(sceneSet, litResources);
    }

    /** Updates the Set and executes two shadow draws and one single-sample lit draw. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderStlShadowInvocationData invocation0;
        invocation0.lightIndexAndReserved = uint4(0u, 0u, 0u, 0u);
        WebglLoaderStlShadowInvocationData invocation1;
        invocation1.lightIndexAndReserved = uint4(1u, 0u, 0u, 0u);

        WebglLoaderStlShadowFrameBuffer shadowFrameBuffer0;
        shadowFrameBuffer0.depth = shadowDepth0->createView();
        shadowFrameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer0.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer0.depth.depthClearValue = 1.0f;
        WebglLoaderStlShadowFrameBuffer shadowFrameBuffer1;
        shadowFrameBuffer1.depth = shadowDepth1->createView();
        shadowFrameBuffer1.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer1.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer1.depth.depthClearValue = 1.0f;

        WebglLoaderStlLitFrameBuffer litFrameBuffer;
        litFrameBuffer.color = outputColor->createView();
        litFrameBuffer.color.loadOp = LoadOp::Clear;
        litFrameBuffer.color.storeOp = StoreOp::Store;
        litFrameBuffer.color.clearValue = {0.4470588235294118f, 0.3921568627450980f, 0.3568627450980392f, 1.0f};
        litFrameBuffer.depth = sceneDepth->createView();
        litFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        litFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        litFrameBuffer.depth.depthClearValue = 1.0f;

        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->writeBuffer(BufferRange(shadowInvocationBuffer0), &invocation0, sizeof(invocation0))
            ->writeBuffer(BufferRange(shadowInvocationBuffer1), &invocation1, sizeof(invocation1))
            ->renderPass("WebglLoaderStlShadow0", shadowFrameBuffer0, shadowPass0())
            ->renderPass("WebglLoaderStlShadow1", shadowFrameBuffer1, shadowPass1())
            ->renderPass("WebglLoaderStlMainPhongFog", litFrameBuffer, litPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final RGBA8 texture used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the explicitly configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique RenderSet and every private shadow or capture resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(shadowInvocationBuffer0);
        device->freeBuffer(shadowInvocationBuffer1);
        device->freeTexture(shadowDepth0);
        device->freeTexture(shadowDepth1);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
