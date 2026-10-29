#ifndef GVM_THREE_WEBGPU_MATERIALS_DISPLACEMENTMAP_HPP
#define GVM_THREE_WEBGPU_MATERIALS_DISPLACEMENTMAP_HPP

#include "UGL.h"
#include "WebgpuMaterialsDisplacementmapData.hpp"

#include <EASTL/array.h>
#include <EASTL/vector.h>

using namespace UGL;

static const uint WebgpuMaterialsDisplacementmapPmremCubeSize = 512u;
static const uint WebgpuMaterialsDisplacementmapPmremAtlasWidth = 1536u;
static const uint WebgpuMaterialsDisplacementmapPmremAtlasHeight = 2048u;
static const uint WebgpuMaterialsDisplacementmapPmremLodCount = 12u;
// r185's common WebGPU PMREM generator uses 512 Hammersley samples.  Keep
// this private filter aligned with the WebGPU path; the sample count is not a
// public DSL or RHI capability.
static const uint WebgpuMaterialsDisplacementmapPmremSampleCount = 512u;

/** Stores one ordered r185 WebGPU PMREM incremental-filter dispatch configuration. */
struct WebgpuMaterialsDisplacementmapPmremUniforms
{
    uint4 outputOffsetSize;
    float4 roughnessSourceMipAndReserved;
};

/** Binds the ninja maps, filtered environment atlas, DFG LUT, and frame state. */
struct WebgpuMaterialsDisplacementmapResources final : public IBindGroup
{
    /** Declares every texture consumed by the dedicated physical material. */
    constructor(
        UniformBuffer<WebgpuMaterialsDisplacementmapUniforms> uniforms [[Binding0]],
        Texture2D<float4> normalMap [[Binding1]],
        Texture2D<float4> aoMap [[Binding2]],
        Texture2D<float4> displacementMap [[Binding3]],
        Texture2D<half4> environmentAtlas [[Binding4]],
        Texture2D<half4> dfgLut [[Binding5]],
        Sampler materialSampler [[Binding6]],
        Sampler environmentSampler [[Binding7]],
        Sampler dfgSampler [[Binding8]])
    {
    }
};

/** Binds all six authored cube faces to the level-zero PMREM conversion pass. */
struct WebgpuMaterialsDisplacementmapPmremSourceResources final : public IBindGroup
{
    /** Declares the locked sRGB cube faces, source sampler, and linear atlas. */
    constructor(
        Texture2D<float4> positiveX [[Binding0]],
        Texture2D<float4> negativeX [[Binding1]],
        Texture2D<float4> positiveY [[Binding2]],
        Texture2D<float4> negativeY [[Binding3]],
        Texture2D<float4> positiveZ [[Binding4]],
        Texture2D<float4> negativeZ [[Binding5]],
        Sampler sourceSampler [[Binding6]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding7]])
    {
    }
};

/** Binds one immutable PMREM level and separate source and destination atlases. */
struct WebgpuMaterialsDisplacementmapPmremFilterResources final : public IBindGroup
{
    /** Declares the level state and non-aliasing filter resources. */
    constructor(
        UniformBuffer<WebgpuMaterialsDisplacementmapPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        Sampler atlasSampler [[Binding2]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding3]])
    {
    }
};

/** Binds one completed PMREM level for copying back into the main atlas. */
struct WebgpuMaterialsDisplacementmapPmremCopyResources final : public IBindGroup
{
    /** Declares the active level and non-overlapping copy resources. */
    constructor(
        UniformBuffer<WebgpuMaterialsDisplacementmapPmremUniforms> uniforms [[Binding0]],
        Texture2D<half4> sourceAtlas [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destinationAtlas [[Binding2]])
    {
    }
};

/** Carries displaced view-space geometry to the physical fragment stage. */
struct WebgpuMaterialsDisplacementmapVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Defines the single-sample output and shared depth attachment. */
struct WebgpuMaterialsDisplacementmapFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only target used by the screen-space Inspector overlay. */
struct WebgpuMaterialsDisplacementmapOverlayFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen coordinates into the minimized Inspector overlay pass. */
struct WebgpuMaterialsDisplacementmapOverlayVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts an authored sRGB channel into linear working space. */
float webgpuMaterialsDisplacementmapSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Encodes one linear-light channel for the browser canvas. */
float webgpuMaterialsDisplacementmapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Samples one authored Swedish Royal Castle cube face in linear working space. */
float3 webgpuMaterialsDisplacementmapSampleCubeSource(
    IN BindGroup<WebgpuMaterialsDisplacementmapPmremSourceResources> resources,
    float3 direction,
    float lod)
{
    const float3 absoluteDirection = abs(direction);
    float2 uv = float2(0.0f);
    float4 encoded = float4(0.0f);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            if (direction.x > 0.0f)
            {
                uv = float2(direction.z, -direction.y) /
                    absoluteDirection.x * 0.5f + 0.5f;
                encoded = resources->negativeX->sampleLevel(
                    resources->sourceSampler, uv, lod);
            }
            else
            {
                uv = float2(-direction.z, -direction.y) /
                    absoluteDirection.x * 0.5f + 0.5f;
                encoded = resources->positiveX->sampleLevel(
                    resources->sourceSampler, uv, lod);
            }
        }
        else if (direction.y > 0.0f)
        {
            uv = float2(-direction.x, direction.z) /
                absoluteDirection.y * 0.5f + 0.5f;
            encoded = resources->negativeY->sampleLevel(
                resources->sourceSampler, uv, lod);
        }
        else
        {
            uv = float2(-direction.x, -direction.z) /
                absoluteDirection.y * 0.5f + 0.5f;
            encoded = resources->positiveY->sampleLevel(
                resources->sourceSampler, uv, lod);
        }
    }
    else if (absoluteDirection.z > absoluteDirection.y)
    {
        if (direction.z > 0.0f)
        {
            uv = float2(-direction.x, -direction.y) /
                absoluteDirection.z * 0.5f + 0.5f;
            encoded = resources->positiveZ->sampleLevel(
                resources->sourceSampler, uv, lod);
        }
        else
        {
            uv = float2(direction.x, -direction.y) /
                absoluteDirection.z * 0.5f + 0.5f;
            encoded = resources->negativeZ->sampleLevel(
                resources->sourceSampler, uv, lod);
        }
    }
    else if (direction.y > 0.0f)
    {
        uv = float2(-direction.x, direction.z) /
            absoluteDirection.y * 0.5f + 0.5f;
        encoded = resources->negativeY->sampleLevel(
            resources->sourceSampler, uv, lod);
    }
    else
    {
        uv = float2(-direction.x, -direction.z) /
            absoluteDirection.y * 0.5f + 0.5f;
        encoded = resources->positiveY->sampleLevel(
            resources->sourceSampler, uv, lod);
    }
    return float3(
        webgpuMaterialsDisplacementmapSrgbToLinear(encoded.r),
        webgpuMaterialsDisplacementmapSrgbToLinear(encoded.g),
        webgpuMaterialsDisplacementmapSrgbToLinear(encoded.b));
}

/** Reverses one unsigned bit pattern into a Van der Corput sample. */
float webgpuMaterialsDisplacementmapRadicalInverse(uint bits)
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
uint webgpuMaterialsDisplacementmapPmremFace(float3 direction)
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
float2 webgpuMaterialsDisplacementmapPmremFaceUv(float3 direction, uint face)
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
float3 webgpuMaterialsDisplacementmapPmremDirection(float2 uv, uint face)
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
float2 webgpuMaterialsDisplacementmapPmremAtlasUv(float3 direction, float sourceMip)
{
    uint face = webgpuMaterialsDisplacementmapPmremFace(direction);
    const float filterIndex = max(4.0f - sourceMip, 0.0f);
    const float storedMip = max(sourceMip, 4.0f);
    const float faceSize = exp2(storedMip);
    float2 uv = webgpuMaterialsDisplacementmapPmremFaceUv(direction, face) *
        (faceSize - 2.0f) + 1.0f;
    if (face > 2u)
    {
        uv.y += faceSize;
        face -= 3u;
    }
    uv.x += float(face) * faceSize;
    uv.x += filterIndex * 48.0f;
    uv.y += 4.0f *
        (float(WebgpuMaterialsDisplacementmapPmremCubeSize) - faceSize);
    return uv / float2(
        float(WebgpuMaterialsDisplacementmapPmremAtlasWidth),
        float(WebgpuMaterialsDisplacementmapPmremAtlasHeight));
}

/** Converts Standard roughness into Three r185's nonlinear virtual PMREM mip. */
float webgpuMaterialsDisplacementmapRoughnessToMip(float roughness)
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
float3 webgpuMaterialsDisplacementmapSampleEnvironment(
    IN BindGroup<WebgpuMaterialsDisplacementmapResources> resources,
    float3 direction,
    float roughness)
{
    direction = float3(direction.x, -direction.y, direction.z);
    const float mip = clamp(
        webgpuMaterialsDisplacementmapRoughnessToMip(roughness), -2.0f, 9.0f);
    const float lowerMip = floor(mip);
    const float interpolation = mip - lowerMip;
    const float3 lowerColor = float4(resources->environmentAtlas->sample(
        resources->environmentSampler,
        webgpuMaterialsDisplacementmapPmremAtlasUv(direction, lowerMip))).xyz;
    if (interpolation == 0.0f) return lowerColor;
    const float3 upperColor = float4(resources->environmentAtlas->sample(
        resources->environmentSampler,
        webgpuMaterialsDisplacementmapPmremAtlasUv(direction, lowerMip + 1.0f))).xyz;
    return lerp(lowerColor, upperColor, interpolation);
}

/** Evaluates Three's exponential Schlick approximation. */
float3 webgpuMaterialsDisplacementmapFresnel(
    float3 f0,
    float dotVH)
{
    const float factor = exp2((-5.55473f * dotVH - 6.98316f) * dotVH);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates one GGX direct-light contribution. */
float3 webgpuMaterialsDisplacementmapDirectLight(
    IN BindGroup<WebgpuMaterialsDisplacementmapResources> resources,
    float3 lightDirection,
    float3 lightColor,
    float3 normal,
    float3 viewDirection,
    float3 diffuseColor,
    float3 f0,
    float roughness)
{
    const float dotNL = max(dot(normal, lightDirection), 0.0f);
    const float dotNV = max(dot(normal, viewDirection), 0.0001f);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNH = max(dot(normal, halfDirection), 0.0f);
    const float dotVH = max(dot(viewDirection, halfDirection), 0.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator =
        dotNH * dotNH * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared /
        max(3.141592653589793f * denominator * denominator, 0.000001f);
    const float visibility = 0.5f /
        max(
            dotNL * sqrt(
                alphaSquared +
                (1.0f - alphaSquared) * dotNV * dotNV) +
            dotNV * sqrt(
                alphaSquared +
                (1.0f - alphaSquared) * dotNL * dotNL),
            0.0001f);
    const float3 specular =
        webgpuMaterialsDisplacementmapFresnel(f0, dotVH) *
        distribution * visibility;
    const float2 dfgView = float4(resources->dfgLut->sample(
        resources->dfgSampler, float2(roughness, dotNV))).xy;
    const float2 dfgLight = float4(resources->dfgLut->sample(
        resources->dfgSampler, float2(roughness, dotNL))).xy;
    const float3 viewEnergy = f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy = f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissing = 1.0f - dfgView.x - dfgView.y;
    const float lightMissing = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multipleScattering =
        viewEnergy * lightEnergy * averageFresnel /
        (float3(1.0f) -
             viewMissing * lightMissing *
                 averageFresnel * averageFresnel +
         float3(0.000001f)) *
        viewMissing * lightMissing;
    return lightColor * dotNL *
        (diffuseColor * 0.3183098861837907f +
         specular + multipleScattering);
}

/** Transforms and texture-displaces one ninja-head vertex. */
WebgpuMaterialsDisplacementmapVertexOutput
webgpuMaterialsDisplacementmapVertex(
    IN BindGroup<WebgpuMaterialsDisplacementmapResources> resources,
    WebgpuMaterialsDisplacementmapVertex inputValue)
{
    const float displacement = resources->displacementMap->sampleLevel(
        resources->materialSampler,
        inputValue.textureCoordinate,
        0.0f).r;
    const float scale = resources->uniforms->materialState1.x;
    const float3 displaced = inputValue.position + inputValue.normal *
        (displacement * scale - 0.428408f) * 25.0f;
    const float4 localPosition = float4(displaced, 1.0f);
    const float4 viewPosition = mul(
        resources->uniforms->modelView, localPosition);
    WebgpuMaterialsDisplacementmapVertexOutput outputValue;
    outputValue.position = mul(
        resources->uniforms->modelViewProjection, localPosition);
    outputValue.position.y = -outputValue.position.y;
    outputValue.position.z =
        (outputValue.position.z + outputValue.position.w) * 0.5f;
    outputValue.viewPosition = viewPosition.xyz;
    outputValue.viewNormal = normalize(float3(mul(
        resources->uniforms->normalTransform,
        float4(inputValue.normal, 0.0f)).xyz));
    outputValue.textureCoordinate = inputValue.textureCoordinate;
    return outputValue;
}

/** Shades one face orientation with normal, AO, direct lights, and cube IBL. */
float4
webgpuMaterialsDisplacementmapFragment(
    IN BindGroup<WebgpuMaterialsDisplacementmapResources> resources,
    WebgpuMaterialsDisplacementmapVertexOutput inputValue,
    float faceDirection)
{
    const float2 uv = inputValue.textureCoordinate;
    const float3 positionDx = ddx(inputValue.viewPosition);
    const float3 positionDy = -ddy(inputValue.viewPosition);
    const float2 uvDx = ddx(uv);
    const float2 uvDy = -ddy(uv);
    const float3 baseNormal = normalize(inputValue.viewNormal);
    const float3 orientedBaseNormal = baseNormal * faceDirection;
    const float3 r1 = cross(positionDy, orientedBaseNormal);
    const float3 r2 = cross(orientedBaseNormal, positionDx);
    const float3 tangentUnscaled =
        r1 * uvDx.x + r2 * uvDy.x;
    const float3 bitangentUnscaled =
        r1 * uvDx.y + r2 * uvDy.y;
    const float tangentScale = 1.0f / sqrt(max(
        max(dot(tangentUnscaled, tangentUnscaled),
            dot(bitangentUnscaled, bitangentUnscaled)),
        0.000001f));
    const float3 tangent =
        tangentUnscaled * tangentScale * faceDirection;
    const float3 bitangent =
        bitangentUnscaled * tangentScale * faceDirection;
    float3 mappedNormal = resources->normalMap->sample(
        resources->materialSampler, uv).xyz * 2.0f - float3(1.0f);
    mappedNormal.xy *= resources->uniforms->materialState1.y;
    float3 normal = normalize(
        tangent * mappedNormal.x - bitangent * mappedNormal.y +
        orientedBaseNormal * mappedNormal.z);
    const float3 normalDerivative = max(
        abs(ddx(baseNormal)), abs(-ddy(baseNormal)));
    const float geometryRoughness = max(
        max(normalDerivative.x, normalDerivative.y), normalDerivative.z);
    const float roughness = clamp(
        max(resources->uniforms->materialState0.y, 0.0525f) +
            geometryRoughness,
        0.0525f,
        1.0f);
    const float metalness = saturate(resources->uniforms->materialState0.x);
    const float baseChannel =
        webgpuMaterialsDisplacementmapSrgbToLinear(193.0f / 255.0f);
    const float3 baseColor = float3(baseChannel);
    const float3 diffuseColor = baseColor * (1.0f - metalness);
    const float3 f0 = lerp(float3(0.04f), baseColor, metalness);
    const float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
    float3 color = diffuseColor *
        resources->uniforms->materialState0.z * 0.3183098861837907f;
    const float3 redVector =
        resources->uniforms->redLightPositionAndIntensity.xyz -
        inputValue.viewPosition;
    const float3 cameraVector =
        resources->uniforms->cameraLightPositionAndIntensity.xyz -
        inputValue.viewPosition;
    const float3 blueVector =
        resources->uniforms->blueLightPositionAndIntensity.xyz -
        inputValue.viewPosition;
    color += webgpuMaterialsDisplacementmapDirectLight(
        resources,
        normalize(redVector),
        float3(resources->uniforms->redLightPositionAndIntensity.w, 0.0f, 0.0f),
        normal, viewDirection, diffuseColor, f0, roughness);
    color += webgpuMaterialsDisplacementmapDirectLight(
        resources,
        normalize(cameraVector),
        float3(1.0f, 0.13286832f, 0.13286832f) *
            resources->uniforms->cameraLightPositionAndIntensity.w,
        normal, viewDirection, diffuseColor, f0, roughness);
    color += webgpuMaterialsDisplacementmapDirectLight(
        resources,
        normalize(blueVector),
        float3(0.0f, 0.0f, resources->uniforms->blueLightPositionAndIntensity.w),
        normal, viewDirection, diffuseColor, f0, roughness);
    float3 reflection = reflect(-viewDirection, normal);
    const float normalMixWeight =
        roughness * roughness * roughness * roughness;
    reflection = normalize(
        reflection * (1.0f - normalMixWeight) +
        normal * normalMixWeight);
    const float ao = lerp(
        1.0f,
        resources->aoMap->sample(resources->materialSampler, uv).r,
        resources->uniforms->materialState0.w);
    const float dotNV = saturate(dot(normal, viewDirection));
    const float specularOcclusion = clamp(
        ao + pow(dotNV + ao, exp2(-1.0f - 16.0f * roughness)) - 1.0f,
        0.0f,
        1.0f);
    const float2 dfg = float4(resources->dfgLut->sample(
        resources->dfgSampler, float2(roughness, dotNV))).xy;
    const float3 singleScatter = f0 * dfg.x + float3(dfg.y);
    const float ess = dfg.x + dfg.y;
    const float ems = 1.0f - ess;
    const float3 averageFresnel =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multiScatter =
        singleScatter * averageFresnel /
        (float3(1.0f) - ems * averageFresnel + float3(0.000001f)) *
        ems;
    const float3 environmentRadiance =
        webgpuMaterialsDisplacementmapSampleEnvironment(
            resources, reflection, roughness);
    const float3 environmentIrradiance =
        webgpuMaterialsDisplacementmapSampleEnvironment(
            resources, normal, 1.0f);
    color += (environmentRadiance * singleScatter +
              environmentIrradiance * multiScatter) *
        resources->uniforms->materialState1.z * specularOcclusion;
    return float4(
        webgpuMaterialsDisplacementmapLinearToSrgb(color.r),
        webgpuMaterialsDisplacementmapLinearToSrgb(color.g),
        webgpuMaterialsDisplacementmapLinearToSrgb(color.b),
        1.0f);
}

/** Converts the six authored cube faces into the padded level-zero cubeUV layout. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebgpuMaterialsDisplacementmapPmremSourcePass final : public IComputeClass
{
public:
    /** Binds the immutable source faces and writable main PMREM atlas. */
    constructor(
        BindGroup<WebgpuMaterialsDisplacementmapPmremSourceResources> sourceResources [[Slot0]])
    {
    }

private:
    /** Writes one padded level-zero cubeUV texel in linear working space. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebgpuMaterialsDisplacementmapPmremAtlasWidth ||
            threadID.y >= WebgpuMaterialsDisplacementmapPmremCubeSize * 2u)
            return;
        const uint faceColumn =
            threadID.x / WebgpuMaterialsDisplacementmapPmremCubeSize;
        const uint faceRow =
            threadID.y / WebgpuMaterialsDisplacementmapPmremCubeSize;
        const uint face = faceColumn + faceRow * 3u;
        const uint2 localCoordinate = uint2(
            threadID.x - faceColumn * WebgpuMaterialsDisplacementmapPmremCubeSize,
            threadID.y - faceRow * WebgpuMaterialsDisplacementmapPmremCubeSize);
        const float2 faceUv = (float2(localCoordinate) - 0.5f) /
            float(WebgpuMaterialsDisplacementmapPmremCubeSize - 2u);
        const float3 direction = normalize(
            webgpuMaterialsDisplacementmapPmremDirection(faceUv, face));
        const float3 color = webgpuMaterialsDisplacementmapSampleCubeSource(
            sourceResources, direction, 0.0f);
        sourceResources->destinationAtlas->write(
            threadID.xy, half4(float4(color, 1.0f)));
    }
};

/** Applies one ordered r185 WebGPU incremental GGX VNDF PMREM filter level. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebgpuMaterialsDisplacementmapPmremFilterPass final : public IComputeClass
{
public:
    /** Binds one level state and separate main-to-ping-pong atlas resources. */
    constructor(
        BindGroup<WebgpuMaterialsDisplacementmapPmremFilterResources> filterResources [[Slot0]])
    {
    }

private:
    /** Evaluates exactly 256 deterministic r185 WebGPU VNDF samples per texel. */
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
            webgpuMaterialsDisplacementmapPmremDirection(faceUv, face));
        const float targetRoughness =
            filterResources->uniforms->roughnessSourceMipAndReserved.x;
        const float sourceRoughness = targetRoughness -
            1.0f / float(WebgpuMaterialsDisplacementmapPmremLodCount - 1u);
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
                webgpuMaterialsDisplacementmapPmremAtlasUv(
                    normal, sourceMip), 0.0f)).xyz;
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
                 sampleIndex < WebgpuMaterialsDisplacementmapPmremSampleCount;
                 ++sampleIndex)
            {
                const float2 xi = float2(
                    float(sampleIndex) /
                        float(WebgpuMaterialsDisplacementmapPmremSampleCount),
                    webgpuMaterialsDisplacementmapRadicalInverse(sampleIndex));
                const float diskRadius = sqrt(xi.x);
                const float azimuth = 6.283185307179586f * xi.y;
                const float tangentX = diskRadius * cos(azimuth);
                const float tangentY = diskRadius * sin(azimuth);
                const float tangentZ = sqrt(max(
                    0.0f, 1.0f - tangentX * tangentX - tangentY * tangentY));
                const float3 halfVector = normalize(float3(
                    alpha * tangentX, alpha * tangentY, tangentZ));
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
                    filteredColor += float4(
                        filterResources->sourceAtlas->sampleLevel(
                            filterResources->atlasSampler,
                            webgpuMaterialsDisplacementmapPmremAtlasUv(
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
    WebgpuMaterialsDisplacementmapPmremCopyPass final : public IComputeClass
{
public:
    /** Binds the active level rectangle and non-overlapping atlas resources. */
    constructor(
        BindGroup<WebgpuMaterialsDisplacementmapPmremCopyResources> copyResources [[Slot0]])
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

/** Composites the deterministic minimized r185 Inspector bar over the Scene. */
class WebgpuMaterialsDisplacementmapOverlayPass final : public IRenderClass
{
public:
    /** Binds frame state and configures conventional source-alpha blending. */
    constructor(
        BindGroup<WebgpuMaterialsDisplacementmapResources> resources [[Slot0]])
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
    /** Emits one fullscreen triangle for the screen-only overlay. */
    WebgpuMaterialsDisplacementmapOverlayVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuMaterialsDisplacementmapOverlayVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the rounded bar, border, split fill, and soft shadow. */
    WebgpuMaterialsDisplacementmapOverlayFrameBuffer fragment(
        WebgpuMaterialsDisplacementmapOverlayVertexOutput inputValue)
    {
        if (resources->uniforms->viewportAndUi.z < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel = float2(
            inputValue.uv.x * resources->uniforms->viewportAndUi.x,
            inputValue.uv.y * resources->uniforms->viewportAndUi.y);
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float cornerRadius = pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta = abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) - cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowCenter = float2(699.5f, 37.5f);
            const float2 shadowDelta = abs(pixel - shadowCenter) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) -
                cornerRadius;
            const float shadowAlpha = 0.13f * exp(
                -max(shadowDistance, 0.0f) *
                 max(shadowDistance, 0.0f) / 72.0f);
            if (shadowAlpha < 0.004f)
            {
                discard_fragment();
            }
            WebgpuMaterialsDisplacementmapOverlayFrameBuffer shadow;
            shadow.color = half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadow;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (pixel.x < 663.0f)
        {
            color = float3(23.1818f, 61.8182f, 85.7727f) / 255.0f;
            alpha = 0.88f;
        }
        if (roundedDistance > -1.0f)
        {
            color = float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuMaterialsDisplacementmapOverlayFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Draws back-facing ninja geometry before the front-facing pass. */
class WebgpuMaterialsDisplacementmapBackPass final : public IRenderClass
{
public:
    /** Configures the back-face half of Three's DoubleSide material. */
    constructor(BindGroup<WebgpuMaterialsDisplacementmapResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies texture displacement through the shared material resources. */
    WebgpuMaterialsDisplacementmapVertexOutput vertex(
        WebgpuMaterialsDisplacementmapVertex inputValue [[VertexInput0]])
    {
        return webgpuMaterialsDisplacementmapVertex(resources, inputValue);
    }

    /** Evaluates physical shading with the back-face normal orientation. */
    WebgpuMaterialsDisplacementmapFrameBuffer fragment(
        WebgpuMaterialsDisplacementmapVertexOutput inputValue)
    {
        const float4 color = webgpuMaterialsDisplacementmapFragment(
            resources, inputValue, -1.0f);
        WebgpuMaterialsDisplacementmapFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Draws front-facing ninja geometry over the completed back-face pass. */
class WebgpuMaterialsDisplacementmapFrontPass final : public IRenderClass
{
public:
    /** Configures the front-face half of Three's DoubleSide material. */
    constructor(BindGroup<WebgpuMaterialsDisplacementmapResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies texture displacement through the shared material resources. */
    WebgpuMaterialsDisplacementmapVertexOutput vertex(
        WebgpuMaterialsDisplacementmapVertex inputValue [[VertexInput0]])
    {
        return webgpuMaterialsDisplacementmapVertex(resources, inputValue);
    }

    /** Evaluates physical shading with the front-face normal orientation. */
    WebgpuMaterialsDisplacementmapFrameBuffer fragment(
        WebgpuMaterialsDisplacementmapVertexOutput inputValue)
    {
        const float4 color = webgpuMaterialsDisplacementmapFragment(
            resources, inputValue, 1.0f);
        WebgpuMaterialsDisplacementmapFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the dedicated OBJ geometry, material maps, and two Scene passes. */
class WebgpuMaterialsDisplacementmapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuMaterialsDisplacementmapVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuMaterialsDisplacementmapUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> normalTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> aoTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> displacementTexture;
    eastl::array<
        Texture<TextureFormat::RGBA8Unorm,
                TextureUsage<TextureBinding, CopyDst>,
                TextureDimension::e2D>,
        6u> cubeTextures;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> environmentAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> environmentPingPongAtlas;
    Texture<TextureFormat::RG16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> dfgLutTexture;
    eastl::array<
        Buffer<WebgpuMaterialsDisplacementmapPmremUniforms,
               BufferUsage<Uniform, CopyDst>>,
        WebgpuMaterialsDisplacementmapPmremLodCount - 1u> pmremUniformBuffers;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    Sampler materialSampler;
    Sampler environmentSampler;
    Sampler dfgSampler;
    BindGroup<WebgpuMaterialsDisplacementmapResources> resources;
    RenderClass<WebgpuMaterialsDisplacementmapBackPass> backPass;
    RenderClass<WebgpuMaterialsDisplacementmapFrontPass> frontPass;
    RenderClass<WebgpuMaterialsDisplacementmapOverlayPass> overlayPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one immutable trilinear material sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        materialSampler = device->createSampler({
            .label = "WebgpuMaterialsDisplacementmapSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
        environmentSampler = device->createSampler({
            .label = "WebgpuMaterialsDisplacementmapEnvironmentSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
        dfgSampler = device->createSampler({
            .label = "WebgpuMaterialsDisplacementmapDfgSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
    }

    /** Allocates fixed single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapDepth", width, height, 1u);
    }

    /** Uploads the locked OBJ, all explicit mip chains, and target-frame state. */
    void configureScene(
        const eastl::vector<WebgpuMaterialsDisplacementmapVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &normalMips,
        const eastl::vector<eastl::vector<uint8_t>> &aoMips,
        const eastl::vector<eastl::vector<uint8_t>> &displacementMips,
        const eastl::array<eastl::vector<eastl::vector<uint8_t>>, 6u> &cubeMips,
        const eastl::vector<uint> &dfgLutPackedPixels,
        WebgpuMaterialsDisplacementmapUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer(
            "WebgpuMaterialsDisplacementmapVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebgpuMaterialsDisplacementmapIndices", indexCount);
        uniformBuffer = device->createBuffer(
            "WebgpuMaterialsDisplacementmapUniforms", 1u);
        normalTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapNormal", 1024u, 1024u, 1u,
            uint(normalMips.size()));
        aoTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapAo", 1024u, 1024u, 1u,
            uint(aoMips.size()));
        displacementTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapDisplacement", 1024u, 1024u, 1u,
            uint(displacementMips.size()));
        for (uint face = 0u; face < 6u; ++face)
        {
            cubeTextures[face] = device->createTexture(
                "WebgpuMaterialsDisplacementmapCubeFace", 512u, 512u, 1u,
                uint(cubeMips[face].size()));
        }
        environmentAtlas = device->createTexture(
            "WebgpuMaterialsDisplacementmapPmremAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        environmentPingPongAtlas = device->createTexture(
            "WebgpuMaterialsDisplacementmapPmremPingPongAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        dfgLutTexture = device->createTexture(
            "WebgpuMaterialsDisplacementmapDfgLut", 16u, 16u, 1u);
        for (uint index = 0u;
             index < WebgpuMaterialsDisplacementmapPmremLodCount - 1u;
             ++index)
        {
            pmremUniformBuffers[index] = device->createBuffer(
                "WebgpuMaterialsDisplacementmapPmremUniforms", 1u);
        }
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->writeTexture(dfgLutTexture, dfgLutPackedPixels.data(),
                uint64_t(dfgLutPackedPixels.size()) * sizeof(uint))
            ->submit();
        for (uint mip = 0u; mip < uint(normalMips.size()); ++mip)
            graphicsQueue->writeTexture(normalTexture, normalMips[mip].data(), uint64_t(normalMips[mip].size()), mip)->submit();
        for (uint mip = 0u; mip < uint(aoMips.size()); ++mip)
            graphicsQueue->writeTexture(aoTexture, aoMips[mip].data(), uint64_t(aoMips[mip].size()), mip)->submit();
        for (uint mip = 0u; mip < uint(displacementMips.size()); ++mip)
            graphicsQueue->writeTexture(displacementTexture, displacementMips[mip].data(), uint64_t(displacementMips[mip].size()), mip)->submit();
        for (uint face = 0u; face < 6u; ++face)
            for (uint mip = 0u; mip < uint(cubeMips[face].size()); ++mip)
                graphicsQueue->writeTexture(cubeTextures[face], cubeMips[face][mip].data(), uint64_t(cubeMips[face][mip].size()), mip)->submit();
        auto sourceResources = device->createBindGroup<
            WebgpuMaterialsDisplacementmapPmremSourceResources>(
            cubeTextures[0u]->createView(), cubeTextures[1u]->createView(),
            cubeTextures[2u]->createView(), cubeTextures[3u]->createView(),
            cubeTextures[4u]->createView(), cubeTextures[5u]->createView(),
            materialSampler, environmentAtlas->createView());
        auto sourcePass = device->createComputeClass<
            WebgpuMaterialsDisplacementmapPmremSourcePass>(sourceResources);
        graphicsQueue
            ->computePass(
                "WebgpuMaterialsDisplacementmapPmremCubeLevelZero",
                sourcePass(
                    WebgpuMaterialsDisplacementmapPmremAtlasWidth,
                    WebgpuMaterialsDisplacementmapPmremCubeSize * 2u,
                    1u))
            ->submit();
        for (uint lodIndex = 1u;
             lodIndex < WebgpuMaterialsDisplacementmapPmremLodCount;
             ++lodIndex)
        {
            const uint mipExponent = lodIndex <= 5u ? 9u - lodIndex : 4u;
            const uint faceSize = 1u << mipExponent;
            const uint outputX = lodIndex > 5u
                ? 3u * faceSize * (lodIndex - 5u)
                : 0u;
            const uint outputY = 4u *
                (WebgpuMaterialsDisplacementmapPmremCubeSize - faceSize);
            WebgpuMaterialsDisplacementmapPmremUniforms pmremUniforms;
            pmremUniforms.outputOffsetSize = uint4(
                outputX, outputY, faceSize * 3u, faceSize * 2u);
            pmremUniforms.roughnessSourceMipAndReserved = float4(
                float(lodIndex) /
                    float(WebgpuMaterialsDisplacementmapPmremLodCount - 1u),
                10.0f - float(lodIndex), 0.0f, 0.0f);
            auto filterResources = device->createBindGroup<
                WebgpuMaterialsDisplacementmapPmremFilterResources>(
                pmremUniformBuffers[lodIndex - 1u],
                environmentAtlas->createView(), environmentSampler,
                environmentPingPongAtlas->createView());
            auto filterPass = device->createComputeClass<
                WebgpuMaterialsDisplacementmapPmremFilterPass>(
                filterResources);
            auto copyResources = device->createBindGroup<
                WebgpuMaterialsDisplacementmapPmremCopyResources>(
                pmremUniformBuffers[lodIndex - 1u],
                environmentPingPongAtlas->createView(),
                environmentAtlas->createView());
            auto copyPass = device->createComputeClass<
                WebgpuMaterialsDisplacementmapPmremCopyPass>(copyResources);
            graphicsQueue
                ->writeBuffer(BufferRange(pmremUniformBuffers[lodIndex - 1u]),
                    &pmremUniforms, sizeof(pmremUniforms))
                ->computePass(
                    "WebgpuMaterialsDisplacementmapPmremFilter",
                    filterPass(faceSize * 3u, faceSize * 2u, 1u))
                ->computePass(
                    "WebgpuMaterialsDisplacementmapPmremCopy",
                    copyPass(faceSize * 3u, faceSize * 2u, 1u));
        }
        graphicsQueue->submit();
        resources = device->createBindGroup<WebgpuMaterialsDisplacementmapResources>(
            uniformBuffer, normalTexture->createView(), aoTexture->createView(),
            displacementTexture->createView(), environmentAtlas->createView(),
            dfgLutTexture->createView(), materialSampler, environmentSampler,
            dfgSampler);
        backPass = device->createRenderClass<WebgpuMaterialsDisplacementmapBackPass>(resources);
        frontPass = device->createRenderClass<WebgpuMaterialsDisplacementmapFrontPass>(resources);
        overlayPass = device->createRenderClass<
            WebgpuMaterialsDisplacementmapOverlayPass>(resources);
    }

    /** Executes the two DoubleSide Scene passes and presents the RGBA8 result. */
    void render() override
    {
        WebgpuMaterialsDisplacementmapFrameBuffer frame;
        frame.color = outputTexture->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frame.depth = depthTexture->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
        WebgpuMaterialsDisplacementmapFrameBuffer frontFrame = frame;
        frontFrame.color.loadOp = LoadOp::Load;
        frontFrame.depth.depthLoadOp = LoadOp::Load;
        WebgpuMaterialsDisplacementmapOverlayFrameBuffer overlayFrame;
        overlayFrame.color = outputTexture->createView();
        overlayFrame.color.loadOp = LoadOp::Load;
        overlayFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuMaterialsDisplacementmapBack", frame,
                backPass->setVertexBuffer(vertexBuffer),
                backPass->setIndexBuffer(indexBuffer),
                backPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebgpuMaterialsDisplacementmapFront", frontFrame,
                frontPass->setVertexBuffer(vertexBuffer),
                frontPass->setIndexBuffer(indexBuffer),
                frontPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebgpuMaterialsDisplacementmapInspector", overlayFrame,
                overlayPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created readback target. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the fixed capture width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the fixed capture height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases every dedicated geometry, texture, and target resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(normalTexture);
        device->freeTexture(aoTexture);
        device->freeTexture(displacementTexture);
        for (uint face = 0u; face < 6u; ++face)
            device->freeTexture(cubeTextures[face]);
        device->freeTexture(environmentAtlas);
        device->freeTexture(environmentPingPongAtlas);
        device->freeTexture(dfgLutTexture);
        for (uint index = 0u;
             index < WebgpuMaterialsDisplacementmapPmremLodCount - 1u;
             ++index)
            device->freeBuffer(pmremUniformBuffers[index]);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
