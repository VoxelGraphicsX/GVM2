#ifndef GVM_THREE_WEBGPU_PARALLAX_UV_HPP
#define GVM_THREE_WEBGPU_PARALLAX_UV_HPP

#include "UGL.h"

#include <EASTL/array.h>
#include <EASTL/vector.h>

using namespace UGL;

static const uint WebgpuParallaxUvPmremCubeSize = 256u;
static const uint WebgpuParallaxUvPmremAtlasWidth = 768u;
static const uint WebgpuParallaxUvPmremAtlasHeight = 1024u;
static const uint WebgpuParallaxUvPmremLodCount = 11u;
// Three.js r185's WebGPU PMREM generator uses a fixed 512-sample Hammersley
// sequence for the GGX VNDF convolution.  Keep the sample count identical so
// the existing Compute path reproduces the reference radiance distribution.
static const uint WebgpuParallaxUvPmremSampleCount = 512u;
static const uint WebgpuParallaxUvMaterialBaseSize = 1024u;
static const uint WebgpuParallaxUvMaterialMipCount = 11u;
static const uint WebgpuParallaxUvMaterialAtlasWidth = 1026u;
static const uint WebgpuParallaxUvMaterialAtlasHeight = 2070u;

/** Stores one exact CircleGeometry position, normal, and texture coordinate. */
struct WebgpuParallaxUvVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Stores the deterministic camera basis and mutable parallax controls. */
struct WebgpuParallaxUvUniforms
{
    float4 cameraPositionAndScale;
    float4 cameraRightAndBackgroundBlur;
    float4 cameraUpAndParallaxScale;
    float4 cameraForwardAndTanHalfFov;
    float4 viewportAndExposure;
    float4 outputFlagsAndOffset;
};

/** Stores one ordered r185 PMREM incremental-filter dispatch configuration. */
struct WebgpuParallaxUvPmremUniforms
{
    uint4 outputOffsetSize;
    float4 roughnessSourceMipAndReserved;
};

/** Binds all locked material maps, the environment, and frame state. */
struct WebgpuParallaxUvResources final : public IBindGroup
{
    /** Declares the five Ice maps, filtered environment, and DFG LUT. */
    constructor(
        UniformBuffer<WebgpuParallaxUvUniforms> uniforms [[Binding0]],
        Texture2D<float4> topColor [[Binding1]],
        Texture2D<float4> bottomColor [[Binding2]],
        Texture2D<float4> roughness [[Binding3]],
        Texture2D<float4> normal [[Binding4]],
        Texture2D<float4> displacement [[Binding5]],
        Texture2D<half4> environmentAtlas [[Binding6]],
        Texture2D<half4> dfgLut [[Binding7]],
        Sampler materialSampler [[Binding8]],
        Sampler environmentSampler [[Binding9]],
        Sampler dfgSampler [[Binding10]])
    {
    }
};

/** Binds the decoded equirectangular source to the PMREM base conversion. */
struct WebgpuParallaxUvPmremSourceResources final : public IBindGroup
{
    /** Declares the HDR source, sampler, and writable packed atlas. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding2]])
    {
    }
};

/** Binds one immutable PMREM level and separate source and destination atlases. */
struct WebgpuParallaxUvPmremFilterResources final : public IBindGroup
{
    /** Declares the level state and non-aliasing filter resources. */
    constructor(
        UniformBuffer<WebgpuParallaxUvPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        Sampler atlasSampler [[Binding2]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding3]])
    {
    }
};

/** Binds one completed PMREM level for copying back into the main atlas. */
struct WebgpuParallaxUvPmremCopyResources final : public IBindGroup
{
    /** Declares the active level and non-overlapping copy resources. */
    constructor(
        UniformBuffer<WebgpuParallaxUvPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding2]])
    {
    }
};

/** Binds the linear Scene texture to the dedicated output transfer pass. */
struct WebgpuParallaxUvOutputResources final : public IBindGroup
{
    /** Declares the linear Scene, nearest sampler, and exposure state. */
    constructor(
        Texture2D<half4> sceneColor [[Binding0]],
        Sampler outputSampler [[Binding1]],
        UniformBuffer<WebgpuParallaxUvUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries world-space ground data into the dedicated material fragment. */
struct WebgpuParallaxUvGroundOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float2 textureCoordinate [[Attribute1]];
};

/** Carries a fullscreen environment reconstruction coordinate. */
struct WebgpuParallaxUvScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the single-sample linear Scene color and shared depth attachments. */
struct WebgpuParallaxUvLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 output attachment. */
struct WebgpuParallaxUvOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to the r185 output transfer function. */
float webgpuParallaxUvLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Converts one authored sRGB channel into linear working space. */
float webgpuParallaxUvSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Converts one authored sRGB color into linear working space. */
float3 webgpuParallaxUvSrgbColorToLinear(float3 value)
{
    return float3(
        webgpuParallaxUvSrgbToLinear(value.x),
        webgpuParallaxUvSrgbToLinear(value.y),
        webgpuParallaxUvSrgbToLinear(value.z));
}

/** Applies the sample's Reinhard tone map and output transfer. */
float3 webgpuParallaxUvOutputColor(float3 linearColor, float exposure)
{
    linearColor *= exposure;
    linearColor = linearColor / (float3(1.0f) + linearColor);
    return float3(
        webgpuParallaxUvLinearToSrgb(linearColor.x),
        webgpuParallaxUvLinearToSrgb(linearColor.y),
        webgpuParallaxUvLinearToSrgb(linearColor.z));
}

/** Maps one world direction into Three's equirectangular texture convention. */
float2 webgpuParallaxUvEnvironmentUv(float3 direction)
{
    const float3 normalizedDirection = normalize(direction);
    return float2(
        atan2(normalizedDirection.z, normalizedDirection.x) *
                0.15915494309189535f +
            0.5f,
        0.5f - asin(clamp(normalizedDirection.y, -1.0f, 1.0f)) *
            0.3183098861837907f);
}

/** Reverses one unsigned bit pattern into a Van der Corput sample. */
float webgpuParallaxUvRadicalInverse(uint bits)
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

/** Returns the packed cubeUV face selected by one nonzero direction. */
uint webgpuParallaxUvPmremFace(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
            return direction.x > 0.0f ? 0u : 3u;
        return direction.y > 0.0f ? 1u : 4u;
    }
    if (absoluteDirection.z > absoluteDirection.y)
        return direction.z > 0.0f ? 2u : 5u;
    return direction.y > 0.0f ? 1u : 4u;
}

/** Maps one direction to normalized coordinates within its packed cubeUV face. */
float2 webgpuParallaxUvPmremFaceUv(float3 direction, uint face)
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

/** Maps one padded packed-face coordinate back to a world direction. */
float3 webgpuParallaxUvPmremDirection(float2 uv, uint face)
{
    const float2 signedUv = uv * 2.0f - 1.0f;
    if (face == 0u) return float3(1.0f, signedUv.y, signedUv.x);
    if (face == 1u) return float3(-signedUv.x, 1.0f, -signedUv.y);
    if (face == 2u) return float3(-signedUv.x, signedUv.y, 1.0f);
    if (face == 3u) return float3(-1.0f, signedUv.y, -signedUv.x);
    if (face == 4u) return float3(-signedUv.x, -1.0f, signedUv.y);
    return float3(signedUv.x, signedUv.y, -1.0f);
}

/** Returns the r185 packed cubeUV atlas coordinate for a virtual mip. */
float2 webgpuParallaxUvPmremAtlasUv(float3 direction, float sourceMip)
{
    uint face = webgpuParallaxUvPmremFace(direction);
    const float filterIndex = max(4.0f - sourceMip, 0.0f);
    const float storedMip = max(sourceMip, 4.0f);
    const float faceSize = exp2(storedMip);
    float2 uv = webgpuParallaxUvPmremFaceUv(direction, face) *
        (faceSize - 2.0f) + 1.0f;
    if (face > 2u)
    {
        uv.y += faceSize;
        face -= 3u;
    }
    uv.x += float(face) * faceSize;
    uv.x += filterIndex * 48.0f;
    uv.y += 4.0f * (float(WebgpuParallaxUvPmremCubeSize) - faceSize);
    return uv / float2(
        float(WebgpuParallaxUvPmremAtlasWidth),
        float(WebgpuParallaxUvPmremAtlasHeight));
}

/** Converts Standard roughness into Three r185's nonlinear virtual PMREM mip. */
float webgpuParallaxUvRoughnessToMip(float roughness)
{
    if (roughness >= 0.8f) return (1.0f - roughness) * 5.0f - 2.0f;
    if (roughness >= 0.4f) return (0.8f - roughness) * 7.5f - 1.0f;
    if (roughness >= 0.305f)
        return (0.4f - roughness) * 10.526315789473684f + 2.0f;
    if (roughness >= 0.21f)
        return (0.305f - roughness) * 10.526315789473684f + 3.0f;
    return -2.0f * log2(1.16f * max(roughness, 0.000001f));
}

/** Samples the exact packed PMREM levels selected by one material roughness. */
float3 webgpuParallaxUvSampleEnvironment(
    BindGroup<WebgpuParallaxUvResources> resources,
    float3 direction,
    float roughness)
{
    direction = normalize(float3(direction.x, -direction.y, direction.z));
    const float mip = clamp(
        webgpuParallaxUvRoughnessToMip(roughness), -2.0f, 8.0f);
    const float lowerMip = floor(mip);
    const float interpolation = mip - lowerMip;
    const float3 lowerColor = float4(resources->environmentAtlas->sampleGrad(
        resources->environmentSampler,
        webgpuParallaxUvPmremAtlasUv(direction, lowerMip),
        float2(0.0f), float2(0.0f))).xyz;
    if (interpolation == 0.0f) return lowerColor;
    const float3 upperColor = float4(resources->environmentAtlas->sampleGrad(
        resources->environmentSampler,
        webgpuParallaxUvPmremAtlasUv(direction, lowerMip + 1.0f),
        float2(0.0f), float2(0.0f))).xyz;
    return lerp(lowerColor, upperColor, interpolation);
}

/** Samples one wrapped material mip from the single-mip packed atlas. */
float4 webgpuParallaxUvSampleMaterialMip(
    IN BindGroup<WebgpuParallaxUvResources> resources,
    uint textureIndex,
    float2 uv,
    uint mipLevel)
{
    const uint mipSize = max(
        1u, WebgpuParallaxUvMaterialBaseSize >> mipLevel);
    const uint mipOffsetY =
        2u * mipLevel + 2048u - (2048u >> mipLevel);
    const float2 atlasUv = float2(
        (1.0f + frac(uv.x) * float(mipSize)) /
            float(WebgpuParallaxUvMaterialAtlasWidth),
        (float(mipOffsetY + 1u) + frac(uv.y) * float(mipSize)) /
            float(WebgpuParallaxUvMaterialAtlasHeight));
    if (textureIndex == 0u)
        return resources->topColor->sampleLevel(
            resources->materialSampler, atlasUv, 0.0f);
    if (textureIndex == 1u)
        return resources->bottomColor->sampleLevel(
            resources->materialSampler, atlasUv, 0.0f);
    if (textureIndex == 2u)
        return resources->roughness->sampleLevel(
            resources->materialSampler, atlasUv, 0.0f);
    if (textureIndex == 3u)
        return resources->normal->sampleLevel(
            resources->materialSampler, atlasUv, 0.0f);
    return resources->displacement->sampleLevel(
        resources->materialSampler, atlasUv, 0.0f);
}

/** Reproduces implicit trilinear material sampling through the packed mip atlas. */
float4 webgpuParallaxUvSampleMaterial(
    IN BindGroup<WebgpuParallaxUvResources> resources,
    uint textureIndex,
    float2 uv)
{
    const float2 derivativeX =
        ddx(uv) * float(WebgpuParallaxUvMaterialBaseSize);
    const float2 derivativeY =
        ddy(uv) * float(WebgpuParallaxUvMaterialBaseSize);
    const float footprintSquared = max(
        dot(derivativeX, derivativeX),
        dot(derivativeY, derivativeY));
    const float lod = clamp(
        0.5f * log2(max(footprintSquared, 1.0f)),
        0.0f,
        float(WebgpuParallaxUvMaterialMipCount - 1u));
    const uint lowerMip = uint(floor(lod));
    const uint upperMip = min(
        lowerMip + 1u,
        WebgpuParallaxUvMaterialMipCount - 1u);
    return lerp(
        webgpuParallaxUvSampleMaterialMip(
            resources, textureIndex, uv, lowerMip),
        webgpuParallaxUvSampleMaterialMip(
            resources, textureIndex, uv, upperMip),
        frac(lod));
}

/** Reproduces the TSL overlay blend used by the two Ice color maps. */
float3 webgpuParallaxUvBlendOverlay(float3 base, float3 blend)
{
    const float3 low = base * 2.0f * blend;
    const float3 high = float3(1.0f) -
        2.0f * (float3(1.0f) - base) * (float3(1.0f) - blend);
    return float3(
        base.x < 0.5f ? low.x : high.x,
        base.y < 0.5f ? low.y : high.y,
        base.z < 0.5f ? low.z : high.z);
}

/** Converts the HDR equirectangular source into the padded PMREM cubeUV base. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuParallaxUvPmremSourcePass final : public IComputeClass
{
public:
    /** Binds the immutable equirectangular source and writable PMREM atlas. */
    constructor(
        BindGroup<WebgpuParallaxUvPmremSourceResources> sourceResources [[Slot0]])
    {
    }

private:
    /** Writes one level-zero cubeUV texel with Three's equirectangular mapping. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebgpuParallaxUvPmremAtlasWidth ||
            threadID.y >= WebgpuParallaxUvPmremCubeSize * 2u)
            return;
        const uint faceColumn = threadID.x / WebgpuParallaxUvPmremCubeSize;
        const uint faceRow = threadID.y / WebgpuParallaxUvPmremCubeSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x - faceColumn * WebgpuParallaxUvPmremCubeSize,
            threadID.y - faceRow * WebgpuParallaxUvPmremCubeSize);
        const float2 faceUv = (float2(localCoordinate) - 0.5f) /
            float(WebgpuParallaxUvPmremCubeSize - 2u);
        const float3 direction = normalize(
            webgpuParallaxUvPmremDirection(faceUv, face));
        const float2 uv = float2(
            atan2(direction.z, direction.x) * 0.15915494309189535f + 0.5f,
            asin(clamp(direction.y, -1.0f, 1.0f)) *
                    0.3183098861837907f +
                0.5f);
        sourceResources->destinationAtlas->write(
            threadID.xy,
            sourceResources->source->sampleLevel(
                sourceResources->sourceSampler, uv, 0.0f));
    }
};

/** Applies one ordered r185 WebGPU incremental GGX VNDF PMREM filter level. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuParallaxUvPmremFilterPass final : public IComputeClass
{
public:
    /** Binds one level state and separate main-to-ping-pong atlas resources. */
    constructor(
        BindGroup<WebgpuParallaxUvPmremFilterResources> filterResources [[Slot0]])
    {
    }

private:
    /** Evaluates the fixed 512-sample deterministic r185 WebGPU VNDF filter per texel. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint4 output = filterResources->uniforms->outputOffsetSize;
        if (threadID.x >= output.z || threadID.y >= output.w) return;
        const uint faceSize = output.z / 3u;
        const uint faceColumn = threadID.x / faceSize;
        const uint faceRow = threadID.y / faceSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x - faceColumn * faceSize,
            threadID.y - faceRow * faceSize);
        const float2 faceUv =
            (float2(localCoordinate) - 0.5f) / float(faceSize - 2u);
        const float3 normal = normalize(
            webgpuParallaxUvPmremDirection(faceUv, face));
        const float targetRoughness =
            filterResources->uniforms->roughnessSourceMipAndReserved.x;
        const float sourceRoughness = targetRoughness -
            1.0f / float(WebgpuParallaxUvPmremLodCount - 1u);
        const float incrementalRoughness = sqrt(max(
            targetRoughness * targetRoughness -
                sourceRoughness * sourceRoughness,
            0.0f));
        const float roughness = incrementalRoughness *
            targetRoughness * 1.25f;
        const float sourceMip =
            filterResources->uniforms->roughnessSourceMipAndReserved.y;
        float3 filteredColor = float3(0.0f);
        float totalWeight = 0.0f;
        if (roughness < 0.001f)
        {
            filteredColor = float4(filterResources->sourceAtlas->sampleLevel(
                filterResources->atlasSampler,
                webgpuParallaxUvPmremAtlasUv(normal, sourceMip),
                0.0f)).xyz;
            totalWeight = 1.0f;
        }
        else
        {
            const float3 referenceAxis = abs(normal.z) < 0.999f
                ? float3(0.0f, 0.0f, 1.0f)
                : float3(1.0f, 0.0f, 0.0f);
            const float3 tangent = normalize(cross(referenceAxis, normal));
            const float3 bitangent = cross(normal, tangent);
            const float alpha = roughness * roughness;
            for (uint sampleIndex = 0u;
                 sampleIndex < WebgpuParallaxUvPmremSampleCount;
                 ++sampleIndex)
            {
                const float2 xi = float2(
                    float(sampleIndex) /
                        float(WebgpuParallaxUvPmremSampleCount),
                    webgpuParallaxUvRadicalInverse(sampleIndex));
                const float diskRadius = sqrt(xi.x);
                const float azimuth = 6.283185307179586f * xi.y;
                const float diskX = diskRadius * cos(azimuth);
                const float diskY = diskRadius * sin(azimuth);
                const float viewBlend = 1.0f;
                const float projectedY = (1.0f - viewBlend) *
                    sqrt(max(0.0f, 1.0f - diskX * diskX)) +
                    viewBlend * diskY;
                const float3 projectedNormal = float3(
                    diskX, projectedY,
                    sqrt(max(0.0f,
                             1.0f - diskX * diskX - projectedY * projectedY)));
                const float3 halfVectorTangent = normalize(float3(
                    alpha * projectedNormal.x,
                    alpha * projectedNormal.y,
                    max(0.0f, projectedNormal.z)));
                const float3 halfVector = normalize(
                    tangent * halfVectorTangent.x +
                    bitangent * halfVectorTangent.y +
                    normal * halfVectorTangent.z);
                const float3 sampledDirection = normalize(
                    halfVector * dot(normal, halfVector) * 2.0f - normal);
                const float sampleWeight = max(
                    dot(normal, sampledDirection), 0.0f);
                if (sampleWeight > 0.0f)
                {
                    filteredColor += float4(
                        filterResources->sourceAtlas->sampleLevel(
                            filterResources->atlasSampler,
                            webgpuParallaxUvPmremAtlasUv(
                                sampledDirection, sourceMip),
                            0.0f)).xyz * sampleWeight;
                    totalWeight += sampleWeight;
                }
            }
        }
        filterResources->destinationAtlas->write(
            uint2(output.x, output.y) + threadID.xy,
            half4(float4(
                filteredColor / max(totalWeight, 0.0001f), 1.0f)));
    }
};

/** Copies one completed PMREM target level from ping-pong to the main atlas. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuParallaxUvPmremCopyPass final : public IComputeClass
{
public:
    /** Binds the active level and non-overlapping copy resources. */
    constructor(
        BindGroup<WebgpuParallaxUvPmremCopyResources> copyResources [[Slot0]])
    {
    }

private:
    /** Copies one half-float texel without filtering or format conversion. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint4 output = copyResources->uniforms->outputOffsetSize;
        if (threadID.x >= output.z || threadID.y >= output.w) return;
        const uint2 coordinate = uint2(output.x, output.y) + threadID.xy;
        copyResources->destinationAtlas->write(
            coordinate, copyResources->sourceAtlas->read(coordinate));
    }
};

/** Draws the blurred HDR environment behind the sole ground object. */
class WebgpuParallaxUvBackgroundPass final : public IRenderClass
{
public:
    /** Configures a depth-disabled fullscreen environment pass. */
    constructor(BindGroup<WebgpuParallaxUvResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuParallaxUvScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuParallaxUvScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray and applies background blurriness. */
    WebgpuParallaxUvLinearFrameBuffer fragment(WebgpuParallaxUvScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float aspect = resources->uniforms->viewportAndExposure.x /
            resources->uniforms->viewportAndExposure.y;
        const float tangentHalfFov =
            resources->uniforms->cameraForwardAndTanHalfFov.w;
        const float3 ray = normalize(
            resources->uniforms->cameraForwardAndTanHalfFov.xyz +
            resources->uniforms->cameraRightAndBackgroundBlur.xyz *
                (ndc.x * aspect * tangentHalfFov) -
            resources->uniforms->cameraUpAndParallaxScale.xyz *
                (ndc.y * tangentHalfFov));
        const float3 linearColor = webgpuParallaxUvSampleEnvironment(
            resources,
            ray,
            resources->uniforms->cameraRightAndBackgroundBlur.w);
        WebgpuParallaxUvLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the sole CircleGeometry with dedicated parallax and standard shading. */
class WebgpuParallaxUvScenePass final : public IRenderClass
{
public:
    /** Configures the ordinary indexed single-object scene pass. */
    constructor(BindGroup<WebgpuParallaxUvResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects the horizontal CircleGeometry through the deterministic camera basis. */
    WebgpuParallaxUvGroundOutput vertex(WebgpuParallaxUvVertex inputValue [[VertexInput0]])
    {
        const float3 worldPosition = float3(
            inputValue.position.x,
            inputValue.position.z,
            -inputValue.position.y);
        const float3 relative = worldPosition -
            resources->uniforms->cameraPositionAndScale.xyz;
        const float viewX =
            relative.x * resources->uniforms->cameraRightAndBackgroundBlur.x +
            relative.y * resources->uniforms->cameraRightAndBackgroundBlur.y +
            relative.z * resources->uniforms->cameraRightAndBackgroundBlur.z;
        const float viewY =
            relative.x * resources->uniforms->cameraUpAndParallaxScale.x +
            relative.y * resources->uniforms->cameraUpAndParallaxScale.y +
            relative.z * resources->uniforms->cameraUpAndParallaxScale.z;
        const float viewDepth =
            relative.x * resources->uniforms->cameraForwardAndTanHalfFov.x +
            relative.y * resources->uniforms->cameraForwardAndTanHalfFov.y +
            relative.z * resources->uniforms->cameraForwardAndTanHalfFov.z;
        const float tangentHalfFov =
            resources->uniforms->cameraForwardAndTanHalfFov.w;
        const float aspect = resources->uniforms->viewportAndExposure.x /
            resources->uniforms->viewportAndExposure.y;
        WebgpuParallaxUvGroundOutput outputValue;
        outputValue.position = float4(
            viewX / (tangentHalfFov * aspect) -
                resources->uniforms->outputFlagsAndOffset.y * viewDepth /
                    resources->uniforms->viewportAndExposure.x,
            -viewY / tangentHalfFov -
                resources->uniforms->outputFlagsAndOffset.z * viewDepth /
                    resources->uniforms->viewportAndExposure.y,
            viewDepth - 0.2002002f,
            viewDepth);
        outputValue.worldPosition = worldPosition;
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        return outputValue;
    }

    /** Evaluates exact parallax UV and map combination with environment lighting. */
    WebgpuParallaxUvLinearFrameBuffer fragment(WebgpuParallaxUvGroundOutput inputValue)
    {
        const float uvScale = resources->uniforms->cameraPositionAndScale.w;
        const float2 scaledUv = inputValue.textureCoordinate * uvScale;
        const float3 cameraRight = resources->uniforms->cameraRightAndBackgroundBlur.xyz;
        const float3 cameraUp = resources->uniforms->cameraUpAndParallaxScale.xyz;
        const float3 cameraForward = resources->uniforms->cameraForwardAndTanHalfFov.xyz;
        const float3 cameraPosition = resources->uniforms->cameraPositionAndScale.xyz;
        const float3 viewNormal = normalize(float3(
            cameraRight.y, cameraUp.y, cameraForward.y));
        const float3 relativeWorldPosition =
            inputValue.worldPosition - cameraPosition;
        const float3 viewPosition = float3(
            dot(relativeWorldPosition, cameraRight),
            dot(relativeWorldPosition, cameraUp),
            dot(relativeWorldPosition, cameraForward));
        const float3 q0 = ddx(viewPosition);
        const float3 q1 = ddy(viewPosition);
        const float2 st0 = ddx(inputValue.textureCoordinate);
        const float2 st1 = ddy(inputValue.textureCoordinate);
        const float3 q1Perp = cross(q1, viewNormal);
        const float3 q0Perp = cross(viewNormal, q0);
        const float3 tangent = q1Perp * st0.x + q0Perp * st1.x;
        const float3 bitangent = q1Perp * st0.y + q0Perp * st1.y;
        const float tangentDet = max(dot(tangent, tangent),
            dot(bitangent, bitangent));
        const float tangentScale = tangentDet == 0.0f
            ? 0.0f
            : rsqrt(tangentDet);
        const float3 tangentPlaneView = tangent * tangentScale;
        const float3 bitangentPlaneView = bitangent * tangentScale;
        // TSL's parallaxDirection is positionViewDirection multiplied by the
        // dynamically reconstructed view-space TBN matrix.  Keep the
        // multiplication in view space: using world-space x/z here changes
        // the offset whenever the orbit camera is tilted.
        const float3 positionViewDirection = normalize(-viewPosition);
        const float2 parallaxDirection = float2(
            dot(positionViewDirection, tangentPlaneView),
            dot(positionViewDirection, bitangentPlaneView));
        const float3 viewDirection = normalize(
            resources->uniforms->cameraPositionAndScale.xyz -
            inputValue.worldPosition);
        const float displacement = webgpuParallaxUvSampleMaterial(
            resources,
            4u,
            scaledUv).x *
            resources->uniforms->cameraUpAndParallaxScale.w;
        const float2 parallaxUv = scaledUv -
            parallaxDirection * displacement;
        const float3 topColor = webgpuParallaxUvSampleMaterial(
            resources,
            0u,
            scaledUv).xyz;
        const float3 bottomColor = webgpuParallaxUvSampleMaterial(
            resources,
            1u,
            parallaxUv).xyz;
        const float3 baseColor =
            webgpuParallaxUvBlendOverlay(topColor, bottomColor) * 5.0f;
        const float3 mappedNormal =
            webgpuParallaxUvSampleMaterial(
                resources,
                3u,
                scaledUv).xyz *
            2.0f -
            float3(1.0f);
        const float3 mappedViewNormal = normalize(
            tangentPlaneView * mappedNormal.x +
            bitangentPlaneView * mappedNormal.y +
            viewNormal * mappedNormal.z);
        const float3 worldNormal = normalize(
            cameraRight * mappedViewNormal.x +
            cameraUp * mappedViewNormal.y +
            cameraForward * mappedViewNormal.z);
        const float roughness = clamp(
            webgpuParallaxUvSampleMaterial(
                resources,
                2u,
                scaledUv).x,
            0.0525f,
            1.0f);
        const float3 reflected = normalize(lerp(
            reflect(-viewDirection, worldNormal),
            worldNormal,
            roughness * roughness * roughness * roughness));
        const float dotNV = clamp(dot(worldNormal, viewDirection), 0.0f, 1.0f);
        const float2 dfg = float4(resources->dfgLut->sample(
            resources->dfgSampler, float2(roughness, dotNV))).xy;
        const float3 f0 = float3(0.04f);
        const float3 singleScatter = f0 * dfg.x + float3(dfg.y);
        const float energyRemainder = 1.0f - dfg.x - dfg.y;
        const float3 averageFresnel =
            f0 + (float3(1.0f) - f0) * 0.047619f;
        const float3 multiScatter =
            singleScatter * averageFresnel /
            (float3(1.0f) - energyRemainder * averageFresnel +
             float3(0.000001f)) *
            energyRemainder;
        const float3 radiance = webgpuParallaxUvSampleEnvironment(
            resources, reflected, roughness);
        const float3 irradianceOverPi = webgpuParallaxUvSampleEnvironment(
            resources, worldNormal, 1.0f);
        const float3 linearColor =
            radiance * singleScatter +
            irradianceOverPi * multiScatter +
            baseColor *
                (float3(1.0f) - singleScatter - multiScatter) *
                irradianceOverPi;
        WebgpuParallaxUvLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies Reinhard exposure and output transfer to the linear Scene result. */
class WebgpuParallaxUvOutputPass final : public IRenderClass
{
public:
    /** Binds the linear Scene and disables geometry state. */
    constructor(BindGroup<WebgpuParallaxUvOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen output triangle. */
    WebgpuParallaxUvScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuParallaxUvScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Encodes the linear Scene with the sample's fixed Reinhard operator. */
    WebgpuParallaxUvOutputFrameBuffer fragment(
        WebgpuParallaxUvScreenOutput inputValue)
    {
        const float3 linearColor = float4(resources->sceneColor->sampleLevel(
            resources->outputSampler, inputValue.uv, 0.0f)).xyz;
        WebgpuParallaxUvOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(webgpuParallaxUvOutputColor(
            linearColor,
            resources->uniforms->viewportAndExposure.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Composites the deterministic Inspector strip used by orbit replays. */
class WebgpuParallaxUvInspectorPass final : public IRenderClass
{
public:
    /** Enables premultiplied-alpha overlay over the tone-mapped output. */
    constructor(BindGroup<WebgpuParallaxUvOutputResources> resources [[Slot0]])
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
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the fixed Inspector overlay. */
    WebgpuParallaxUvScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuParallaxUvScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Draws the two flat Inspector segments with a CSS-compatible rounded edge. */
    WebgpuParallaxUvOutputFrameBuffer fragment(
        WebgpuParallaxUvScreenOutput inputValue)
    {
        const float enabled = resources->uniforms->outputFlagsAndOffset.x;
        const float2 viewport = resources->uniforms->viewportAndExposure.xy;
        const float2 pixel = float2(
            inputValue.uv.x * viewport.x,
            inputValue.uv.y * viewport.y);
        const float2 center = float2(699.5f, 34.0f);
        const float2 halfExtent = float2(84.5f, 18.0f);
        const float2 relative = pixel - center;
        const float radius = relative.x < 0.0f ? 12.0f : 6.0f;
        const float2 q = abs(relative) - halfExtent + radius;
        const float distanceToBox = length(max(q, float2(0.0f))) +
            min(max(q.x, q.y), 0.0f) - radius - 0.35f;
        const float alpha = enabled * (1.0f - smoothstep(0.0f, 1.0f,
            distanceToBox));
        const float3 panelColor = pixel.x < 663.0f
            ? float3(44.0f / 255.0f, 79.0f / 255.0f, 101.0f / 255.0f)
            : float3(55.0f / 255.0f, 56.0f / 255.0f, 63.0f / 255.0f);
        WebgpuParallaxUvOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(panelColor), half(alpha));
        return frameBuffer;
    }
};

/** Draws the soft drop shadow behind the deterministic Inspector strip. */
class WebgpuParallaxUvInspectorShadowPass final : public IRenderClass
{
public:
    /** Enables a black alpha shadow over the existing output. */
    constructor(BindGroup<WebgpuParallaxUvOutputResources> resources [[Slot0]])
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
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the shadow coverage evaluation. */
    WebgpuParallaxUvScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuParallaxUvScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Evaluates a small Gaussian-like shadow around the panel bounds. */
    WebgpuParallaxUvOutputFrameBuffer fragment(
        WebgpuParallaxUvScreenOutput inputValue)
    {
        const float enabled = resources->uniforms->outputFlagsAndOffset.x;
        const float2 viewport = resources->uniforms->viewportAndExposure.xy;
        const float2 pixel = float2(
            inputValue.uv.x * viewport.x,
            inputValue.uv.y * viewport.y);
        const float2 center = float2(698.0f, 38.0f);
        const float2 halfExtent = float2(84.5f, 18.0f);
        const float2 relative = pixel - center;
        const float radius = relative.x < 0.0f ? 12.0f : 6.0f;
        const float2 q = abs(relative) - halfExtent + radius;
        const float distanceToBox = length(max(q, float2(0.0f))) +
            min(max(q.x, q.y), 0.0f) - radius;
        const float outsideDistance = max(distanceToBox, 0.0f);
        const float alpha = enabled * 0.18f * exp(
            -outsideDistance * outsideDistance / 50.0f);
        WebgpuParallaxUvOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated parallax scene, immutable assets, and single-sample output. */
class WebgpuParallaxUvRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuParallaxUvVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuParallaxUvUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> topColor;
    Texture<TextureFormat::RGBA8UnormSrgb, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> bottomColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> roughness;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> normal;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> displacement;
    Texture<TextureFormat::RGBA16Float, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> environment;
    Texture<TextureFormat::RGBA16Float, TextureUsage<StorageBinding, TextureBinding, CopySrc>, TextureDimension::e2D> environmentAtlas;
    Texture<TextureFormat::RGBA16Float, TextureUsage<StorageBinding, TextureBinding>, TextureDimension::e2D> environmentPingPongAtlas;
    Texture<TextureFormat::RG16Float, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> dfgLut;
    eastl::array<
        Buffer<WebgpuParallaxUvPmremUniforms, BufferUsage<Uniform, CopyDst>>,
        WebgpuParallaxUvPmremLodCount - 1u> pmremUniformBuffers;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Sampler materialSampler;
    Sampler environmentSampler;
    Sampler pmremSampler;
    Sampler dfgSampler;
    Sampler outputSampler;
    BindGroup<WebgpuParallaxUvResources> resources;
    BindGroup<WebgpuParallaxUvOutputResources> outputResources;
    RenderClass<WebgpuParallaxUvBackgroundPass> backgroundPass;
    RenderClass<WebgpuParallaxUvScenePass> mainPass;
    RenderClass<WebgpuParallaxUvOutputPass> outputPass;
    RenderClass<WebgpuParallaxUvInspectorShadowPass> inspectorShadowPass;
    RenderClass<WebgpuParallaxUvInspectorPass> inspectorPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Initializes immutable samplers and the dedicated two-pass pipeline. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        materialSampler = device->createSampler({
            .label = "WebgpuParallaxUvMaterialSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
        });
        environmentSampler = device->createSampler({
            .label = "WebgpuParallaxUvEnvironmentSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
        });
        pmremSampler = device->createSampler({
            .label = "WebgpuParallaxUvPmremSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
        });
        dfgSampler = device->createSampler({
            .label = "WebgpuParallaxUvDfgSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
        });
        outputSampler = device->createSampler({
            .label = "WebgpuParallaxUvOutputSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
        });
    }

    /** Allocates the ordinary single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture("WebgpuParallaxUvColor", width, height, 1u);
        outputColor = device->createTexture("WebgpuParallaxUvOutput", width, height, 1u);
        sceneDepth = device->createTexture("WebgpuParallaxUvDepth", width, height, 1u);
    }

    /** Uploads locked geometry, maps, HDR pixels, and deterministic controls. */
    void configureScene(
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &normals,
        const eastl::vector<float2> &textureCoordinates,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint8_t> &topPixels,
        const eastl::vector<uint8_t> &bottomPixels,
        const eastl::vector<uint8_t> &roughnessPixels,
        const eastl::vector<uint8_t> &normalPixels,
        const eastl::vector<uint8_t> &displacementPixels,
        uint textureWidth,
        uint textureHeight,
        const eastl::vector<uint16_t> &environmentPixels,
        uint environmentWidth,
        uint environmentHeight,
        const eastl::vector<uint> &dfgLutPackedPixels,
        float4 cameraPositionAndScale,
        float4 cameraRightAndBackgroundBlur,
        float4 cameraUpAndParallaxScale,
        float4 cameraForwardAndTanHalfFov,
        float4 viewportAndExposure,
        float4 outputFlagsAndOffset)
    {
        eastl::vector<WebgpuParallaxUvVertex> vertices;
        vertices.resize(positions.size());
        for (uint index = 0u; index < uint(vertices.size()); ++index)
        {
            vertices[index].position = positions[index];
            vertices[index].normal = normals[index];
            vertices[index].textureCoordinate = textureCoordinates[index];
        }
        indexCount = uint(indices.size());
        WebgpuParallaxUvUniforms uniforms;
        uniforms.cameraPositionAndScale = cameraPositionAndScale;
        uniforms.cameraRightAndBackgroundBlur = cameraRightAndBackgroundBlur;
        uniforms.cameraUpAndParallaxScale = cameraUpAndParallaxScale;
        uniforms.cameraForwardAndTanHalfFov = cameraForwardAndTanHalfFov;
        uniforms.viewportAndExposure = viewportAndExposure;
        uniforms.outputFlagsAndOffset = outputFlagsAndOffset;
        vertexBuffer = device->createBuffer("WebgpuParallaxUvVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer("WebgpuParallaxUvIndices", indexCount);
        uniformBuffer = device->createBuffer("WebgpuParallaxUvUniforms", 1u);
        topColor = device->createTexture("WebgpuParallaxUvTop", textureWidth, textureHeight, 1u);
        bottomColor = device->createTexture("WebgpuParallaxUvBottom", textureWidth, textureHeight, 1u);
        roughness = device->createTexture("WebgpuParallaxUvRoughness", textureWidth, textureHeight, 1u);
        normal = device->createTexture("WebgpuParallaxUvNormal", textureWidth, textureHeight, 1u);
        displacement = device->createTexture("WebgpuParallaxUvDisplacement", textureWidth, textureHeight, 1u);
        environment = device->createTexture("WebgpuParallaxUvEnvironment", environmentWidth, environmentHeight, 1u);
        environmentAtlas = device->createTexture(
            "WebgpuParallaxUvPmremAtlas",
            WebgpuParallaxUvPmremAtlasWidth,
            WebgpuParallaxUvPmremAtlasHeight,
            1u);
        environmentPingPongAtlas = device->createTexture(
            "WebgpuParallaxUvPmremPingPongAtlas",
            WebgpuParallaxUvPmremAtlasWidth,
            WebgpuParallaxUvPmremAtlasHeight,
            1u);
        dfgLut = device->createTexture("WebgpuParallaxUvDfgLut", 16u, 16u, 1u);
        for (uint index = 0u;
             index < WebgpuParallaxUvPmremLodCount - 1u;
             ++index)
        {
            pmremUniformBuffers[index] = device->createBuffer(
                "WebgpuParallaxUvPmremUniforms", 1u);
        }
        graphicsQueue->writeBuffer(BufferRange(vertexBuffer), vertices.data(), uint64_t(vertices.size()) * sizeof(WebgpuParallaxUvVertex))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->writeTexture(topColor, topPixels.data(), topPixels.size(), 0u)
            ->writeTexture(bottomColor, bottomPixels.data(), bottomPixels.size(), 0u)
            ->writeTexture(roughness, roughnessPixels.data(), roughnessPixels.size(), 0u)
            ->writeTexture(normal, normalPixels.data(), normalPixels.size(), 0u)
            ->writeTexture(displacement, displacementPixels.data(), displacementPixels.size(), 0u)
            ->writeTexture(environment, environmentPixels.data(), uint64_t(environmentPixels.size()) * sizeof(uint16_t), 0u)
            ->writeTexture(dfgLut, dfgLutPackedPixels.data(),
                uint64_t(dfgLutPackedPixels.size()) * sizeof(uint), 0u)
            ->submit();
        auto pmremSourceResources = device->createBindGroup<
            WebgpuParallaxUvPmremSourceResources>(
                environment->createView(),
                environmentSampler,
                environmentAtlas->createView());
        auto pmremSourcePass = device->createComputeClass<
            WebgpuParallaxUvPmremSourcePass>(pmremSourceResources);
        graphicsQueue
            ->computePass(
                "WebgpuParallaxUvPmremLevelZero",
                pmremSourcePass(
                    WebgpuParallaxUvPmremAtlasWidth,
                    WebgpuParallaxUvPmremCubeSize * 2u,
                    1u))
            ->submit();
        eastl::vector<uint16_t> pmremLevelZeroReadback(
            size_t(WebgpuParallaxUvPmremAtlasWidth) *
            WebgpuParallaxUvPmremAtlasHeight * 4u);
        graphicsQueue
            ->readTexture(
                environmentAtlas,
                pmremLevelZeroReadback.data(),
                uint64_t(pmremLevelZeroReadback.size()) * sizeof(uint16_t),
                0u)
            ->submit();
        for (uint lodIndex = 1u;
             lodIndex < WebgpuParallaxUvPmremLodCount;
             ++lodIndex)
        {
            const uint mipExponent = lodIndex <= 4u ? 8u - lodIndex : 4u;
            const uint faceSize = 1u << mipExponent;
            const uint outputX = lodIndex > 4u
                ? 3u * faceSize * (lodIndex - 4u)
                : 0u;
            const uint outputY = 4u *
                (WebgpuParallaxUvPmremCubeSize - faceSize);
            WebgpuParallaxUvPmremUniforms pmremUniforms;
            pmremUniforms.outputOffsetSize = uint4(
                outputX, outputY, faceSize * 3u, faceSize * 2u);
            pmremUniforms.roughnessSourceMipAndReserved = float4(
                float(lodIndex) /
                    float(WebgpuParallaxUvPmremLodCount - 1u),
                9.0f - float(lodIndex),
                0.0f,
                0.0f);
            auto pmremFilterResources = device->createBindGroup<
                WebgpuParallaxUvPmremFilterResources>(
                    pmremUniformBuffers[lodIndex - 1u],
                    environmentAtlas->createView(),
                    pmremSampler,
                    environmentPingPongAtlas->createView());
            auto pmremFilterPass = device->createComputeClass<
                WebgpuParallaxUvPmremFilterPass>(
                    pmremFilterResources);
            auto pmremCopyResources = device->createBindGroup<
                WebgpuParallaxUvPmremCopyResources>(
                    pmremUniformBuffers[lodIndex - 1u],
                    environmentPingPongAtlas->createView(),
                    environmentAtlas->createView());
            auto pmremCopyPass = device->createComputeClass<
                WebgpuParallaxUvPmremCopyPass>(
                    pmremCopyResources);
            graphicsQueue
                ->writeBuffer(
                    BufferRange(pmremUniformBuffers[lodIndex - 1u]),
                    &pmremUniforms,
                    sizeof(pmremUniforms))
                ->computePass(
                    "WebgpuParallaxUvPmremFilter",
                    pmremFilterPass(
                        faceSize * 3u, faceSize * 2u, 1u))
                ->computePass(
                    "WebgpuParallaxUvPmremCopy",
                    pmremCopyPass(
                        faceSize * 3u, faceSize * 2u, 1u));
        }
        graphicsQueue->submit();
        resources = device->createBindGroup<WebgpuParallaxUvResources>(
            uniformBuffer,
            topColor->createView(), bottomColor->createView(),
            roughness->createView(), normal->createView(),
            displacement->createView(),
            environmentAtlas->createView(), dfgLut->createView(),
            materialSampler, pmremSampler, dfgSampler);
        outputResources = device->createBindGroup<WebgpuParallaxUvOutputResources>(
            sceneColor->createView(), outputSampler, uniformBuffer);
        backgroundPass = device->createRenderClass<WebgpuParallaxUvBackgroundPass>(resources);
        mainPass = device->createRenderClass<WebgpuParallaxUvScenePass>(resources);
        outputPass = device->createRenderClass<WebgpuParallaxUvOutputPass>(
            outputResources);
        inspectorShadowPass = device->createRenderClass<
            WebgpuParallaxUvInspectorShadowPass>(outputResources);
        inspectorPass = device->createRenderClass<WebgpuParallaxUvInspectorPass>(
            outputResources);
    }

    /** Draws the environment and sole indexed ground object in DSL. */
    void render() override
    {
        WebgpuParallaxUvLinearFrameBuffer backgroundFrame;
        backgroundFrame.color = sceneColor->createView();
        backgroundFrame.color.loadOp = LoadOp::Clear;
        backgroundFrame.color.storeOp = StoreOp::Store;
        backgroundFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrame.depth = sceneDepth->createView();
        backgroundFrame.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrame.depth.depthStoreOp = StoreOp::Store;
        backgroundFrame.depth.depthClearValue = 1.0f;
        WebgpuParallaxUvLinearFrameBuffer mainFrame = backgroundFrame;
        mainFrame.color.loadOp = LoadOp::Load;
        mainFrame.depth.depthLoadOp = LoadOp::Load;
        WebgpuParallaxUvOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuParallaxUvOutputFrameBuffer inspectorFrame = outputFrame;
        inspectorFrame.color.loadOp = LoadOp::Load;
        WebgpuParallaxUvOutputFrameBuffer shadowFrame = inspectorFrame;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebgpuParallaxUvBackground", backgroundFrame, backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuParallaxUvMain", mainFrame,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebgpuParallaxUvOutput",
                outputFrame,
                outputPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuParallaxUvInspectorShadow",
                shadowFrame,
                inspectorShadowPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuParallaxUvInspector",
                inspectorFrame,
                inspectorPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(swapchainTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 texture. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases all dedicated parallax resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(topColor);
        device->freeTexture(bottomColor);
        device->freeTexture(roughness);
        device->freeTexture(normal);
        device->freeTexture(displacement);
        device->freeTexture(environment);
        device->freeTexture(environmentAtlas);
        device->freeTexture(environmentPingPongAtlas);
        device->freeTexture(dfgLut);
        device->freeTexture(sceneColor);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
        for (uint index = 0u;
             index < WebgpuParallaxUvPmremLodCount - 1u;
             ++index)
        {
            device->freeBuffer(pmremUniformBuffers[index]);
        }
    }
};

#endif
