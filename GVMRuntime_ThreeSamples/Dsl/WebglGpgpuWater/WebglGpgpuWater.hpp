#ifndef GVM_THREE_WEBGL_GPGPU_WATER_HPP
#define GVM_THREE_WEBGL_GPGPU_WATER_HPP

#include "UGL.h"
#include "WebglGpgpuWaterData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglGpgpuWaterTextureWidth = 128u;
static const uint WebglGpgpuWaterMaxSceneTextures = 16u;
static const uint WebglGpgpuWaterEnvironmentWidth = 1024u;
static const uint WebglGpgpuWaterEnvironmentHeight = 512u;
static const uint WebglGpgpuWaterPmremCubeSize = 256u;
static const uint WebglGpgpuWaterPmremAtlasWidth = 768u;
static const uint WebglGpgpuWaterPmremAtlasHeight = 1024u;
static const uint WebglGpgpuWaterPmremLodCount = 11u;
static const uint WebglGpgpuWaterEnvironmentPrefilterSampleCount = 256u;

/** Stores one normalized triangle-list vertex in the consolidated water Scene. */
struct WebglGpgpuWaterVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uvAndReserved [[Attribute2]];
};

/** Stores one entity transform and the deterministic main camera transform. */
struct WebglGpgpuWaterObjectData
{
    float4x4 model;
    float4x4 normalMatrix;
    float4x4 viewProjection;
    float4 cameraPositionAndTime;
};

/** Stores the mandatory non-instanced translation entry for one Scene entity. */
struct WebglGpgpuWaterInstanceData
{
    float4 translation;
};

/** Stores one private material's base color and compact surface parameters. */
struct WebglGpgpuWaterMaterialData
{
    float4 baseColor;
    float4 metalnessRoughnessOpacityAndReserved;
};

/** Stores one ordered r185 PMREM incremental-filter dispatch configuration. */
struct WebglGpgpuWaterPmremUniforms
{
    uint4 outputOffsetSize;
    float4 roughnessSourceMipAndReserved;
};

/** Stores material phase, visibility, shadow, and optional wireframe flags. */
struct WebglGpgpuWaterRenderFlags
{
    uint4 values;
};

/** Defines the only RenderSet used by every geometry pass for the logical Scene. */
struct WebglGpgpuWaterSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, entity data, and fixed per-entity material textures. */
    constructor(
        BufferComponent<WebglGpgpuWaterVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGpgpuWaterObjectData> objects,
        BufferComponent<WebglGpgpuWaterInstanceData> instances,
        BufferComponent<WebglGpgpuWaterMaterialData> materials,
        BufferComponent<WebglGpgpuWaterRenderFlags> renderFlags,
        (TextureComponent<half4, WebglGpgpuWaterMaxSceneTextures> textures))
    {
    }
};

/** Binds the shared Standard material textures and their dedicated samplers. */
struct WebglGpgpuWaterMaterialBindGroup final : public IBindGroup
{
    /** Declares base color, PMREM, and exact r185 DFG lookup resources. */
    constructor(
        Sampler materialSampler [[Binding0]],
        Texture2D<half4> environmentAtlas [[Binding1]],
        Sampler environmentSampler [[Binding2]],
        Texture2D<half4> dfgLut [[Binding3]],
        Sampler dfgSampler [[Binding4]])
    {
    }
};

/** Binds directional VSM state independently from the frozen Standard resources. */
struct WebglGpgpuWaterShadowSamplingBindGroup final : public IBindGroup
{
    /** Declares light projection, filtered moments, and the linear clamp sampler. */
    constructor(
        UniformBuffer<WebglGpgpuWaterShadowUniforms> uniforms [[Binding0]],
        Texture2D<half2> distribution [[Binding1]],
        Sampler linearSampler [[Binding2]])
    {
    }
};

/** Binds the decoded HDR source and writable two-level prefilter atlas. */
struct WebglGpgpuWaterEnvironmentPrefilterBindGroup final : public IBindGroup
{
    /** Declares all resources consumed by the one-time private DSL compute. */
    constructor(
        Texture2D<float4> environmentTexture [[Binding0]],
        Sampler environmentSampler [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> environmentAtlas [[Binding2]])
    {
    }
};

/** Binds one immutable PMREM level configuration and the main-to-ping textures. */
struct WebglGpgpuWaterPmremFilterBindGroup final : public IBindGroup
{
    /** Declares the packed source atlas and separate writable ping-pong atlas. */
    constructor(
        UniformBuffer<WebglGpgpuWaterPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        Sampler atlasSampler [[Binding2]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding3]])
    {
    }
};

/** Binds the PMREM ping-pong atlas for exact target-level copies to the main atlas. */
struct WebglGpgpuWaterPmremCopyBindGroup final : public IBindGroup
{
    /** Declares the active level configuration and non-overlapping copy resources. */
    constructor(
        UniformBuffer<WebglGpgpuWaterPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding2]])
    {
    }
};

/** Binds the locked equirectangular HDR source and fixed camera screen state. */
struct WebglGpgpuWaterEnvironmentBindGroup final : public IBindGroup
{
    /** Declares the linear HDR texture, repeat-clamp sampler, and camera basis. */
    constructor(
        UniformBuffer<WebglGpgpuWaterScreenUniforms> screenUniforms [[Binding0]],
        Texture2D<half4> environmentTexture [[Binding1]],
        Sampler environmentSampler [[Binding2]])
    {
    }
};

/** Binds the linear Scene target consumed by the final ACES screen pass. */
struct WebglGpgpuWaterToneMapBindGroup final : public IBindGroup
{
    /** Declares the exposure state, HDR Scene texture, and clamp sampler. */
    constructor(
        UniformBuffer<WebglGpgpuWaterScreenUniforms> screenUniforms [[Binding0]],
        Texture2D<half4> sceneTexture [[Binding1]],
        Sampler sceneSampler [[Binding2]])
    {
    }
};

/** Binds the current full-precision height state to one fragment simulation step. */
struct WebglGpgpuWaterHeightBindGroup final : public IBindGroup
{
    /** Declares the current RGBA32Float state, exact controls, and nearest sampler. */
    constructor(
        UniformBuffer<WebglGpgpuWaterHeightUniforms> uniforms [[Binding0]],
        Texture2D<float4> currentHeight [[Binding1]],
        Sampler nearestSampler [[Binding2]])
    {
    }
};

/** Binds the fixed light basis consumed by the RenderSet shadow Scene pass. */
struct WebglGpgpuWaterShadowStateBindGroup final : public IBindGroup
{
    /** Declares the existing uniform buffer without introducing standalone geometry. */
    constructor(
        UniformBuffer<WebglGpgpuWaterShadowUniforms> uniforms [[Binding0]])
    {
    }
};

/** Binds the native depth texture consumed by the first VSM blur direction. */
struct WebglGpgpuWaterVsmVerticalBindGroup final : public IBindGroup
{
    /** Declares the current directional depth attachment as a sampled texture. */
    constructor(
        Texture2D<TextureFormat::Depth32Float> shadowDepth [[Binding0]])
    {
    }
};

/** Binds vertical VSM moments for the exact horizontal blur direction. */
struct WebglGpgpuWaterVsmHorizontalBindGroup final : public IBindGroup
{
    /** Declares half-float moments and Three's linear clamp sampling state. */
    constructor(
        Texture2D<half2> verticalMoments [[Binding0]],
        Sampler linearSampler [[Binding1]])
    {
    }
};

/** Carries one fullscreen simulation triangle into the height fragment stage. */
struct WebglGpgpuWaterHeightVertexOutput
{
    float4 position [[Position]];
};

/** Carries one fullscreen triangle and normalized coordinates through screen passes. */
struct WebglGpgpuWaterScreenVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Carries one fullscreen VSM triangle while preserving pixel coordinates. */
struct WebglGpgpuWaterVsmVertexOutput
{
    float4 position [[Position]];
};

/** Carries light-space position and entity identity into the depth-only pass. */
struct WebglGpgpuWaterShadowVertexOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
};

/** Defines the full-precision attachment used by the frozen fragment ping-pong path. */
struct WebglGpgpuWaterHeightFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
};

/** Carries entity-resolved geometry, material, and phase values to a Scene fragment. */
struct WebglGpgpuWaterSceneVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 cameraPosition [[Attribute2]];
    float2 uv [[Attribute3]];
    uint2 entityAndPhase [[Attribute4]];
};

/** Stores backend-neutral Scene vertex values before the annotated shader output is formed. */
struct WebglGpgpuWaterResolvedVertex
{
    float4 clipPosition;
    float3 worldPosition;
    float3 worldNormal;
    float3 cameraPosition;
    float2 uv;
    uint2 entityAndPhase;
};

/** Defines the display-encoded RGBA8 color and depth attachments used by Scene passes. */
struct WebglGpgpuWaterSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the display-encoded RGBA8 color attachment initialized by the background pass. */
struct WebglGpgpuWaterHdrScreenFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines the display-encoded RGBA8 attachment written by final ACES output. */
struct WebglGpgpuWaterOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines the optional depth-only directional shadow target outside the RenderSet. */
struct WebglGpgpuWaterShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one RG16F VSM moment target matching Three's half-float maps. */
struct WebglGpgpuWaterVsmFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RG16Float> moments;
};

/** Reproduces one exact Three r185 heightmap step as a fullscreen fragment pass. */
class WebglGpgpuWaterHeightPass final : public IRenderClass
{
public:
    /** Binds one immutable ping-pong direction and disables triangle culling. */
    constructor(BindGroup<WebglGpgpuWaterHeightBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by GPUComputationRenderer. */
    WebglGpgpuWaterHeightVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuWaterHeightVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Applies the four-neighbor wave equation and optional pointer disturbance. */
    WebglGpgpuWaterHeightFrameBuffer fragment(
        WebglGpgpuWaterHeightVertexOutput inputValue)
    {
        const float2 cellSize = float2(
            1.0f / float(WebglGpgpuWaterTextureWidth),
            1.0f / float(WebglGpgpuWaterTextureWidth));
        const float2 uv = inputValue.position.xy * cellSize;
        float4 heightValue = bindGroup->currentHeight->sample(
            bindGroup->nearestSampler,
            uv);
        const float north = bindGroup->currentHeight
                                ->sample(
                                    bindGroup->nearestSampler,
                                    uv + float2(0.0f, cellSize.y))
                                .x;
        const float south = bindGroup->currentHeight
                                ->sample(
                                    bindGroup->nearestSampler,
                                    uv - float2(0.0f, cellSize.y))
                                .x;
        const float east = bindGroup->currentHeight
                               ->sample(
                                   bindGroup->nearestSampler,
                                   uv + float2(cellSize.x, 0.0f))
                               .x;
        const float west = bindGroup->currentHeight
                               ->sample(
                                   bindGroup->nearestSampler,
                                   uv - float2(cellSize.x, 0.0f))
                               .x;
        const float viscosity = bindGroup->uniforms->viscosityAndReserved.x;
        float newHeight =
            ((north + south + east + west) * 0.5f - heightValue.y) *
            viscosity;
        const float2 mousePosition =
            bindGroup->uniforms->mousePositionSizeAndDepth.xy;
        const float mouseSize =
            bindGroup->uniforms->mousePositionSizeAndDepth.z;
        const float deep =
            bindGroup->uniforms->mousePositionSizeAndDepth.w;
        const float mousePhase = clamp(
            length(
                (uv - float2(0.5f)) * 6.0f -
                float2(mousePosition.x, -mousePosition.y)) *
                3.14159265358979323846f / mouseSize,
            0.0f,
            3.14159265358979323846f);
        newHeight -= (cos(mousePhase) + 1.0f) * deep;
        heightValue.y = heightValue.x;
        heightValue.x = newHeight;

        WebglGpgpuWaterHeightFrameBuffer frameBuffer;
        frameBuffer.color = heightValue;
        return frameBuffer;
    }
};

/** Converts one non-negative linear display channel to the r185 sRGB output transfer. */
float webglGpgpuWaterLinearToSrgb(float value)
{
    return value <= 0.0031308f
               ? value * 12.92f
               : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the scalar rational fit shared by Three's ACES filmic tone mapper. */
float3 webglGpgpuWaterRrtAndOdtFit(float3 value)
{
    const float3 numerator =
        value * (value + 0.0245786f) - 0.000090537f;
    const float3 denominator =
        value * (value * 0.983729f + 0.4329510f) + 0.238081f;
    return numerator / denominator;
}

/** Reproduces Three r185 ACES filmic tone mapping at the configured exposure. */
float3 webglGpgpuWaterAcesToneMap(float3 linearColor, float exposure)
{
    const float3 exposed = linearColor * (exposure / 0.6f);
    float3 acesColor = float3(
        exposed.x * 0.59719f + exposed.y * 0.35458f + exposed.z * 0.04823f,
        exposed.x * 0.07600f + exposed.y * 0.90834f + exposed.z * 0.01566f,
        exposed.x * 0.02840f + exposed.y * 0.13383f + exposed.z * 0.83777f);
    acesColor = webglGpgpuWaterRrtAndOdtFit(acesColor);
    return clamp(
        float3(
            acesColor.x * 1.60475f - acesColor.y * 0.53108f -
                acesColor.z * 0.07367f,
            -acesColor.x * 0.10208f + acesColor.y * 1.10813f -
                acesColor.z * 0.00605f,
            -acesColor.x * 0.00327f - acesColor.y * 0.07276f +
                acesColor.z * 1.07602f),
        float3(0.0f),
        float3(1.0f));
}

/** Applies Three's per-fragment ACES and sRGB encoding before framebuffer blending. */
float3 webglGpgpuWaterEncodeDisplay(float3 linearColor, float exposure)
{
    const float3 toneMapped =
        webglGpgpuWaterAcesToneMap(linearColor, exposure);
    return float3(
        webglGpgpuWaterLinearToSrgb(toneMapped.x),
        webglGpgpuWaterLinearToSrgb(toneMapped.y),
        webglGpgpuWaterLinearToSrgb(toneMapped.z));
}

/** Maps one world-space direction to the top-down equirectangular source convention. */
float2 webglGpgpuWaterEnvironmentUv(float3 direction)
{
    const float3 normalizedDirection = normalize(direction);
    return float2(
        atan2(normalizedDirection.z, normalizedDirection.x) *
                0.15915494309189535f +
            0.5f,
        0.5f - asin(clamp(normalizedDirection.y, -1.0f, 1.0f)) *
                   0.3183098861837907f);
}

/** Reverses one unsigned bit pattern into a deterministic Van der Corput sample. */
float webglGpgpuWaterRadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) |
           ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) |
           ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) |
           ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) |
           ((bits & 0xff00ff00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

/** Returns the r185 PMREM face index selected by one nonzero direction. */
uint webglGpgpuWaterPmremFace(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            return direction.x > 0.0f ? 0u : 3u;
        }
        return direction.y > 0.0f ? 1u : 4u;
    }
    if (absoluteDirection.z > absoluteDirection.y)
    {
        return direction.z > 0.0f ? 2u : 5u;
    }
    return direction.y > 0.0f ? 1u : 4u;
}

/** Maps one direction onto normalized coordinates within its selected PMREM face. */
float2 webglGpgpuWaterPmremFaceUv(float3 direction, uint face)
{
    float2 uv;
    if (face == 0u)
    {
        uv = float2(direction.z, direction.y) / abs(direction.x);
    }
    else if (face == 1u)
    {
        uv = float2(-direction.x, -direction.z) / abs(direction.y);
    }
    else if (face == 2u)
    {
        uv = float2(-direction.x, direction.y) / abs(direction.z);
    }
    else if (face == 3u)
    {
        uv = float2(-direction.z, direction.y) / abs(direction.x);
    }
    else if (face == 4u)
    {
        uv = float2(-direction.x, direction.z) / abs(direction.y);
    }
    else
    {
        uv = float2(direction.x, direction.y) / abs(direction.z);
    }
    return uv * 0.5f + 0.5f;
}

/** Maps one padded face coordinate and face index back to a PMREM direction. */
float3 webglGpgpuWaterPmremDirection(float2 uv, uint face)
{
    const float2 signedUv = uv * 2.0f - 1.0f;
    if (face == 0u)
    {
        return float3(1.0f, signedUv.y, signedUv.x);
    }
    if (face == 1u)
    {
        return float3(-signedUv.x, 1.0f, -signedUv.y);
    }
    if (face == 2u)
    {
        return float3(-signedUv.x, signedUv.y, 1.0f);
    }
    if (face == 3u)
    {
        return float3(-1.0f, signedUv.y, -signedUv.x);
    }
    if (face == 4u)
    {
        return float3(-signedUv.x, -1.0f, signedUv.y);
    }
    return float3(signedUv.x, signedUv.y, -1.0f);
}

/** Returns the packed cubeUV atlas coordinate for one direction and virtual mip. */
float2 webglGpgpuWaterPmremAtlasUv(float3 direction, float sourceMip)
{
    uint face = webglGpgpuWaterPmremFace(direction);
    const float filterIndex = max(4.0f - sourceMip, 0.0f);
    const float storedMip = max(sourceMip, 4.0f);
    const float faceSize = exp2(storedMip);
    float2 uv =
        webglGpgpuWaterPmremFaceUv(direction, face) *
            (faceSize - 2.0f) +
        1.0f;
    if (face > 2u)
    {
        uv.y += faceSize;
        face -= 3u;
    }
    uv.x += float(face) * faceSize;
    uv.x += filterIndex * 48.0f;
    uv.y += 4.0f * (256.0f - faceSize);
    return uv / float2(
                    float(WebglGpgpuWaterPmremAtlasWidth),
                    float(WebglGpgpuWaterPmremAtlasHeight));
}

/** Converts one Standard roughness into Three r185's nonlinear virtual PMREM mip. */
float webglGpgpuWaterRoughnessToMip(float roughness)
{
    if (roughness >= 0.8f)
    {
        return (1.0f - roughness) * 5.0f - 2.0f;
    }
    if (roughness >= 0.4f)
    {
        return (0.8f - roughness) * 7.5f - 1.0f;
    }
    if (roughness >= 0.305f)
    {
        return (0.4f - roughness) * 10.526315789473684f + 2.0f;
    }
    if (roughness >= 0.21f)
    {
        return (0.305f - roughness) * 10.526315789473684f + 3.0f;
    }
    return -2.0f * log2(1.16f * max(roughness, 0.000001f));
}

/** Samples the exact packed PMREM levels selected by one material roughness. */
float3 webglGpgpuWaterSampleEnvironment(
    IN BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources,
    float3 direction,
    float roughness)
{
    const float mip = clamp(
        webglGpgpuWaterRoughnessToMip(roughness),
        -2.0f,
        8.0f);
    const float lowerMip = floor(mip);
    const float interpolation = mip - lowerMip;
    const float3 lowerColor = float4(
        materialResources->environmentAtlas->sample(
            materialResources->environmentSampler,
            webglGpgpuWaterPmremAtlasUv(direction, lowerMip)))
                                  .xyz;
    if (interpolation == 0.0f)
    {
        return lowerColor;
    }
    const float3 upperColor = float4(
        materialResources->environmentAtlas->sample(
            materialResources->environmentSampler,
            webglGpgpuWaterPmremAtlasUv(direction, lowerMip + 1.0f)))
                                  .xyz;
    return lowerColor * (1.0f - interpolation) +
           upperColor * interpolation;
}

/** Samples r185's immutable split-sum DFG lookup for one roughness and view cosine. */
float2 webglGpgpuWaterSampleDfg(
    IN BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources,
    float roughness,
    float normalDotView)
{
    return float4(materialResources->dfgLut->sample(
                      materialResources->dfgSampler,
                      float2(roughness, normalDotView)))
        .xy;
}

/** Projects one world position through the fixed r185 directional shadow camera. */
float4 webglGpgpuWaterShadowClip(
    float3 worldPosition,
    float4 lightRight,
    float4 lightUp,
    float4 lightBackwardAndEnabled,
    float4 lightPositionAndBias)
{
    const float3 relative = worldPosition - lightPositionAndBias.xyz;
    const float viewX = dot(
        relative,
        float3(lightRight.x, lightRight.y, lightRight.z));
    const float viewY = dot(
        relative,
        float3(lightUp.x, lightUp.y, lightUp.z));
    const float viewZ = dot(
        relative,
        float3(
            lightBackwardAndEnabled.x,
            lightBackwardAndEnabled.y,
            lightBackwardAndEnabled.z));
    return float4(
        viewX * 0.2f,
        -viewY * 0.2f,
        (-viewZ - 0.1f) / 5.9f,
        1.0f);
}

/** Evaluates Three r185's VSM hard test, variance bound, and light-bleed reduction. */
float webglGpgpuWaterShadowVisibility(
    IN BindGroup<WebglGpgpuWaterShadowSamplingBindGroup> shadowResources,
    float3 worldPosition)
{
    if (shadowResources->uniforms->lightBackwardAndEnabled.w < 0.5f)
    {
        return 1.0f;
    }
    const float4 shadowClip = webglGpgpuWaterShadowClip(
        worldPosition,
        shadowResources->uniforms->lightRight,
        shadowResources->uniforms->lightUp,
        shadowResources->uniforms->lightBackwardAndEnabled,
        shadowResources->uniforms->lightPositionAndBias);
    const float2 shadowUv = shadowClip.xy * 0.5f + 0.5f;
    const float compareDepth =
        shadowClip.z +
        shadowResources->uniforms->lightPositionAndBias.w;
    const bool inFrustum =
        shadowUv.x >= 0.0f && shadowUv.x <= 1.0f &&
        shadowUv.y >= 0.0f && shadowUv.y <= 1.0f &&
        compareDepth <= 1.0f;
    if (!inFrustum)
    {
        return 1.0f;
    }
    const float distributionMean = float(
        shadowResources->distribution->sample(
            shadowResources->linearSampler,
            shadowUv)
            .x);
    const float distributionDeviation = float(
        shadowResources->distribution->sample(
            shadowResources->linearSampler,
            shadowUv)
            .y);
    const float hardShadow =
        compareDepth <= distributionMean ? 1.0f : 0.0f;
    if (hardShadow == 1.0f)
    {
        return 1.0f;
    }
    const float variance = max(
        distributionDeviation * distributionDeviation,
        0.0000001f);
    const float distance = compareDepth - distributionMean;
    float probability = variance / (variance + distance * distance);
    probability = clamp(
        (probability - 0.3f) / 0.65f,
        0.0f,
        1.0f);
    return max(hardShadow, probability);
}

/** Evaluates compact Standard direct and image-based lighting from existing DSL math. */
float3 webglGpgpuWaterShadeStandard(
    IN BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources,
    IN BindGroup<WebglGpgpuWaterShadowSamplingBindGroup> shadowResources,
    float3 baseColor,
    float metalness,
    float roughness,
    float3 worldPosition,
    float3 worldNormal,
    float3 cameraPosition)
{
    const float3 normal = normalize(worldNormal);
    const float3 viewDirection = normalize(cameraPosition - worldPosition);
    const float3 lightDirection = normalize(float3(-1.0f, 2.6f, 1.4f));
    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float normalDotLight =
        clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView =
        clamp(dot(normal, viewDirection), 0.0001f, 1.0f);
    const float normalDotHalf =
        clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float viewDotHalf =
        clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float boundedMetalness = clamp(metalness, 0.0f, 1.0f);
    const float3 normalDerivative = max(abs(ddx(normal)), abs(ddy(normal)));
    const float geometryRoughness = max(
        max(normalDerivative.x, normalDerivative.y),
        normalDerivative.z);
    const float boundedRoughness = clamp(
        max(roughness, 0.0525f) + geometryRoughness,
        0.0525f,
        1.0f);
    const float3 diffuseColor = baseColor * (1.0f - boundedMetalness);
    const float3 f0 =
        float3(0.04f) * (1.0f - boundedMetalness) +
        baseColor * boundedMetalness;
    const float fresnelWeight = exp2(
        (-5.55473f * viewDotHalf - 6.98316f) * viewDotHalf);
    const float3 fresnel =
        f0 + (float3(1.0f) - f0) * fresnelWeight;
    const float alpha = boundedRoughness * boundedRoughness;
    const float alphaSquared = alpha * alpha;
    const float distributionDenominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared /
        (3.14159265358979323846f * distributionDenominator *
         distributionDenominator);
    const float geometryView = normalDotLight * sqrt(
        normalDotView * normalDotView * (1.0f - alphaSquared) +
        alphaSquared);
    const float geometryLight = normalDotView * sqrt(
        normalDotLight * normalDotLight * (1.0f - alphaSquared) +
        alphaSquared);
    const float visibility =
        0.5f / max(geometryView + geometryLight, 0.0001f);
    const float2 dfgView = webglGpgpuWaterSampleDfg(
        materialResources,
        boundedRoughness,
        normalDotView);
    const float2 dfgLight = webglGpgpuWaterSampleDfg(
        materialResources,
        boundedRoughness,
        normalDotLight);
    const float3 directFssView = f0 * dfgView.x + float3(dfgView.y);
    const float3 directFssLight = f0 * dfgLight.x + float3(dfgLight.y);
    const float directEssView = dfgView.x + dfgView.y;
    const float directEssLight = dfgLight.x + dfgLight.y;
    const float directEmsView = 1.0f - directEssView;
    const float directEmsLight = 1.0f - directEssLight;
    const float3 directFavg =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 directMultipleScattering =
        directFssView * directFssLight * directFavg /
        (float3(1.0f) -
         directEmsView * directEmsLight * directFavg +
         float3(0.000001f)) *
        (directEmsView * directEmsLight);
    const float3 directColor =
        (diffuseColor * 0.3183098861837907f +
         fresnel * distribution * visibility +
         directMultipleScattering) *
        (4.0f * normalDotLight) *
        webglGpgpuWaterShadowVisibility(
            shadowResources,
            worldPosition);

    const float3 incident = -viewDirection;
    float3 reflectionDirection = normalize(
        incident - normal * (2.0f * dot(normal, incident)));
    const float normalMixWeight =
        boundedRoughness * boundedRoughness *
        boundedRoughness * boundedRoughness;
    reflectionDirection = normalize(
        reflectionDirection * (1.0f - normalMixWeight) +
        normal * normalMixWeight);
    const float3 specularEnvironment = webglGpgpuWaterSampleEnvironment(
        materialResources,
        reflectionDirection,
        boundedRoughness) * 1.25f;
    const float3 diffuseEnvironment = webglGpgpuWaterSampleEnvironment(
        materialResources,
        normal,
        1.0f) * 1.25f;
    const float3 dielectricF0 = float3(0.04f);
    const float3 dielectricFss =
        dielectricF0 * dfgView.x + float3(dfgView.y);
    const float environmentEss = dfgView.x + dfgView.y;
    const float environmentEms = 1.0f - environmentEss;
    const float3 dielectricFavg =
        dielectricF0 +
        (float3(1.0f) - dielectricF0) * 0.047619f;
    const float3 dielectricFms =
        dielectricFss * dielectricFavg /
        (float3(1.0f) - environmentEms * dielectricFavg);
    const float3 dielectricMultipleScattering =
        dielectricFms * environmentEms;
    const float3 metallicFss =
        baseColor * dfgView.x + float3(dfgView.y);
    const float3 metallicFavg =
        baseColor + (float3(1.0f) - baseColor) * 0.047619f;
    const float3 metallicFms =
        metallicFss * metallicFavg /
        (float3(1.0f) - environmentEms * metallicFavg);
    const float3 metallicMultipleScattering =
        metallicFms * environmentEms;
    const float3 singleScattering =
        dielectricFss * (1.0f - boundedMetalness) +
        metallicFss * boundedMetalness;
    const float3 multipleScattering =
        dielectricMultipleScattering * (1.0f - boundedMetalness) +
        metallicMultipleScattering * boundedMetalness;
    const float3 dielectricTotalScattering =
        dielectricFss + dielectricMultipleScattering;
    const float3 environmentColor =
        specularEnvironment * singleScattering +
        multipleScattering * diffuseEnvironment +
        diffuseColor *
            (float3(1.0f) - dielectricTotalScattering) *
            diffuseEnvironment;
    return directColor + environmentColor;
}

/** Samples one virtual mip from the main PMREM atlas during incremental filtering. */
float3 webglGpgpuWaterSamplePmremFilterSource(
    IN BindGroup<WebglGpgpuWaterPmremFilterBindGroup> filterResources,
    float3 direction,
    float sourceMip)
{
    return float4(filterResources->sourceAtlas->sampleLevel(
                      filterResources->atlasSampler,
                      webglGpgpuWaterPmremAtlasUv(direction, sourceMip),
                      0.0f))
        .xyz;
}

/** Converts the equirectangular HDR source into Three's padded level-zero cubeUV layout. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglGpgpuWaterEnvironmentPrefilterPass final : public IComputeClass
{
public:
    /** Binds the locked HDR source, wrap-aware sampler, and writable atlas. */
    constructor(
        BindGroup<WebglGpgpuWaterEnvironmentPrefilterBindGroup> prefilterResources [[Slot0]])
    {
    }

private:
    /** Writes one padded level-zero cube-face texel from its world direction. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebglGpgpuWaterPmremAtlasWidth ||
            threadID.y >= WebglGpgpuWaterPmremCubeSize * 2u)
        {
            return;
        }
        const uint faceColumn =
            threadID.x / WebglGpgpuWaterPmremCubeSize;
        const uint faceRow =
            threadID.y / WebglGpgpuWaterPmremCubeSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x - faceColumn * WebglGpgpuWaterPmremCubeSize,
            threadID.y - faceRow * WebglGpgpuWaterPmremCubeSize);
        const float2 faceUv =
            (float2(localCoordinate) - 0.5f) /
            float(WebglGpgpuWaterPmremCubeSize - 2u);
        const float3 direction = normalize(
            webglGpgpuWaterPmremDirection(faceUv, face));
        const float3 color =
            prefilterResources->environmentTexture->sampleLevel(
                prefilterResources->environmentSampler,
                webglGpgpuWaterEnvironmentUv(direction),
                0.0f)
                .xyz;
        prefilterResources->environmentAtlas->write(
            threadID.xy,
            half4(float4(color, 1.0f)));
    }
};

/** Applies one ordered r185 incremental GGX VNDF filter from main atlas to ping-pong. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglGpgpuWaterPmremFilterPass final : public IComputeClass
{
public:
    /** Binds one level configuration and distinct sampled and writable atlases. */
    constructor(
        BindGroup<WebglGpgpuWaterPmremFilterBindGroup> filterResources [[Slot0]])
    {
    }

private:
    /** Evaluates exactly 256 r185 Hammersley VNDF samples for one padded texel. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint4 output = filterResources->uniforms->outputOffsetSize;
        if (threadID.x >= output.z || threadID.y >= output.w)
        {
            return;
        }
        const uint faceSize = output.z / 3u;
        const uint faceColumn = threadID.x / faceSize;
        const uint faceRow = threadID.y / faceSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x - faceColumn * faceSize,
            threadID.y - faceRow * faceSize);
        const float2 faceUv =
            (float2(localCoordinate) - 0.5f) /
            float(faceSize - 2u);
        const float3 normal = normalize(
            webglGpgpuWaterPmremDirection(faceUv, face));
        const float targetRoughness =
            filterResources->uniforms->roughnessSourceMipAndReserved.x;
        const float sourceRoughness = targetRoughness - 0.1f;
        const float incrementalRoughness = sqrt(
            targetRoughness * targetRoughness -
            sourceRoughness * sourceRoughness);
        const float roughness =
            incrementalRoughness * targetRoughness * 1.25f;
        const float sourceMip =
            filterResources->uniforms->roughnessSourceMipAndReserved.y;
        float3 filteredColor = float3(0.0f);
        float totalWeight = 0.0f;
        if (roughness < 0.001f)
        {
            filteredColor = webglGpgpuWaterSamplePmremFilterSource(
                filterResources,
                normal,
                sourceMip);
            totalWeight = 1.0f;
        }
        else
        {
            const float3 referenceAxis =
                abs(normal.z) < 0.999f
                    ? float3(0.0f, 0.0f, 1.0f)
                    : float3(1.0f, 0.0f, 0.0f);
            const float3 tangent = normalize(cross(referenceAxis, normal));
            const float3 bitangent = cross(normal, tangent);
            const float alpha = roughness * roughness;
            for (uint sampleIndex = 0u;
                 sampleIndex < WebglGpgpuWaterEnvironmentPrefilterSampleCount;
                 ++sampleIndex)
            {
                const float2 xi = float2(
                    float(sampleIndex) /
                        float(WebglGpgpuWaterEnvironmentPrefilterSampleCount),
                    webglGpgpuWaterRadicalInverse(sampleIndex));
                const float diskRadius = sqrt(xi.x);
                const float azimuth =
                    6.28318530717958647692f * xi.y;
                const float tangentX = diskRadius * cos(azimuth);
                const float tangentY = diskRadius * sin(azimuth);
                const float tangentZ = sqrt(max(
                    0.0f,
                    1.0f - tangentX * tangentX - tangentY * tangentY));
                const float3 halfVector = normalize(float3(
                    alpha * tangentX,
                    alpha * tangentY,
                    tangentZ));
                const float3 sampledTangentDirection = normalize(float3(
                    2.0f * halfVector.z * halfVector.x,
                    2.0f * halfVector.z * halfVector.y,
                    2.0f * halfVector.z * halfVector.z - 1.0f));
                const float sampleWeight =
                    max(sampledTangentDirection.z, 0.0f);
                if (sampleWeight > 0.0f)
                {
                    const float3 sampledDirection = normalize(
                        tangent * sampledTangentDirection.x +
                        bitangent * sampledTangentDirection.y +
                        normal * sampledTangentDirection.z);
                    filteredColor +=
                        webglGpgpuWaterSamplePmremFilterSource(
                            filterResources,
                            sampledDirection,
                            sourceMip) *
                        sampleWeight;
                    totalWeight += sampleWeight;
                }
            }
        }
        const uint2 outputCoordinate =
            uint2(output.x, output.y) + threadID.xy;
        filterResources->destinationAtlas->write(
            outputCoordinate,
            half4(float4(
                filteredColor / max(totalWeight, 0.0001f),
                1.0f)));
    }
};

/** Copies one completed ping-pong PMREM target level back into the packed main atlas. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglGpgpuWaterPmremCopyPass final : public IComputeClass
{
public:
    /** Binds one target-level rectangle and its non-overlapping source and destination. */
    constructor(
        BindGroup<WebglGpgpuWaterPmremCopyBindGroup> copyResources [[Slot0]])
    {
    }

private:
    /** Copies one exact half-float texel without sampling or format conversion. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint4 output = copyResources->uniforms->outputOffsetSize;
        if (threadID.x >= output.z || threadID.y >= output.w)
        {
            return;
        }
        const uint2 coordinate =
            uint2(output.x, output.y) + threadID.xy;
        copyResources->destinationAtlas->write(
            coordinate,
            copyResources->sourceAtlas->read(coordinate));
    }
};

/** Samples the PMREM atlas behind Scene geometry at the configured background blur. */
float3 webglGpgpuWaterSampleBackgroundEnvironment(
    IN BindGroup<WebglGpgpuWaterEnvironmentBindGroup> environmentResources,
    float3 direction,
    float roughness)
{
    const float mip = clamp(
        webglGpgpuWaterRoughnessToMip(roughness),
        -2.0f,
        8.0f);
    const float lowerMip = floor(mip);
    const float interpolation = mip - lowerMip;
    const float3 lowerColor = float4(
        environmentResources->environmentTexture->sample(
            environmentResources->environmentSampler,
            webglGpgpuWaterPmremAtlasUv(direction, lowerMip)))
                                  .xyz;
    if (interpolation == 0.0f)
    {
        return lowerColor;
    }
    const float3 upperColor = float4(
        environmentResources->environmentTexture->sample(
            environmentResources->environmentSampler,
            webglGpgpuWaterPmremAtlasUv(direction, lowerMip + 1.0f)))
                                  .xyz;
    return lowerColor * (1.0f - interpolation) +
           upperColor * interpolation;
}

/** Draws the locked linear equirectangular source behind all Scene geometry. */
class WebglGpgpuWaterEnvironmentPass final : public IRenderClass
{
public:
    /** Binds only screen resources and disables culling for one fullscreen triangle. */
    constructor(
        BindGroup<WebglGpgpuWaterEnvironmentBindGroup> environmentResources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Generates one fullscreen triangle with normalized screen coordinates. */
    WebglGpgpuWaterScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuWaterScreenVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.texCoord = texCoord;
        return outputValue;
    }

    /** Reconstructs the fixed camera ray and samples the top-down RGBE decode. */
    WebglGpgpuWaterHdrScreenFrameBuffer fragment(
        WebglGpgpuWaterScreenVertexOutput inputValue)
    {
        const float4 screen = environmentResources->screenUniforms
                                  ->outputSizeTanHalfFovAndExposure;
        const float aspect =
            screen.x / screen.y;
        const float tanHalfFov = screen.z;
        const float2 ndc = float2(
            inputValue.texCoord.x * 2.0f - 1.0f,
            1.0f - inputValue.texCoord.y * 2.0f);
        const float3 direction = normalize(
            environmentResources->screenUniforms->cameraRight.xyz *
                (ndc.x * aspect * tanHalfFov) +
            environmentResources->screenUniforms->cameraUp.xyz *
                (ndc.y * tanHalfFov) +
            environmentResources->screenUniforms->cameraForward.xyz);
        const float3 environmentColor =
            webglGpgpuWaterSampleBackgroundEnvironment(
                environmentResources,
                direction,
                0.3f);
        WebglGpgpuWaterHdrScreenFrameBuffer frameBuffer;
        frameBuffer.color = half4(float4(
            webglGpgpuWaterEncodeDisplay(environmentColor, screen.w),
            1.0f));
        return frameBuffer;
    }
};

/** Converts the complete linear Scene target to r185 ACES and sRGB output. */
class WebglGpgpuWaterToneMapPass final : public IRenderClass
{
public:
    /** Binds only the completed linear Scene target and its exposure state. */
    constructor(
        BindGroup<WebglGpgpuWaterToneMapBindGroup> toneMapResources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Generates one fullscreen triangle with normalized source coordinates. */
    WebglGpgpuWaterScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuWaterScreenVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.texCoord = texCoord;
        return outputValue;
    }

    /** Copies the display-encoded Scene result into the readback attachment. */
    WebglGpgpuWaterOutputFrameBuffer fragment(
        WebglGpgpuWaterScreenVertexOutput inputValue)
    {
        const float4 sceneColor = float4(
            toneMapResources->sceneTexture->sample(
                toneMapResources->sceneSampler,
                inputValue.texCoord));
        WebglGpgpuWaterOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(sceneColor);
        return frameBuffer;
    }
};

/** Resolves backend-neutral Scene vertex data from the unique RenderSet components. */
WebglGpgpuWaterResolvedVertex webglGpgpuWaterResolveSceneVertex(
    IN RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet,
    WebglGpgpuWaterVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglGpgpuWaterObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGpgpuWaterInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebglGpgpuWaterRenderFlags flags =
        sceneSet->renderFlags->get(renderEntityID, 0u);
    const float4 localPosition =
        inputValue.position + float4(instanceData.translation.xyz, 0.0f);
    const float4 worldPosition = mul(objectData.model, localPosition);

    WebglGpgpuWaterResolvedVertex outputValue;
    outputValue.clipPosition = mul(objectData.viewProjection, worldPosition);
    outputValue.worldPosition = worldPosition.xyz;
    const float4 transformedNormal =
        mul(objectData.normalMatrix, inputValue.normal);
    outputValue.worldNormal = normalize(float3(
        transformedNormal.x,
        transformedNormal.y,
        transformedNormal.z));
    outputValue.cameraPosition = objectData.cameraPositionAndTime.xyz;
    outputValue.uv = inputValue.uvAndReserved.xy;
    outputValue.entityAndPhase = uint2(renderEntityID, flags.values.x);
    return outputValue;
}

/** Shades the twelve ducks and pool border through the unique Scene RenderSet. */
class WebglGpgpuWaterOpaquePass final : public IRenderClass
{
public:
    /** Configures the ordinary opaque phase with Three's default depth behavior. */
    constructor(
        RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources [[Slot1]],
        BindGroup<WebglGpgpuWaterShadowSamplingBindGroup> shadowResources [[Slot2]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves object and instance components for every indirect entity command. */
    WebglGpgpuWaterSceneVertexOutput vertex(
        WebglGpgpuWaterVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGpgpuWaterResolvedVertex resolved =
            webglGpgpuWaterResolveSceneVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
        WebglGpgpuWaterSceneVertexOutput outputValue;
        outputValue.position = resolved.clipPosition;
        outputValue.worldPosition = resolved.worldPosition;
        outputValue.worldNormal = resolved.worldNormal;
        outputValue.cameraPosition = resolved.cameraPosition;
        outputValue.uv = resolved.uv;
        outputValue.entityAndPhase = resolved.entityAndPhase;
        return outputValue;
    }

    /** Rejects the water phase and emits a compact deterministic lit approximation. */
    WebglGpgpuWaterSceneFrameBuffer fragment(
        WebglGpgpuWaterSceneVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 1u)
        {
            discard_fragment();
        }
        const WebglGpgpuWaterMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const WebglGpgpuWaterRenderFlags flags =
            sceneSet->renderFlags->get(inputValue.entityAndPhase.x, 0u);
        float4 baseColor = material.baseColor;
        if (flags.values.w != 0u)
        {
            auto baseColorTexture =
                sceneSet->textures->get(inputValue.entityAndPhase.x, 0u);
            baseColor *= float4(baseColorTexture->sample(
                materialResources->materialSampler,
                float2(inputValue.uv.x, 1.0f - inputValue.uv.y)));
        }
        const float3 linearColor = webglGpgpuWaterShadeStandard(
            materialResources,
            shadowResources,
            baseColor.xyz,
            material.metalnessRoughnessOpacityAndReserved.x,
            material.metalnessRoughnessOpacityAndReserved.y,
            inputValue.worldPosition,
            inputValue.worldNormal,
            inputValue.cameraPosition);
        WebglGpgpuWaterSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(webglGpgpuWaterEncodeDisplay(linearColor, 0.5f)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent water back faces before its front faces. */
class WebglGpgpuWaterBackPass final : public IRenderClass
{
public:
    /** Culls front faces and enables the standard source-alpha blend state. */
    constructor(
        RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources [[Slot1]],
        BindGroup<WebglGpgpuWaterShadowSamplingBindGroup> shadowResources [[Slot2]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState;
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Resolves the water entity through the same indirect metadata as all Scene passes. */
    WebglGpgpuWaterSceneVertexOutput vertex(
        WebglGpgpuWaterVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGpgpuWaterResolvedVertex resolved =
            webglGpgpuWaterResolveSceneVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
        WebglGpgpuWaterSceneVertexOutput outputValue;
        outputValue.position = resolved.clipPosition;
        outputValue.worldPosition = resolved.worldPosition;
        outputValue.worldNormal = resolved.worldNormal;
        outputValue.cameraPosition = resolved.cameraPosition;
        outputValue.uv = resolved.uv;
        outputValue.entityAndPhase = resolved.entityAndPhase;
        return outputValue;
    }

    /** Rejects opaque entities and emits the frozen translucent water base color. */
    WebglGpgpuWaterSceneFrameBuffer fragment(
        WebglGpgpuWaterSceneVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 0u)
        {
            discard_fragment();
        }
        const WebglGpgpuWaterMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const float3 linearColor = webglGpgpuWaterShadeStandard(
            materialResources,
            shadowResources,
            material.baseColor.xyz,
            material.metalnessRoughnessOpacityAndReserved.x,
            material.metalnessRoughnessOpacityAndReserved.y,
            inputValue.worldPosition,
            inputValue.worldNormal,
            inputValue.cameraPosition);
        WebglGpgpuWaterSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(webglGpgpuWaterEncodeDisplay(linearColor, 0.5f)),
            half(material.metalnessRoughnessOpacityAndReserved.z));
        return frameBuffer;
    }
};

/** Draws the transparent water front faces after its back faces. */
class WebglGpgpuWaterFrontPass final : public IRenderClass
{
public:
    /** Culls back faces and uses the same source-alpha state as the back pass. */
    constructor(
        RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources [[Slot1]],
        BindGroup<WebglGpgpuWaterShadowSamplingBindGroup> shadowResources [[Slot2]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState;
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Resolves the water entity through the Scene's only RenderSet. */
    WebglGpgpuWaterSceneVertexOutput vertex(
        WebglGpgpuWaterVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGpgpuWaterResolvedVertex resolved =
            webglGpgpuWaterResolveSceneVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
        WebglGpgpuWaterSceneVertexOutput outputValue;
        outputValue.position = resolved.clipPosition;
        outputValue.worldPosition = resolved.worldPosition;
        outputValue.worldNormal = resolved.worldNormal;
        outputValue.cameraPosition = resolved.cameraPosition;
        outputValue.uv = resolved.uv;
        outputValue.entityAndPhase = resolved.entityAndPhase;
        return outputValue;
    }

    /** Applies the same water phase filter and material as the ordered back pass. */
    WebglGpgpuWaterSceneFrameBuffer fragment(
        WebglGpgpuWaterSceneVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 0u)
        {
            discard_fragment();
        }
        const WebglGpgpuWaterMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const float3 linearColor = webglGpgpuWaterShadeStandard(
            materialResources,
            shadowResources,
            material.baseColor.xyz,
            material.metalnessRoughnessOpacityAndReserved.x,
            material.metalnessRoughnessOpacityAndReserved.y,
            inputValue.worldPosition,
            inputValue.worldNormal,
            inputValue.cameraPosition);
        WebglGpgpuWaterSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(webglGpgpuWaterEncodeDisplay(linearColor, 0.5f)),
            half(material.metalnessRoughnessOpacityAndReserved.z));
        return frameBuffer;
    }
};

/** Submits the optional water and border wireframe phase through the same Scene Set. */
class WebglGpgpuWaterWireframePass final : public IRenderClass
{
public:
    /** Keeps the disabled phase deterministic while allowing both triangle orientations. */
    constructor(RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves one ordinary entity through the unique Scene RenderSet metadata. */
    WebglGpgpuWaterSceneVertexOutput vertex(
        WebglGpgpuWaterVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGpgpuWaterResolvedVertex resolved =
            webglGpgpuWaterResolveSceneVertex(
                sceneSet,
                inputValue,
                renderEntityID,
                renderEntityInstanceID);
        WebglGpgpuWaterSceneVertexOutput outputValue;
        outputValue.position = resolved.clipPosition;
        outputValue.worldPosition = resolved.worldPosition;
        outputValue.worldNormal = resolved.worldNormal;
        outputValue.cameraPosition = resolved.cameraPosition;
        outputValue.uv = resolved.uv;
        outputValue.entityAndPhase = resolved.entityAndPhase;
        return outputValue;
    }

    /** Discards every entity unless its existing material component enables wireframe. */
    WebglGpgpuWaterSceneFrameBuffer fragment(
        WebglGpgpuWaterSceneVertexOutput inputValue)
    {
        const WebglGpgpuWaterMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        if (material.metalnessRoughnessOpacityAndReserved.w < 0.5f)
        {
            discard_fragment();
        }
        WebglGpgpuWaterSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(material.baseColor);
        return frameBuffer;
    }
};

/** Draws the optional directional shadow invocation through the same Scene RenderSet. */
class WebglGpgpuWaterShadowPass final : public IRenderClass
{
public:
    /** Configures one depth-only Set pass and preserves DoubleSide water casting. */
    constructor(
        RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGpgpuWaterShadowStateBindGroup> shadowState [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity metadata and emits the exact orthographic light projection. */
    WebglGpgpuWaterShadowVertexOutput vertex(
        WebglGpgpuWaterVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGpgpuWaterResolvedVertex resolved =
            webglGpgpuWaterResolveSceneVertex(
                sceneSet,
                inputValue,
                renderEntityID,
                renderEntityInstanceID);
        float3 shadowWorldPosition = resolved.worldPosition;
        if (resolved.entityAndPhase.y == 0u)
        {
            shadowWorldPosition.y = 0.0f;
        }
        WebglGpgpuWaterShadowVertexOutput outputValue;
        outputValue.position = webglGpgpuWaterShadowClip(
            shadowWorldPosition,
            shadowState->uniforms->lightRight,
            shadowState->uniforms->lightUp,
            shadowState->uniforms->lightBackwardAndEnabled,
            shadowState->uniforms->lightPositionAndBias);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Rejects entities whose existing component disables shadow casting. */
    WebglGpgpuWaterShadowFrameBuffer fragment(
        WebglGpgpuWaterShadowVertexOutput inputValue)
    {
        const WebglGpgpuWaterRenderFlags flags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        if (flags.values.z == 0u)
        {
            discard_fragment();
        }
        WebglGpgpuWaterShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Converts native directional depth into vertically blurred VSM moments. */
class WebglGpgpuWaterVsmVerticalPass final : public IRenderClass
{
public:
    /** Binds the sampled depth texture and disables fullscreen triangle culling. */
    constructor(
        BindGroup<WebglGpgpuWaterVsmVerticalBindGroup> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by Three's first VSM direction. */
    WebglGpgpuWaterVsmVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuWaterVsmVertexOutput outputValue;
        outputValue.position =
            float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Applies the exact eight nearest depth samples across radius two. */
    WebglGpgpuWaterVsmFrameBuffer fragment(
        WebglGpgpuWaterVsmVertexOutput inputValue)
    {
        float mean = 0.0f;
        float squaredMean = 0.0f;
        for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
        {
            const float offset =
                -1.0f + float(sampleIndex) * (2.0f / 7.0f);
            const float2 samplePosition =
                inputValue.position.xy + float2(0.0f, offset * 2.0f);
            const uint2 texel = uint2(
                uint(clamp(floor(samplePosition.x), 0.0f, 2047.0f)),
                uint(clamp(floor(samplePosition.y), 0.0f, 2047.0f)));
            const float depth = resources->shadowDepth->read(texel).x;
            mean += depth;
            squaredMean += depth * depth;
        }
        mean *= 0.125f;
        squaredMean *= 0.125f;
        const float standardDeviation = sqrt(max(
            0.0f,
            squaredMean - mean * mean));
        WebglGpgpuWaterVsmFrameBuffer frameBuffer;
        frameBuffer.moments = half2(mean, standardDeviation);
        return frameBuffer;
    }
};

/** Applies Three r185's horizontal linear VSM moment blur. */
class WebglGpgpuWaterVsmHorizontalPass final : public IRenderClass
{
public:
    /** Binds vertical moments with the current linear clamp sampler. */
    constructor(
        BindGroup<WebglGpgpuWaterVsmHorizontalBindGroup> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by Three's second VSM direction. */
    WebglGpgpuWaterVsmVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuWaterVsmVertexOutput outputValue;
        outputValue.position =
            float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Reconstructs squared moments and emits the final half-float distribution. */
    WebglGpgpuWaterVsmFrameBuffer fragment(
        WebglGpgpuWaterVsmVertexOutput inputValue)
    {
        float mean = 0.0f;
        float squaredMean = 0.0f;
        for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
        {
            const float offset =
                -1.0f + float(sampleIndex) * (2.0f / 7.0f);
            const float2 sampleUv =
                (inputValue.position.xy + float2(offset * 2.0f, 0.0f)) /
                2048.0f;
            const float distributionMean = float(
                resources->verticalMoments->sample(
                    resources->linearSampler,
                    sampleUv)
                    .x);
            const float distributionDeviation = float(
                resources->verticalMoments->sample(
                    resources->linearSampler,
                    sampleUv)
                    .y);
            mean += distributionMean;
            squaredMean +=
                distributionDeviation * distributionDeviation +
                distributionMean * distributionMean;
        }
        mean *= 0.125f;
        squaredMean *= 0.125f;
        const float standardDeviation = sqrt(max(
            0.0f,
            squaredMean - mean * mean));
        WebglGpgpuWaterVsmFrameBuffer frameBuffer;
        frameBuffer.moments = half2(mean, standardDeviation);
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet, private height ping-pong, and ordered Scene passes. */
class WebglGpgpuWaterRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGpgpuWaterSceneRenderSet> sceneSet;
    Buffer<WebglGpgpuWaterHeightUniforms, BufferUsage<Uniform, CopyDst>>
        heightUniformBuffer;
    Sampler heightSampler;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        heightTexture0;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        heightTexture1;
    BindGroup<WebglGpgpuWaterHeightBindGroup> heightBindGroup0To1;
    BindGroup<WebglGpgpuWaterHeightBindGroup> heightBindGroup1To0;
    RenderClass<WebglGpgpuWaterHeightPass> heightPass0To1;
    RenderClass<WebglGpgpuWaterHeightPass> heightPass1To0;
    Sampler materialSampler;
    Sampler dfgSampler;
    Texture<TextureFormat::RG16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        dfgLutTexture;
    BindGroup<WebglGpgpuWaterMaterialBindGroup> materialResources;
    RenderClass<WebglGpgpuWaterOpaquePass> opaquePass;
    RenderClass<WebglGpgpuWaterBackPass> waterBackPass;
    RenderClass<WebglGpgpuWaterFrontPass> waterFrontPass;
    RenderClass<WebglGpgpuWaterWireframePass> wireframePass;
    Buffer<WebglGpgpuWaterShadowUniforms, BufferUsage<Uniform, CopyDst>>
        shadowUniformBuffer;
    BindGroup<WebglGpgpuWaterShadowSamplingBindGroup>
        shadowSamplingResources;
    BindGroup<WebglGpgpuWaterShadowStateBindGroup> shadowStateResources;
    RenderClass<WebglGpgpuWaterShadowPass> shadowPass;
    BindGroup<WebglGpgpuWaterVsmVerticalBindGroup> vsmVerticalResources;
    BindGroup<WebglGpgpuWaterVsmHorizontalBindGroup> vsmHorizontalResources;
    RenderClass<WebglGpgpuWaterVsmVerticalPass> vsmVerticalPass;
    RenderClass<WebglGpgpuWaterVsmHorizontalPass> vsmHorizontalPass;
    Sampler shadowSampler;
    Buffer<WebglGpgpuWaterScreenUniforms, BufferUsage<Uniform, CopyDst>>
        screenUniformBuffer;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        environmentTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D>
        environmentAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D>
        environmentPingPongAtlas;
    Sampler environmentSampler;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer0;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer1;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer2;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer3;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer4;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer5;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer6;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer7;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer8;
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
        pmremUniformBuffer9;
    BindGroup<WebglGpgpuWaterEnvironmentPrefilterBindGroup>
        environmentPrefilterResources;
    ComputeClass<WebglGpgpuWaterEnvironmentPrefilterPass>
        environmentPrefilterPass;
    BindGroup<WebglGpgpuWaterEnvironmentBindGroup> environmentResources;
    RenderClass<WebglGpgpuWaterEnvironmentPass> environmentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneColorTexture;
    Sampler sceneSampler;
    BindGroup<WebglGpgpuWaterToneMapBindGroup> toneMapResources;
    RenderClass<WebglGpgpuWaterToneMapPass> toneMapPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        shadowDepthTexture;
    Texture<TextureFormat::RG16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        shadowVerticalMomentsTexture;
    Texture<TextureFormat::RG16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        shadowDistributionTexture;
    uint currentHeightIndex = 0u;
    uint heightTickCount = 0u;
    uint shadowEnabled = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

    /** Returns the immutable uniform allocation dedicated to one filtered PMREM level. */
    Buffer<WebglGpgpuWaterPmremUniforms, BufferUsage<Uniform, CopyDst>>
    selectPmremUniformBuffer(uint lodIndex)
    {
        if (lodIndex == 1u)
        {
            return pmremUniformBuffer0;
        }
        if (lodIndex == 2u)
        {
            return pmremUniformBuffer1;
        }
        if (lodIndex == 3u)
        {
            return pmremUniformBuffer2;
        }
        if (lodIndex == 4u)
        {
            return pmremUniformBuffer3;
        }
        if (lodIndex == 5u)
        {
            return pmremUniformBuffer4;
        }
        if (lodIndex == 6u)
        {
            return pmremUniformBuffer5;
        }
        if (lodIndex == 7u)
        {
            return pmremUniformBuffer6;
        }
        if (lodIndex == 8u)
        {
            return pmremUniformBuffer7;
        }
        if (lodIndex == 9u)
        {
            return pmremUniformBuffer8;
        }
        return pmremUniformBuffer9;
    }

public:
    /** Creates only resources expressible by the frozen public DSL and RenderSet surface. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglGpgpuWaterSceneRenderSet>();
        screenUniformBuffer = device->createBuffer(
            "WebglGpgpuWaterScreenUniforms",
            1u);
        environmentSampler = device->createSampler({
            .label = "WebglGpgpuWaterEnvironmentSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        sceneSampler = device->createSampler({
            .label = "WebglGpgpuWaterSceneSampler",
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
        heightUniformBuffer = device->createBuffer(
            "WebglGpgpuWaterHeightUniforms",
            1u);
        heightSampler = device->createSampler({
            .label = "WebglGpgpuWaterNearestSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        heightTexture0 = device->createTexture(
            "WebglGpgpuWaterHeight0RGBA32Float",
            WebglGpgpuWaterTextureWidth,
            WebglGpgpuWaterTextureWidth,
            1u);
        heightTexture1 = device->createTexture(
            "WebglGpgpuWaterHeight1RGBA32Float",
            WebglGpgpuWaterTextureWidth,
            WebglGpgpuWaterTextureWidth,
            1u);
        heightBindGroup0To1 =
            device->createBindGroup<WebglGpgpuWaterHeightBindGroup>(
                heightUniformBuffer,
                heightTexture0->createView(),
                heightSampler);
        heightBindGroup1To0 =
            device->createBindGroup<WebglGpgpuWaterHeightBindGroup>(
                heightUniformBuffer,
                heightTexture1->createView(),
                heightSampler);
        heightPass0To1 = device->createRenderClass<WebglGpgpuWaterHeightPass>(
            heightBindGroup0To1);
        heightPass1To0 = device->createRenderClass<WebglGpgpuWaterHeightPass>(
            heightBindGroup1To0);
        materialSampler = device->createSampler({
            .label = "WebglGpgpuWaterMaterialSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 9.0f,
            .maxAnisotropy = 1u,
        });
        dfgSampler = device->createSampler({
            .label = "WebglGpgpuWaterDfgSampler",
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
        wireframePass =
            device->createRenderClass<WebglGpgpuWaterWireframePass>(
                sceneSet);
        shadowUniformBuffer = device->createBuffer(
            "WebglGpgpuWaterShadowUniforms",
            1u);
        shadowSampler = device->createSampler({
            .label = "WebglGpgpuWaterShadowLinearSampler",
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
        shadowDepthTexture = device->createTexture(
            "WebglGpgpuWaterShadowDepth",
            2048u,
            2048u,
            1u);
        shadowVerticalMomentsTexture = device->createTexture(
            "WebglGpgpuWaterShadowVerticalMomentsRG16Float",
            2048u,
            2048u,
            1u);
        shadowDistributionTexture = device->createTexture(
            "WebglGpgpuWaterShadowDistributionRG16Float",
            2048u,
            2048u,
            1u);
        shadowStateResources =
            device->createBindGroup<WebglGpgpuWaterShadowStateBindGroup>(
                shadowUniformBuffer);
        shadowSamplingResources = device->createBindGroup<
            WebglGpgpuWaterShadowSamplingBindGroup>(
                shadowUniformBuffer,
                shadowDistributionTexture->createView(),
                shadowSampler);
        shadowPass = device->createRenderClass<WebglGpgpuWaterShadowPass>(
            sceneSet,
            shadowStateResources);
        vsmVerticalResources =
            device->createBindGroup<WebglGpgpuWaterVsmVerticalBindGroup>(
                shadowDepthTexture->createView());
        vsmHorizontalResources =
            device->createBindGroup<WebglGpgpuWaterVsmHorizontalBindGroup>(
                shadowVerticalMomentsTexture->createView(),
                shadowSampler);
        vsmVerticalPass =
            device->createRenderClass<WebglGpgpuWaterVsmVerticalPass>(
                vsmVerticalResources);
        vsmHorizontalPass =
            device->createRenderClass<WebglGpgpuWaterVsmHorizontalPass>(
                vsmHorizontalResources);
    }

    /** Uploads the immutable 16x16 r185 RG16F split-sum DFG lookup. */
    void configureDfgLut(const eastl::vector<uint> &packedHalfPixels)
    {
        dfgLutTexture = device->createTexture(
            "WebglGpgpuWaterDfgLutRG16Float",
            16u,
            16u,
            1u);
        graphicsQueue
            ->writeTexture(
                dfgLutTexture,
                packedHalfPixels.data(),
                uint64_t(packedHalfPixels.size()) * sizeof(uint))
            ->submit();
    }

    /** Allocates the final fixed-size RGBA8 color and default Scene depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneColorTexture = device->createTexture(
            "WebglGpgpuWaterSceneRGBA8",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebglGpgpuWaterOutputRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglGpgpuWaterDepth32",
            width,
            height,
            1u);
        WebglGpgpuWaterScreenUniforms screenUniforms;
        screenUniforms.outputSizeTanHalfFovAndExposure =
            float4(float(width), float(height), 0.7673269879789604f, 0.5f);
        screenUniforms.cameraRight = float4(1.0f, 0.0f, 0.0f, 0.0f);
        screenUniforms.cameraUp =
            float4(0.0f, 0.894427191f, -0.4472135955f, 0.0f);
        screenUniforms.cameraForward =
            float4(0.0f, -0.4472135955f, -0.894427191f, 0.0f);
        graphicsQueue->writeBuffer(
            BufferRange(screenUniformBuffer),
            &screenUniforms,
            sizeof(screenUniforms));
        toneMapResources =
            device->createBindGroup<WebglGpgpuWaterToneMapBindGroup>(
                screenUniformBuffer,
                sceneColorTexture->createView(),
                sceneSampler);
        toneMapPass = device->createRenderClass<WebglGpgpuWaterToneMapPass>(
            toneMapResources);
        graphicsQueue->submit();
    }

    /** Uploads the locked RGBE source and builds the ordered r185 packed PMREM atlas. */
    void configureEnvironment(
        const eastl::vector<float4> &pixels,
        uint width,
        uint height)
    {
        environmentTexture = device->createTexture(
            "WebglGpgpuWaterEnvironmentRGBA32Float",
            width,
            height,
            1u);
        environmentAtlas = device->createTexture(
            "WebglGpgpuWaterEnvironmentAtlasRGBA16Float",
            WebglGpgpuWaterPmremAtlasWidth,
            WebglGpgpuWaterPmremAtlasHeight,
            1u);
        environmentPingPongAtlas = device->createTexture(
            "WebglGpgpuWaterEnvironmentPingPongRGBA16Float",
            WebglGpgpuWaterPmremAtlasWidth,
            WebglGpgpuWaterPmremAtlasHeight,
            1u);
        pmremUniformBuffer0 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms0", 1u);
        pmremUniformBuffer1 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms1", 1u);
        pmremUniformBuffer2 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms2", 1u);
        pmremUniformBuffer3 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms3", 1u);
        pmremUniformBuffer4 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms4", 1u);
        pmremUniformBuffer5 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms5", 1u);
        pmremUniformBuffer6 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms6", 1u);
        pmremUniformBuffer7 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms7", 1u);
        pmremUniformBuffer8 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms8", 1u);
        pmremUniformBuffer9 = device->createBuffer(
            "WebglGpgpuWaterPmremUniforms9", 1u);
        environmentPrefilterResources = device->createBindGroup<
            WebglGpgpuWaterEnvironmentPrefilterBindGroup>(
            environmentTexture->createView(),
            environmentSampler,
            environmentAtlas->createView());
        environmentPrefilterPass = device->createComputeClass<
            WebglGpgpuWaterEnvironmentPrefilterPass>(
            environmentPrefilterResources);
        environmentResources =
            device->createBindGroup<WebglGpgpuWaterEnvironmentBindGroup>(
                screenUniformBuffer,
                environmentAtlas->createView(),
                environmentSampler);
        environmentPass =
            device->createRenderClass<WebglGpgpuWaterEnvironmentPass>(
                environmentResources);
        materialResources =
            device->createBindGroup<WebglGpgpuWaterMaterialBindGroup>(
                materialSampler,
                environmentAtlas->createView(),
                environmentSampler,
                dfgLutTexture->createView(),
                dfgSampler);
        opaquePass = device->createRenderClass<WebglGpgpuWaterOpaquePass>(
            sceneSet,
            materialResources,
            shadowSamplingResources);
        waterBackPass = device->createRenderClass<WebglGpgpuWaterBackPass>(
            sceneSet,
            materialResources,
            shadowSamplingResources);
        waterFrontPass = device->createRenderClass<WebglGpgpuWaterFrontPass>(
            sceneSet,
            materialResources,
            shadowSamplingResources);
        graphicsQueue
            ->writeTexture(
                environmentTexture,
                pixels.data(),
                uint64_t(width) * uint64_t(height) * sizeof(float4))
            ->computePass(
                "pmrem-equirectangular-to-cubeuv-level-zero",
                environmentPrefilterPass(
                    WebglGpgpuWaterPmremAtlasWidth,
                    WebglGpgpuWaterPmremCubeSize * 2u,
                    1u))
            ->submit();
        for (uint lodIndex = 1u;
             lodIndex < WebglGpgpuWaterPmremLodCount;
             ++lodIndex)
        {
            const uint mipExponent =
                lodIndex <= 4u ? 8u - lodIndex : 4u;
            const uint faceSize = 1u << mipExponent;
            const uint outputX =
                lodIndex > 4u
                    ? 3u * faceSize * (lodIndex - 4u)
                    : 0u;
            const uint outputY =
                4u * (WebglGpgpuWaterPmremCubeSize - faceSize);
            WebglGpgpuWaterPmremUniforms uniforms;
            uniforms.outputOffsetSize = uint4(
                outputX,
                outputY,
                faceSize * 3u,
                faceSize * 2u);
            uniforms.roughnessSourceMipAndReserved = float4(
                float(lodIndex) /
                    float(WebglGpgpuWaterPmremLodCount - 1u),
                9.0f - float(lodIndex),
                0.0f,
                0.0f);
            auto levelUniformBuffer =
                selectPmremUniformBuffer(lodIndex);
            auto filterResources = device->createBindGroup<
                WebglGpgpuWaterPmremFilterBindGroup>(
                levelUniformBuffer,
                environmentAtlas->createView(),
                environmentSampler,
                environmentPingPongAtlas->createView());
            auto filterPass = device->createComputeClass<
                WebglGpgpuWaterPmremFilterPass>(filterResources);
            auto copyResources = device->createBindGroup<
                WebglGpgpuWaterPmremCopyBindGroup>(
                levelUniformBuffer,
                environmentPingPongAtlas->createView(),
                environmentAtlas->createView());
            auto copyPass = device->createComputeClass<
                WebglGpgpuWaterPmremCopyPass>(copyResources);
            graphicsQueue
                ->writeBuffer(
                    BufferRange(levelUniformBuffer),
                    &uniforms,
                    sizeof(uniforms))
                ->computePass(
                    "pmrem-incremental-ggx-vndf-filter",
                    filterPass(
                        faceSize * 3u,
                        faceSize * 2u,
                        1u))
                ->computePass(
                    "pmrem-copy-filtered-level-to-main-atlas",
                    copyPass(
                        faceSize * 3u,
                        faceSize * 2u,
                        1u));
        }
        graphicsQueue->submit();
    }

    /** Uploads the exact initial height payload to both frozen ping-pong textures. */
    void configureHeight(const eastl::vector<float4> &initialHeight)
    {
        graphicsQueue
            ->writeTexture(
                heightTexture0,
                initialHeight.data(),
                uint64_t(WebglGpgpuWaterTextureWidth) *
                    uint64_t(WebglGpgpuWaterTextureWidth) * sizeof(float4))
            ->writeTexture(
                heightTexture1,
                initialHeight.data(),
                uint64_t(WebglGpgpuWaterTextureWidth) *
                    uint64_t(WebglGpgpuWaterTextureWidth) * sizeof(float4))
            ->submit();
        currentHeightIndex = 0u;
        heightTickCount = 0u;
    }

    /** Uploads the fixed shadow state and selects whether VSM filtering affects lighting. */
    void configureScenePasses(uint inShadowEnabled)
    {
        shadowEnabled = inShadowEnabled;
        WebglGpgpuWaterShadowUniforms uniforms;
        uniforms.lightRight = float4(
            0.813733471206735f,
            0.0f,
            0.5812381937190964f,
            0.0f);
        uniforms.lightUp = float4(
            0.484723817654573f,
            0.5518394231759755f,
            -0.6786133447164023f,
            0.0f);
        uniforms.lightBackwardAndEnabled = float4(
            -0.32075014954979203f,
            0.8339503888294594f,
            0.44905020936970885f,
            float(inShadowEnabled));
        uniforms.lightPositionAndBias =
            float4(-1.0f, 2.6f, 1.4f, -0.0005f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(shadowUniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
    }

    /** Executes exactly one private RGBA32Float fragment step and flips current state. */
    void advanceHeight(WebglGpgpuWaterHeightUniforms uniforms)
    {
        WebglGpgpuWaterHeightFrameBuffer frameBuffer;
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        graphicsQueue->writeBuffer(
            BufferRange(heightUniformBuffer),
            &uniforms,
            sizeof(uniforms));
        if (currentHeightIndex == 0u)
        {
            frameBuffer.color = heightTexture1->createView();
            graphicsQueue->renderPass(
                "WebglGpgpuWaterHeight0To1",
                frameBuffer,
                heightPass0To1(3u, 1u, 0u, 0u));
            currentHeightIndex = 1u;
        }
        else
        {
            frameBuffer.color = heightTexture0->createView();
            graphicsQueue->renderPass(
                "WebglGpgpuWaterHeight1To0",
                frameBuffer,
                heightPass1To0(3u, 1u, 0u, 0u));
            currentHeightIndex = 0u;
        }
        graphicsQueue->submit();
        heightTickCount += 1u;
    }

    /** Updates the Set and submits only RenderSet-indexed Scene geometry invocations. */
    void render() override
    {
        sceneSet->update();
        WebglGpgpuWaterHdrScreenFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = sceneColorTexture->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        WebglGpgpuWaterSceneFrameBuffer clearFrameBuffer;
        clearFrameBuffer.color = sceneColorTexture->createView();
        clearFrameBuffer.color.loadOp = LoadOp::Load;
        clearFrameBuffer.color.storeOp = StoreOp::Store;
        clearFrameBuffer.depth = depthTexture->createView();
        clearFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        clearFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        clearFrameBuffer.depth.depthClearValue = 1.0f;

        WebglGpgpuWaterSceneFrameBuffer loadFrameBuffer;
        loadFrameBuffer.color = sceneColorTexture->createView();
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.color.storeOp = StoreOp::Store;
        loadFrameBuffer.depth = depthTexture->createView();
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthStoreOp = StoreOp::Store;

        WebglGpgpuWaterShadowFrameBuffer shadowFrameBuffer;
        shadowFrameBuffer.depth = shadowDepthTexture->createView();
        shadowFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer.depth.depthClearValue = 1.0f;

        WebglGpgpuWaterVsmFrameBuffer verticalVsmFrameBuffer;
        verticalVsmFrameBuffer.moments =
            shadowVerticalMomentsTexture->createView();
        verticalVsmFrameBuffer.moments.loadOp = LoadOp::Clear;
        verticalVsmFrameBuffer.moments.storeOp = StoreOp::Store;
        verticalVsmFrameBuffer.moments.clearValue = {1.0, 0.0, 0.0, 0.0};

        WebglGpgpuWaterVsmFrameBuffer horizontalVsmFrameBuffer;
        horizontalVsmFrameBuffer.moments =
            shadowDistributionTexture->createView();
        horizontalVsmFrameBuffer.moments.loadOp = LoadOp::Clear;
        horizontalVsmFrameBuffer.moments.storeOp = StoreOp::Store;
        horizontalVsmFrameBuffer.moments.clearValue = {1.0, 0.0, 0.0, 0.0};

        WebglGpgpuWaterOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass(
            "directional-shadow-depth",
            shadowFrameBuffer,
            shadowPass());
        if (shadowEnabled != 0u)
        {
            graphicsQueue
                ->renderPass(
                    "vsm-shadow-filter-vertical",
                    verticalVsmFrameBuffer,
                    vsmVerticalPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "vsm-shadow-filter-horizontal",
                    horizontalVsmFrameBuffer,
                    vsmHorizontalPass(3u, 1u, 0u, 0u));
        }
        graphicsQueue
            ->renderPass(
                "equirectangular-background",
                backgroundFrameBuffer,
                environmentPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "main-opaque-pbr",
                clearFrameBuffer,
                opaquePass())
            ->renderPass(
                "main-transparent-water-back",
                loadFrameBuffer,
                waterBackPass())
            ->renderPass(
                "main-transparent-water-front",
                loadFrameBuffer,
                waterFrontPass())
            ->renderPass(
                "main-wireframe-phase",
                loadFrameBuffer,
                wireframePass())
            ->renderPass(
                "aces-tone-map",
                outputFrameBuffer,
                toneMapPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target used for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Returns the current full-precision height texture after zero or more steps. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
    getCurrentHeightTextureHandle() const
    {
        return currentHeightIndex == 0u ? heightTexture0 : heightTexture1;
    }

    /** Returns the number of fragment height steps submitted by the generated renderer. */
    uint getHeightTickCount() const
    {
        return heightTickCount;
    }

    /** Releases the unique Set and every private simulation or attachment resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(heightUniformBuffer);
        device->freeBuffer(screenUniformBuffer);
        device->freeBuffer(shadowUniformBuffer);
        device->freeBuffer(pmremUniformBuffer0);
        device->freeBuffer(pmremUniformBuffer1);
        device->freeBuffer(pmremUniformBuffer2);
        device->freeBuffer(pmremUniformBuffer3);
        device->freeBuffer(pmremUniformBuffer4);
        device->freeBuffer(pmremUniformBuffer5);
        device->freeBuffer(pmremUniformBuffer6);
        device->freeBuffer(pmremUniformBuffer7);
        device->freeBuffer(pmremUniformBuffer8);
        device->freeBuffer(pmremUniformBuffer9);
        device->freeTexture(heightTexture0);
        device->freeTexture(heightTexture1);
        device->freeTexture(environmentTexture);
        device->freeTexture(environmentAtlas);
        device->freeTexture(environmentPingPongAtlas);
        device->freeTexture(dfgLutTexture);
        device->freeTexture(sceneColorTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
        device->freeTexture(shadowDepthTexture);
        device->freeTexture(shadowVerticalMomentsTexture);
        device->freeTexture(shadowDistributionTexture);
    }
};

#endif
