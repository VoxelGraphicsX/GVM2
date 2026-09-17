#ifndef GVM_RENDER_FEATURE_SUITE_HPP
#define GVM_RENDER_FEATURE_SUITE_HPP

#include "UGL.h"

#include "Composite.hpp"
#include "Cube.hpp"
#include "CubeData.hpp"
#include "Quad.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace UGL;

#define GVM_RENDER_SUITE_LOG(step)                           \
    do                                                       \
    {                                                        \
        std::fputs("[gvm-render-suite] " step "\n", stderr); \
        std::fflush(stderr);                                 \
    } while (0)

struct FeatureVertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
    float2 uv [[Attribute2]];
};

struct FeatureVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct FeatureFrameBuffer : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct FeaturePixelLocalFrameBuffer final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        gbuffer;

    PixelLocalColorAttachment<TextureFormat::R32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        gbufferDepth;

    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::DontCare,
                              PixelLocalStore::Discard>
        lighting;

    ColorAttachment<TextureFormat::RGBA8Unorm> present;

    PixelLocalDepthAttachment<TextureFormat::Depth32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        depth;
};

struct FeatureGlobals
{
    float4 tint;
};

struct FeaturePixelLocalSceneGlobals
{
    float4 viewportTime;
    float4 lightPosition0;
    float4 lightColor0;
    float4 lightPosition1;
    float4 lightColor1;
    float4 lightPosition2;
    float4 lightColor2;
    float4 lightPosition3;
    float4 lightColor3;
};

struct FeatureGlobalsBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<FeatureGlobals> globalsBuffer [[Binding0]])
    {
    }
};

struct FeaturePixelLocalSceneBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<FeaturePixelLocalSceneGlobals> globalsBuffer [[Binding0]])
    {
    }
};

struct TexturedTriangleBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct ComputePatternBindGroup final : public IBindGroup
{
    constructor(RWTexture2D<UGL::TextureFormat::RGBA8Unorm> patternTexture [[Binding0]])
    {
    }
};

class ProceduralTrianglePass final : public IRenderClass
{
public:
    constructor(BindGroup<FeatureGlobalsBindGroup> globalsBindGroup [[Slot0]])
    {
    }

private:
    FeatureVertexOutput vertex(uint vid [[VertexID]])
    {
        FeatureVertexOutput output;
        if (vid == 0)
        {
            output.pos = float4(-0.75f, -0.65f, 0.0f, 1.0f);
            output.color = float4(1.0f, 0.2f, 0.25f, 1.0f);
            output.uv = float2(0.0f, 1.0f);
        }
        else if (vid == 1)
        {
            output.pos = float4(0.0f, 0.75f, 0.0f, 1.0f);
            output.color = float4(0.2f, 1.0f, 0.35f, 1.0f);
            output.uv = float2(0.5f, 0.0f);
        }
        else
        {
            output.pos = float4(0.75f, -0.65f, 0.0f, 1.0f);
            output.color = float4(0.2f, 0.45f, 1.0f, 1.0f);
            output.uv = float2(1.0f, 1.0f);
        }
        return output;
    }

    FeatureFrameBuffer fragment(FeatureVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        framebuffer.color = half4(vertexIn.color * globalsBindGroup->globalsBuffer->tint);
        return framebuffer;
    }
};

class BufferedTrianglePass final : public IRenderClass
{
public:
    constructor(BindGroup<FeatureGlobalsBindGroup> globalsBindGroup [[Slot0]])
    {
    }

private:
    FeatureVertexOutput vertex(uint vid [[VertexID]], FeatureVertexInput input [[VertexInput0]])
    {
        FeatureVertexOutput output;
        output.pos = input.pos;
        output.color = input.color;
        output.uv = input.uv;
        return output;
    }

    FeatureFrameBuffer fragment(FeatureVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        framebuffer.color = half4(vertexIn.color * globalsBindGroup->globalsBuffer->tint);
        return framebuffer;
    }
};

class TexturedTrianglePass final : public IRenderClass
{
public:
    constructor(BindGroup<TexturedTriangleBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    FeatureVertexOutput vertex(uint vid [[VertexID]], FeatureVertexInput input [[VertexInput0]])
    {
        FeatureVertexOutput output;
        output.pos = input.pos;
        output.color = input.color;
        output.uv = input.uv;
        return output;
    }

    FeatureFrameBuffer fragment(FeatureVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        float4 sampledColor = bindGroup->texture0->sample(bindGroup->sampler0, vertexIn.uv);
        framebuffer.color = half4(sampledColor);
        return framebuffer;
    }
};

class InstancedTrianglePass final : public IRenderClass
{
public:
    constructor(BindGroup<FeatureGlobalsBindGroup> globalsBindGroup [[Slot0]])
    {
    }

private:
    FeatureVertexOutput vertex(uint vid [[VertexID]], uint instanceID [[InstanceID]])
    {
        FeatureVertexOutput output;
        float2 localPos;
        if (vid == 0)
        {
            localPos = float2(-0.45f, -0.42f);
        }
        else if (vid == 1)
        {
            localPos = float2(0.0f, 0.55f);
        }
        else
        {
            localPos = float2(0.45f, -0.42f);
        }

        const float gridX = float(instanceID % 5);
        const float gridY = float(instanceID / 5);
        const float2 center = float2(-0.78f + gridX * 0.39f, -0.58f + gridY * 0.56f);
        const float scale = 0.16f;

        output.pos = float4(center + localPos * scale, 0.0f, 1.0f);
        output.color = float4(0.20f + 0.12f * float(instanceID % 4), 0.30f + 0.10f * float((instanceID + 1) % 5), 0.95f - 0.08f * float(instanceID % 6), 1.0f);
        output.uv = localPos * 0.5f + 0.5f;
        return output;
    }

    FeatureFrameBuffer fragment(FeatureVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        framebuffer.color = half4(vertexIn.color * globalsBindGroup->globalsBuffer->tint);
        return framebuffer;
    }
};

class FullscreenGradientPass final : public IRenderClass
{
public:
    constructor(BindGroup<FeatureGlobalsBindGroup> globalsBindGroup [[Slot0]])
    {
    }

private:
    FeatureVertexOutput vertex(uint vid [[VertexID]])
    {
        FeatureVertexOutput output;
        if (vid == 0)
        {
            output.pos = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            output.uv = float2(0.0f, 0.0f);
        }
        else if (vid == 1)
        {
            output.pos = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            output.uv = float2(0.0f, 2.0f);
        }
        else
        {
            output.pos = float4(3.0f, -1.0f, 0.0f, 1.0f);
            output.uv = float2(2.0f, 0.0f);
        }
        output.color = float4(output.uv, 1.0f, 1.0f);
        return output;
    }

    FeatureFrameBuffer fragment(FeatureVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        const float2 centered = vertexIn.uv * 2.0f - 1.0f;
        const float rings = 0.5f + 0.5f * cos(length(centered) * 18.0f);
        const float3 vertical = lerp(float3(0.05f, 0.08f, 0.16f), float3(0.95f, 0.40f, 0.18f), saturate(vertexIn.uv.y));
        const float3 horizontal = lerp(vertical, float3(0.18f, 0.72f, 0.95f), saturate(vertexIn.uv.x) * 0.55f);
        const float vignette = saturate(1.15f - dot(centered, centered));
        const float3 gradientColor = horizontal * (0.65f + 0.35f * rings) * vignette;
        framebuffer.color = half4(gradientColor.x, gradientColor.y, gradientColor.z, 1.0f);
        return framebuffer;
    }
};

struct FeaturePixelLocalVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 normal [[Attribute1]];
    float3 albedo [[Attribute2]];
    float3 material [[Attribute3]];
};

namespace FeaturePixelLocalPbr
{
    /**
     * Returns true when a scalar is NaN or Infinity.
     */
    bool isNonFiniteFloat(float value)
    {
        const float cancellation = value - value;
        return value != value || cancellation != cancellation;
    }

    /**
     * Returns true when any vector component is NaN or Infinity.
     */
    bool isNonFiniteFloat3(IN float3 value)
    {
        return isNonFiniteFloat(value.x) || isNonFiniteFloat(value.y) || isNonFiniteFloat(value.z);
    }

    /**
     * Returns a normalized vector with a deterministic fallback for near-zero or non-finite inputs.
     */
    float3 safeNormalize(IN float3 value)
    {
        const float lengthSquared = dot(value, value);
        if (isNonFiniteFloat(lengthSquared) || lengthSquared < 0.000001f)
        {
            return float3(0.0f, 0.0f, 1.0f);
        }
        return value * rsqrt(lengthSquared);
    }

    /**
     * Clamps a scalar to the normalized material payload domain after replacing non-finite values.
     */
    float safeUnitFloat(float value)
    {
        if (isNonFiniteFloat(value))
        {
            return 0.0f;
        }
        return saturate(value);
    }

    /**
     * Clamps a color to the finite RGBA8 lighting attachment range.
     */
    float3 safeUnitFloat3(IN float3 value)
    {
        return float3(safeUnitFloat(value.x), safeUnitFloat(value.y), safeUnitFloat(value.z));
    }

    float safeNonNegativeFloat(float value)
    {
        if (isNonFiniteFloat(value) || value < 0.0f)
        {
            return 0.0f;
        }
        return value;
    }

    bool isValidHdrChannel(float value)
    {
        return !isNonFiniteFloat(value) && value >= 0.0f;
    }

    float safeHdrFallbackAverage(IN float3 value, float fallback)
    {
        float sum = 0.0f;
        float count = 0.0f;
        if (isValidHdrChannel(value.x))
        {
            sum += value.x;
            count += 1.0f;
        }
        if (isValidHdrChannel(value.y))
        {
            sum += value.y;
            count += 1.0f;
        }
        if (isValidHdrChannel(value.z))
        {
            sum += value.z;
            count += 1.0f;
        }
        if (count < 0.5f)
        {
            return fallback;
        }
        return sum / count;
    }

    float safeHdrChannel(float value, float fallback)
    {
        if (!isValidHdrChannel(value))
        {
            return fallback;
        }
        return min(value, 32.0f);
    }

    float3 safeHdrFloat3(IN float3 value)
    {
        const float fallback = safeHdrFallbackAverage(value, 0.0f);
        return float3(safeHdrChannel(value.x, fallback), safeHdrChannel(value.y, fallback), safeHdrChannel(value.z, fallback));
    }

    float3 compressLightingForLdrAttachment(IN float3 hdrColor)
    {
        const float fallback = safeHdrFallbackAverage(hdrColor, 0.18f);
        const float3 resolvedColor = float3(safeHdrChannel(hdrColor.x, fallback),
                                            safeHdrChannel(hdrColor.y, fallback),
                                            safeHdrChannel(hdrColor.z, fallback));
        return safeUnitFloat3(resolvedColor / (resolvedColor + float3(1.0f)));
    }

    /**
     * Returns a positive scalar suitable for reciprocal and exponent operations.
     */
    float safePositiveFloat(float value, float fallback)
    {
        if (isNonFiniteFloat(value) || value < fallback)
        {
            return fallback;
        }
        return value;
    }

    /**
     * Returns a finite saturate(dot(a, b)) for BRDF cosine terms.
     */
    float safeSaturateDot(IN float3 lhs, IN float3 rhs)
    {
        return safeUnitFloat(dot(lhs, rhs));
    }

    /**
     * Evaluates pow() only on the well-defined positive normalized domain.
     */
    float safePow01(float base, float exponent)
    {
        return pow(max(safeUnitFloat(base), 0.000001f), safePositiveFloat(exponent, 0.000001f));
    }

    float3 materialBaseColor(uint column, uint row)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }

    float2 pixelLocalSphereCenterClip(float columnValue, float rowValue)
    {
        return float2(-0.82f + columnValue * 1.64f,
                      0.78f - rowValue * 1.56f);
    }

    float2 pixelLocalSphereCenterWorld(float columnValue, float rowValue)
    {
        return float2((columnValue * 7.0f - 3.5f) * 0.70f,
                      (3.5f - rowValue * 7.0f) * 0.52f);
    }

    float pixelLocalSphereClipRadiusY(float columnValue, float rowValue)
    {
        return 0.064f;
    }

    float pixelLocalSphereWorldRadius(float columnValue, float rowValue)
    {
        return 0.190f;
    }

    float distributionGGX(float nDotH, float roughness)
    {
        const float resolvedRoughness = clamp(safePositiveFloat(roughness, 0.16f), 0.16f, 0.98f);
        const float roughnessSquared = resolvedRoughness * resolvedRoughness;
        const float alphaSquared = roughnessSquared * roughnessSquared;
        const float resolvedNDotH = safeUnitFloat(nDotH);
        const float nDotHSquared = resolvedNDotH * resolvedNDotH;
        const float denominator = nDotHSquared * (alphaSquared - 1.0f) + 1.0f;
        return min(alphaSquared / max(3.1415926535f * denominator * denominator, 0.0025f), 8.0f);
    }

    float geometrySmith(float nDotV, float nDotL, float roughness)
    {
        const float resolvedNDotV = safeUnitFloat(nDotV);
        const float resolvedNDotL = safeUnitFloat(nDotL);
        const float resolvedRoughness = clamp(safePositiveFloat(roughness, 0.16f), 0.16f, 0.98f);
        const float remapped = resolvedRoughness + 1.0f;
        const float k = (remapped * remapped) * 0.125f;
        const float viewOcclusion = resolvedNDotV / max(resolvedNDotV * (1.0f - k) + k, 0.0001f);
        const float lightOcclusion = resolvedNDotL / max(resolvedNDotL * (1.0f - k) + k, 0.0001f);
        return safeUnitFloat(viewOcclusion * lightOcclusion);
    }

    float3 fresnelSchlick(float cosTheta, IN float3 f0)
    {
        const float3 resolvedF0 = safeUnitFloat3(f0);
        const float oneMinusCos = safeUnitFloat(1.0f - safeUnitFloat(cosTheta));
        const float oneMinusCosSquared = oneMinusCos * oneMinusCos;
        const float oneMinusCosFifth = oneMinusCosSquared * oneMinusCosSquared * oneMinusCos;
        return resolvedF0 + (1.0f - resolvedF0) * oneMinusCosFifth;
    }

    float3 shadePointLight(IN float3 worldPosition,
                           IN float3 normal,
                           IN float3 viewDirection,
                           IN float3 albedo,
                           float roughness,
                           float metalness,
                           IN float3 f0,
                           IN float4 lightPosition,
                           IN float4 lightColor)
    {
        const float3 lightVector = lightPosition.xyz - worldPosition;
        const float distanceSquared = max(dot(lightVector, lightVector), 0.16f);
        const float3 lightDirection = safeNormalize(lightVector);
        const float3 halfVector = safeNormalize(viewDirection + lightDirection);
        const float attenuation = min(safeNonNegativeFloat(lightPosition.w) / distanceSquared, 1.65f);
        const float3 radiance = safeHdrFloat3(min(lightColor.xyz * safeNonNegativeFloat(lightColor.w) * attenuation, float3(8.0f)));

        const float nDotV = safeSaturateDot(normal, viewDirection);
        const float nDotL = safeSaturateDot(normal, lightDirection);
        const float nDotH = safeSaturateDot(normal, halfVector);
        const float resolvedRoughness = clamp(safePositiveFloat(roughness, 0.16f), 0.16f, 0.94f);
        const float gloss = safeUnitFloat(1.0f - resolvedRoughness);
        const float nDotHSquared = nDotH * nDotH;
        const float nDotHFourth = nDotHSquared * nDotHSquared;
        const float nDotHEighth = nDotHFourth * nDotHFourth;
        const float nDotHSixteenth = nDotHEighth * nDotHEighth;
        const float specularTerm = lerp(nDotHFourth, nDotHSixteenth, gloss) * (0.12f + gloss * 0.88f) * (0.35f + metalness * 1.55f);
        const float3 specular = min(fresnelSchlick(safeSaturateDot(halfVector, viewDirection), f0) * specularTerm, float3(2.0f));
        const float3 diffuse = albedo * (1.0f - metalness * 0.72f) * (1.0f / 3.1415926535f);
        const float3 contribution = (diffuse + specular) * radiance * nDotL;
        return safeHdrFloat3(min(contribution, float3(12.0f)));
    }

    float3 environmentReflection(IN float3 normal, IN float3 viewDirection, float roughness, float metalness, IN float3 f0)
    {
        const float nDotV = safeSaturateDot(normal, viewDirection);
        const float skyWeight = safeUnitFloat(normal.z * 0.68f + normal.y * 0.24f + 0.08f);
        const float3 groundColor = float3(0.14f, 0.13f, 0.12f);
        const float3 horizonColor = float3(0.74f, 0.78f, 0.84f);
        const float3 skyColor = float3(0.30f, 0.46f, 0.96f);
        float3 environmentColor = lerp(groundColor, horizonColor, skyWeight);
        environmentColor = lerp(environmentColor, skyColor, skyWeight * skyWeight);
        const float resolvedRoughness = clamp(safePositiveFloat(roughness, 0.16f), 0.16f, 0.98f);
        const float rimWeight = safeUnitFloat(1.0f - nDotV);
        const float warmReflection = rimWeight * rimWeight * safeUnitFloat(0.35f + normal.x * 0.35f + normal.y * 0.30f);
        environmentColor += float3(1.0f, 0.82f, 0.55f) * warmReflection * max(1.0f - resolvedRoughness, 0.0f) * 1.35f;
        const float3 fresnel = fresnelSchlick(nDotV, f0);
        return safeUnitFloat3(environmentColor * fresnel * (0.42f + metalness * 2.35f) * max(1.0f - resolvedRoughness * 0.62f, 0.0f));
    }
} // namespace FeaturePixelLocalPbr

class FeaturePixelLocalGBufferPass final : public IRenderClass
{
public:
    constructor(BindGroup<FeaturePixelLocalSceneBindGroup> sceneBindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    FeaturePixelLocalVertexOutput vertex(uint vertexID [[VertexID]], uint instanceID [[InstanceID]])
    {
        FeaturePixelLocalVertexOutput outputValue;
        const uint longitudeSegments = 32u;
        const uint radialSegments = 12u;
        const uint triangleID = vertexID / 6u;
        const uint cornerID = vertexID - triangleID * 6u;
        const uint longitudeBase = triangleID - (triangleID / longitudeSegments) * longitudeSegments;
        const uint radialBase = triangleID / longitudeSegments;
        uint longitude = longitudeBase;
        uint radial = radialBase;

        if (cornerID == 2u || cornerID == 3u || cornerID == 5u)
        {
            longitude = longitudeBase + 1u;
        }
        if (cornerID == 1u || cornerID == 4u || cornerID == 5u)
        {
            radial = radialBase + 1u;
        }

        const float theta = float(longitude) * (6.283185307f / float(longitudeSegments));
        const float diskRadius = float(radial) * (1.0f / float(radialSegments));
        const float2 diskPosition = float2(cos(theta), sin(theta)) * diskRadius;
        const float normalZ = sqrt(max(1.0f - dot(diskPosition, diskPosition), 0.0f));
        const float3 normal = FeaturePixelLocalPbr::safeNormalize(float3(diskPosition.x, diskPosition.y, normalZ));

        const uint column = instanceID - (instanceID / 8u) * 8u;
        const uint row = instanceID / 8u;
        const float columnValue = float(column) * (1.0f / 7.0f);
        const float rowValue = float(row) * (1.0f / 7.0f);
        const float aspectCompensation = sceneBindGroup->globalsBuffer->viewportTime.y / max(sceneBindGroup->globalsBuffer->viewportTime.x, 1.0f);
        const float radiusY = FeaturePixelLocalPbr::pixelLocalSphereClipRadiusY(columnValue, rowValue);
        const float radiusX = radiusY * aspectCompensation;
        const float2 sphereCenterClip = FeaturePixelLocalPbr::pixelLocalSphereCenterClip(columnValue, rowValue);
        const float2 sphereCenterWorld = FeaturePixelLocalPbr::pixelLocalSphereCenterWorld(columnValue, rowValue);
        const float sphereRadiusWorld = FeaturePixelLocalPbr::pixelLocalSphereWorldRadius(columnValue, rowValue);

        outputValue.worldPosition = float3(sphereCenterWorld + normal.xy * sphereRadiusWorld, normal.z * sphereRadiusWorld);
        outputValue.normal = normal;
        outputValue.albedo = FeaturePixelLocalPbr::materialBaseColor(column, row);
        outputValue.material = float3(rowValue, columnValue, float(row * 8u + column) * (1.0f / 63.0f));
        outputValue.position = float4(sphereCenterClip + normal.xy * float2(radiusX, radiusY),
                                      0.34f + normal.z * 0.11f + rowValue * 0.02f,
                                      1.0f);
        return outputValue;
    }

    FeaturePixelLocalFrameBuffer fragment(FeaturePixelLocalVertexOutput inputValue)
    {
        FeaturePixelLocalFrameBuffer outputValue;
        const float3 normal = FeaturePixelLocalPbr::safeNormalize(inputValue.normal);
        const float3 encodedNormal = normal * 0.5f + float3(0.5f);
        outputValue.gbuffer = half4(encodedNormal.x, encodedNormal.y, encodedNormal.z, inputValue.material.z);
        outputValue.gbufferDepth = inputValue.position.z;
        return outputValue;
    }
};

class FeaturePixelLocalLightingPass final : public IPixelLocalRenderClass
{
public:
    constructor(BindGroup<FeaturePixelLocalSceneBindGroup> sceneBindGroup [[Slot0]])
    {
    }

private:
    FeaturePixelLocalFrameBuffer pixel(FeaturePixelLocalFrameBuffer inputValue [[PixelLocalInput]])
    {
        FeaturePixelLocalFrameBuffer outputValue;
        const float4 lightPosition0 = sceneBindGroup->globalsBuffer->lightPosition0;
        const float4 lightColor0 = sceneBindGroup->globalsBuffer->lightColor0;
        const float4 lightPosition1 = sceneBindGroup->globalsBuffer->lightPosition1;
        const float4 lightColor1 = sceneBindGroup->globalsBuffer->lightColor1;
        const float4 lightPosition2 = sceneBindGroup->globalsBuffer->lightPosition2;
        const float4 lightColor2 = sceneBindGroup->globalsBuffer->lightColor2;
        const float4 lightPosition3 = sceneBindGroup->globalsBuffer->lightPosition3;
        const float4 lightColor3 = sceneBindGroup->globalsBuffer->lightColor3;

        half4 gbufferValue = inputValue.gbuffer.read();
        const float gbufferDepthValue = FeaturePixelLocalPbr::safeUnitFloat(inputValue.gbufferDepth.read());
        const float gbufferX = float(gbufferValue.x);
        const float gbufferY = float(gbufferValue.y);
        const float gbufferZ = float(gbufferValue.z);
        const float gbufferW = float(gbufferValue.w);

        const float decodedGBufferX = FeaturePixelLocalPbr::safeUnitFloat(gbufferX);
        const float decodedGBufferY = FeaturePixelLocalPbr::safeUnitFloat(gbufferY);
        const float decodedGBufferZ = FeaturePixelLocalPbr::safeUnitFloat(gbufferZ);
        const float decodedGBufferW = FeaturePixelLocalPbr::safeUnitFloat(gbufferW);
        const float materialIndex = floor(decodedGBufferW * 63.0f + 0.5f);
        const float rowIndex = floor(materialIndex * (1.0f / 8.0f));
        const float columnIndex = materialIndex - rowIndex * 8.0f;
        const float rowValue = clamp(rowIndex * (1.0f / 7.0f), 0.0f, 1.0f);
        const float columnValue = clamp(columnIndex * (1.0f / 7.0f), 0.0f, 1.0f);
        const float roughness = clamp(0.16f + rowValue * 0.68f, 0.16f, 0.94f);
        const float metalness = clamp(columnValue * 0.82f, 0.0f, 0.82f);
        const float3 normal = FeaturePixelLocalPbr::safeNormalize(float3(decodedGBufferX, decodedGBufferY, decodedGBufferZ) * 2.0f - float3(1.0f));
        const float2 sphereCenterWorld = FeaturePixelLocalPbr::pixelLocalSphereCenterWorld(columnValue, rowValue);
        const float sphereRadiusWorld = FeaturePixelLocalPbr::pixelLocalSphereWorldRadius(columnValue, rowValue);
        const float depthNormalZ = clamp((gbufferDepthValue - 0.34f - rowValue * 0.02f) * (1.0f / 0.11f), 0.0f, 1.0f);
        const float3 worldPosition = float3(sphereCenterWorld + normal.xy * sphereRadiusWorld, depthNormalZ * sphereRadiusWorld);
        const float3 albedo = float3(1.0f, 1.0f, 1.0f);
        const float3 viewDirection = FeaturePixelLocalPbr::safeNormalize(float3(0.0f, 0.0f, 3.2f) - worldPosition);
        const float3 f0 = lerp(float3(0.04f), albedo, metalness);
        const float nDotV = FeaturePixelLocalPbr::safeSaturateDot(normal, viewDirection);
        float3 lighting = albedo * (0.150f + 0.190f * max(normal.z, 0.0f) + 0.070f * max(normal.y, 0.0f)) * (1.0f - metalness * 0.35f);

        const float3 lightContribution0 = FeaturePixelLocalPbr::shadePointLight(worldPosition, normal, viewDirection, albedo, roughness, metalness, f0, lightPosition0, lightColor0);
        lighting += lightContribution0;

        const float3 lightContribution1 = FeaturePixelLocalPbr::shadePointLight(worldPosition, normal, viewDirection, albedo, roughness, metalness, f0, lightPosition1, lightColor1);
        lighting += lightContribution1;

        const float3 lightContribution2 = FeaturePixelLocalPbr::shadePointLight(worldPosition, normal, viewDirection, albedo, roughness, metalness, f0, lightPosition2, lightColor2);
        lighting += lightContribution2;

        const float3 lightContribution3 = FeaturePixelLocalPbr::shadePointLight(worldPosition, normal, viewDirection, albedo, roughness, metalness, f0, lightPosition3, lightColor3);
        lighting += lightContribution3;

        const float3 environmentContribution = FeaturePixelLocalPbr::environmentReflection(normal, viewDirection, roughness, metalness, f0);
        lighting += environmentContribution;

        const float3 rimContribution = FeaturePixelLocalPbr::safePow01(1.0f - nDotV, 4.0f) * (0.10f + metalness * 0.42f) * lerp(float3(0.65f, 0.75f, 1.0f), albedo, metalness);
        lighting += rimContribution;

        if (FeaturePixelLocalPbr::isNonFiniteFloat3(lighting))
        {
            lighting = float3(0.20f, 0.22f, 0.25f);
        }
        lighting = FeaturePixelLocalPbr::compressLightingForLdrAttachment(lighting);
        if (FeaturePixelLocalPbr::isNonFiniteFloat3(lighting))
        {
            lighting = float3(0.020f, 0.026f, 0.038f);
        }
        lighting = max(FeaturePixelLocalPbr::safeUnitFloat3(lighting), float3(0.020f, 0.026f, 0.038f));
        outputValue.lighting = half4(lighting.x, lighting.y, lighting.z, 1.0f);
        return outputValue;
    }
};

class FeaturePixelLocalTonemapPass final : public IPixelLocalRenderClass
{
public:
    constructor(BindGroup<FeaturePixelLocalSceneBindGroup> sceneBindGroup [[Slot0]])
    {
    }

private:
    FeaturePixelLocalFrameBuffer pixel(FeaturePixelLocalFrameBuffer inputValue [[PixelLocalInput]])
    {
        FeaturePixelLocalFrameBuffer outputValue;
        half4 lightingValue = inputValue.lighting.read();
        float3 color = float3(float(lightingValue.x), float(lightingValue.y), float(lightingValue.z));
        color = FeaturePixelLocalPbr::safeHdrFloat3(color);
        if (FeaturePixelLocalPbr::isNonFiniteFloat3(color))
        {
            color = float3(0.020f, 0.026f, 0.038f);
        }
        color = max(color, float3(0.020f, 0.026f, 0.038f));
        color *= sceneBindGroup->globalsBuffer->viewportTime.w;
        color = FeaturePixelLocalPbr::safeUnitFloat3(color);
        if (FeaturePixelLocalPbr::isNonFiniteFloat3(color))
        {
            color = float3(0.020f, 0.026f, 0.038f);
        }
        color = pow(max(color, float3(0.000001f)), float3(0.454545f));
        color = FeaturePixelLocalPbr::safeUnitFloat3(color);
        if (FeaturePixelLocalPbr::isNonFiniteFloat3(color))
        {
            color = float3(0.151f, 0.163f, 0.185f);
        }
        outputValue.present = half4(color.x, color.y, color.z, 1.0f);
        return outputValue;
    }
};

class [[LocalWorkGroupSize(8, 8, 1)]] ComputePatternPass final : public IComputeClass
{
public:
    constructor(BindGroup<ComputePatternBindGroup> patternBindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 ThreadID [[DispatchThreadID]])
    {
        uint2 resolution;
        patternBindGroup->patternTexture->getDimensions(resolution.x, resolution.y);
        if (ThreadID.x >= resolution.x || ThreadID.y >= resolution.y)
        {
            return;
        }

        const float2 uv = float2(ThreadID.xy) / float2(resolution.xy);
        const float2 centered = uv * 2.0f - 1.0f;
        const float rings = 0.5f + 0.5f * cos(length(centered) * 28.0f);
        const float stripes = 0.5f + 0.5f * sin((uv.x + uv.y) * 34.0f);
        float3 color = lerp(float3(0.03f, 0.05f, 0.10f), float3(0.92f, 0.52f, 0.16f), rings);
        color = lerp(color, float3(0.12f, 0.80f, 0.88f), stripes * 0.45f);
        patternBindGroup->patternTexture->write(ThreadID.xy, half4(color.x, color.y, color.z, 1.0f));
    }
};

class PostProcessPass final : public IRenderClass
{
public:
    constructor(BindGroup<QuadBindGroup> quadBindGroup [[Slot0]])
    {
    }

private:
    QuadVertexOutput vertex(uint vid [[VertexID]])
    {
        float2 outputUV = float2((vid << 1) & 2, vid & 2);
        QuadVertexOutput output;
        output.pos = float4(outputUV * 2.0f - 1.0f, 0.0f, 1.0f);
        output.texCoord = outputUV;
        return output;
    }

    FeatureFrameBuffer fragment(QuadVertexOutput vertexIn)
    {
        FeatureFrameBuffer framebuffer;
        const float4 sampled = quadBindGroup->texture0->sample(quadBindGroup->sampler0, vertexIn.texCoord);
        const float2 centered = vertexIn.texCoord * 2.0f - 1.0f;
        const float scanlines = 0.94f + 0.06f * cos(vertexIn.texCoord.y * 180.0f);
        const float vignette = saturate(1.12f - dot(centered, centered) * 0.85f);
        const float3 graded = sampled.xyz * scanlines * vignette;
        framebuffer.color = half4(graded.x, graded.y, graded.z, 1.0f);
        return framebuffer;
    }
};

class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;

    RenderClass<ProceduralTrianglePass> proceduralTriangle;
    RenderClass<BufferedTrianglePass> bufferedTriangle;
    RenderClass<TexturedTrianglePass> texturedTriangle;
    RenderClass<InstancedTrianglePass> instancedTriangle;
    RenderClass<FullscreenGradientPass> fullscreenGradient;
    RenderClass<FeaturePixelLocalGBufferPass> pixelLocalGBuffer;
    RenderClass<FeaturePixelLocalLightingPass> pixelLocalLighting;
    RenderClass<FeaturePixelLocalTonemapPass> pixelLocalTonemap;

    Buffer<FeatureGlobals, BufferUsage<Uniform, CopyDst>> featureGlobalsBuffer;
    Buffer<FeaturePixelLocalSceneGlobals, BufferUsage<Uniform, CopyDst>> pixelLocalSceneGlobalsBuffer;
    Buffer<FeatureVertexInput, BufferUsage<Vertex, CopyDst>> featureVertexBuffer;
    Buffer<uint32_t, BufferUsage<Index, CopyDst>> featureIndexBuffer;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> featureTexture;
    Sampler featureSampler;

    RenderClass<CubeDraw> cube;
    ComputeClass<CheckerBoardBackground> checkerBoardBackground;
    ComputeClass<Composite> composite;
    ComputeClass<ComputePatternPass> computePattern;
    RenderClass<PostProcessPass> postProcess;
    Buffer<CubeVertexInput, BufferUsage<Vertex, CopyDst>> cubeVertexBuffer;
    Buffer<Camera, BufferUsage<Uniform, CopyDst>> cameraBuffer;
    Texture<UGL::TextureFormat::BGRA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> gBufferAlbedo;
    Texture<UGL::TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> depthBuffer;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, StorageBinding, TextureBinding, CopySrc, CopyDst>, TextureDimension::e2D> albedoTexture;
    Texture<UGL::TextureFormat::R32Float, TextureUsage<StorageBinding, TextureBinding, CopyDst>, TextureDimension::e2D> depthTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, StorageBinding, TextureBinding, CopySrc, CopyDst>, TextureDimension::e2D> computePatternTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> offscreenTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, StorageBinding, TextureBinding, CopySrc, CopyDst>, TextureDimension::e2D> presentTexture;
    Texture<UGL::TextureFormat::PreferredSwapchain, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> swapchainStagingTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D> pixelLocalGBufferTexture;
    Texture<UGL::TextureFormat::R32Float, TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D> pixelLocalGBufferDepthTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D> pixelLocalLightingTexture;
    Texture<UGL::TextureFormat::Depth32Float, TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D> pixelLocalDepthTexture;
    Camera camera;
    int width = 640;
    int height = 480;
    int localFrameCounter = 0;

    [[Export]] int renderMode = 0;

    void createSharedSampler()
    {
        GVM_RENDER_SUITE_LOG("Creating shared sampler.");
        featureSampler = device->createSampler({.label = "FeatureSampler", .addressModeU = AddressMode::ClampToEdge, .addressModeV = AddressMode::ClampToEdge, .addressModeW = AddressMode::ClampToEdge, .magFilter = FilterMode::Linear, .minFilter = FilterMode::Linear, .mipmapFilter = MipmapFilterMode::Linear, .lodMinClamp = 0, .lodMaxClamp = 1, .maxAnisotropy = 1});
    }

    BindGroup<FeatureGlobalsBindGroup> createFeatureGlobalsBindGroup()
    {
        GVM_RENDER_SUITE_LOG("Triangle mode: create shared globals bind group.");
        featureGlobalsBuffer = device->createBuffer("FeatureGlobalsBuffer", 1);

        std::vector<FeatureGlobals> featureGlobalsData = {FeatureGlobals{float4(1.0f, 1.0f, 1.0f, 1.0f)}};

        device->graphicsQueue(0)->writeBuffer(BufferRange(featureGlobalsBuffer), featureGlobalsData.data(), sizeof(FeatureGlobals));

        return device->createBindGroup<FeatureGlobalsBindGroup>(featureGlobalsBuffer);
    }

    void createFeatureGeometryBuffers()
    {
        GVM_RENDER_SUITE_LOG("Triangle mode: create vertex/index buffers.");
        featureVertexBuffer = device->createBuffer("FeatureVertexBuffer", sizeof(float) * 30);
        featureIndexBuffer = device->createBuffer("FeatureIndexBuffer", sizeof(uint32_t) * 3);

        const std::vector<float> featureVertexData = {-0.70f, -0.60f, 0.0f, 1.0f, 1.0f, 0.25f, 0.25f, 1.0f, 0.0f, 1.0f, 0.00f, 0.72f, 0.0f, 1.0f, 0.25f, 1.0f, 0.35f, 1.0f, 0.5f, 0.0f, 0.74f, -0.60f, 0.0f, 1.0f, 0.25f, 0.45f, 1.0f, 1.0f, 1.0f, 1.0f};
        const std::vector<uint32_t> featureIndexData = {0, 1, 2};

        GVM_RENDER_SUITE_LOG("Triangle mode: upload vertex/index data.");
        device->graphicsQueue(0)->writeBuffer(BufferRange(featureVertexBuffer), featureVertexData.data(), sizeof(float) * featureVertexData.size());
        device->graphicsQueue(0)->writeBuffer(BufferRange(featureIndexBuffer), featureIndexData.data(), sizeof(uint32_t) * featureIndexData.size());
    }

    void initProceduralTriangleResources()
    {
        GVM_RENDER_SUITE_LOG("Triangle mode: create procedural render class.");
        BindGroup<FeatureGlobalsBindGroup> featureGlobalsBindGroup = createFeatureGlobalsBindGroup();
        proceduralTriangle = device->createRenderClass<ProceduralTrianglePass>(featureGlobalsBindGroup);
    }

    void initBufferedTriangleResources()
    {
        GVM_RENDER_SUITE_LOG("Triangle mode: create buffered render class.");
        BindGroup<FeatureGlobalsBindGroup> featureGlobalsBindGroup = createFeatureGlobalsBindGroup();
        bufferedTriangle = device->createRenderClass<BufferedTrianglePass>(featureGlobalsBindGroup);
        createFeatureGeometryBuffers();
    }

    void initTexturedTriangleResources()
    {
        GVM_RENDER_SUITE_LOG("Triangle mode: create textured resources.");
        featureTexture = device->createTexture("FeatureTexture", 2, 2, 1);
        createFeatureGeometryBuffers();
        createSharedSampler();
        GVM_RENDER_SUITE_LOG("Triangle mode: create textured bind group and render class.");
        BindGroup<TexturedTriangleBindGroup> texturedBindGroup = device->createBindGroup<TexturedTriangleBindGroup>(featureTexture->createView(), featureSampler);
        texturedTriangle = device->createRenderClass<TexturedTrianglePass>(texturedBindGroup);

        const std::vector<uint8_t> featureTextureData = {255, 32, 32, 255, 32, 255, 64, 255, 32, 96, 255, 255, 250, 220, 64, 255};

        GVM_RENDER_SUITE_LOG("Triangle mode: upload texture data.");
        device->graphicsQueue(0)->writeTexture(featureTexture, featureTextureData.data(), sizeof(uint8_t) * featureTextureData.size());
    }

    void initInstancedTriangleResources()
    {
        GVM_RENDER_SUITE_LOG("Instanced mode: create render class.");
        BindGroup<FeatureGlobalsBindGroup> featureGlobalsBindGroup = createFeatureGlobalsBindGroup();
        instancedTriangle = device->createRenderClass<InstancedTrianglePass>(featureGlobalsBindGroup);
    }

    void initFullscreenGradientResources()
    {
        GVM_RENDER_SUITE_LOG("Gradient mode: create fullscreen gradient class.");
        BindGroup<FeatureGlobalsBindGroup> featureGlobalsBindGroup = createFeatureGlobalsBindGroup();
        fullscreenGradient = device->createRenderClass<FullscreenGradientPass>(featureGlobalsBindGroup);
    }

    void initComputePatternResources()
    {
        GVM_RENDER_SUITE_LOG("Compute pattern mode: create texture and compute class.");
        computePatternTexture = device->createTexture("FeatureComputePatternTexture", width, height, 1);
        BindGroup<ComputePatternBindGroup> computePatternBindGroup = device->createBindGroup<ComputePatternBindGroup>(computePatternTexture->createView());
        computePattern = device->createComputeClass<ComputePatternPass>(computePatternBindGroup);
    }

    void initOffscreenPostProcessResources()
    {
        GVM_RENDER_SUITE_LOG("Post-process mode: create offscreen texture, scene pass and post-process pass.");
        BindGroup<FeatureGlobalsBindGroup> featureGlobalsBindGroup = createFeatureGlobalsBindGroup();
        createSharedSampler();
        offscreenTexture = device->createTexture("FeatureOffscreenTexture", width, height, 1);
        presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        instancedTriangle = device->createRenderClass<InstancedTrianglePass>(featureGlobalsBindGroup);
        BindGroup<QuadBindGroup> postProcessBindGroup = device->createBindGroup<QuadBindGroup>(offscreenTexture->createView(), featureSampler);
        postProcess = device->createRenderClass<PostProcessPass>(postProcessBindGroup);
    }

    void initPixelLocalDeferredResources()
    {
        GVM_RENDER_SUITE_LOG("Pixel-local mode: create PBR GBuffer attachments, globals and passes.");
        presentTexture = device->createTexture("FeaturePixelLocalPresentTexture", width, height, 1);
        pixelLocalGBufferTexture = device->createTexture("FeaturePixelLocalGBuffer", width, height, 1);
        pixelLocalGBufferDepthTexture = device->createTexture("FeaturePixelLocalGBufferDepth", width, height, 1);
        pixelLocalLightingTexture = device->createTexture("FeaturePixelLocalLighting", width, height, 1);
        pixelLocalDepthTexture = device->createTexture("FeaturePixelLocalDepth", width, height, 1);
        pixelLocalSceneGlobalsBuffer = device->createBuffer("FeaturePixelLocalSceneGlobalsBuffer", 1);
        BindGroup<FeaturePixelLocalSceneBindGroup> sceneBindGroup = device->createBindGroup<FeaturePixelLocalSceneBindGroup>(pixelLocalSceneGlobalsBuffer);
        pixelLocalGBuffer = device->createRenderClass<FeaturePixelLocalGBufferPass>(sceneBindGroup);
        pixelLocalLighting = device->createRenderClass<FeaturePixelLocalLightingPass>(sceneBindGroup);
        pixelLocalTonemap = device->createRenderClass<FeaturePixelLocalTonemapPass>(sceneBindGroup);
    }

    void uploadPixelLocalSceneGlobals()
    {
        const float time = float(localFrameCounter) * 0.18f;
        FeaturePixelLocalSceneGlobals globals = {};
        globals.viewportTime = float4(float(width), float(height), time, 0.78f);
        globals.lightPosition0 = float4(sin(time * 1.37f) * 2.10f, cos(time * 0.93f) * 1.22f, 2.32f + sin(time * 1.91f) * 0.12f, 3.85f);
        globals.lightColor0 = float4(1.00f, 1.00f, 1.00f, 4.2f);
        globals.lightPosition1 = float4(cos(time * 1.11f + 1.90f) * 2.02f, sin(time * 1.41f + 0.60f) * 1.14f, 2.42f + cos(time * 1.53f) * 0.12f, 3.65f);
        globals.lightColor1 = float4(1.00f, 1.00f, 1.00f, 4.0f);
        globals.lightPosition2 = float4(sin(time * 1.68f + 3.10f) * 1.92f, cos(time * 1.27f + 2.20f) * 1.08f, 2.50f + sin(time * 1.37f) * 0.10f, 3.45f);
        globals.lightColor2 = float4(1.00f, 1.00f, 1.00f, 3.8f);
        globals.lightPosition3 = float4(cos(time * 0.89f + 4.40f) * 2.25f, sin(time * 0.98f + 2.80f) * 1.25f, 2.58f + cos(time * 1.22f) * 0.10f, 3.95f);
        globals.lightColor3 = float4(1.00f, 1.00f, 1.00f, 3.6f);

        std::vector<FeaturePixelLocalSceneGlobals> globalsUpload = {globals};
        device->graphicsQueue(0)->writeBuffer(BufferRange(pixelLocalSceneGlobalsBuffer), globalsUpload.data(), sizeof(FeaturePixelLocalSceneGlobals));
    }

    /**
     * Renders the pixel-local deferred pipeline and optionally presents the resulting swapchain target.
     */
    void renderPixelLocalDeferredFrame(bool presentAfterSubmit)
    {
        uploadPixelLocalSceneGlobals();

        FeaturePixelLocalFrameBuffer framebuffer;
        framebuffer.gbuffer = pixelLocalGBufferTexture->createView();
        framebuffer.gbuffer.loadOp = LoadOp::Clear;
        framebuffer.gbuffer.clearValue = {0.5f, 0.5f, 0.0f, 0.0f};
        framebuffer.gbufferDepth = pixelLocalGBufferDepthTexture->createView();
        framebuffer.gbufferDepth.loadOp = LoadOp::Clear;
        framebuffer.gbufferDepth.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        framebuffer.lighting = pixelLocalLightingTexture->createView();
        framebuffer.lighting.loadOp = LoadOp::Clear;
        framebuffer.lighting.clearValue = {0.020f, 0.026f, 0.038f, 1.0f};
        framebuffer.present = presentTexture->createView();
        framebuffer.present.loadOp = LoadOp::Clear;
        framebuffer.present.clearValue = {0.01f, 0.03f, 0.06f, 1.0f};
        framebuffer.depth = pixelLocalDepthTexture->createView();
        framebuffer.depth.depthLoadOp = LoadOp::Clear;
        framebuffer.depth.depthStoreOp = StoreOp::Discard;
        framebuffer.depth.depthClearValue = 0.0f;
        auto nextTexture = swapchain->queryNextTexture();
        swapchainStagingTexture = nextTexture.texture;
        auto queue = device->graphicsQueue(0);
        queue->renderPass(
            "FeaturePixelLocalDeferredPass",
            framebuffer,
            pixelLocalPass(pixelLocalGBuffer(2304, 64, 0, 0),
                           nextPixelLocalPass(),
                           pixelLocalLighting(),
                           nextPixelLocalPass(),
                           pixelLocalTonemap()))
            ->renderToSwapchain(nextTexture, presentTexture)
            ->submit();
        if (presentAfterSubmit)
        {
            swapchain->present();
        }
    }

    void initCompositeResources()
    {
        GVM_RENDER_SUITE_LOG("Composite mode: create shared sampler.");
        createSharedSampler();

        GVM_RENDER_SUITE_LOG("Composite mode: create GBuffer and camera resources.");
        gBufferAlbedo = device->createTexture("FeatureGBufferAlbedo", width, height, 1);
        depthBuffer = device->createTexture("FeatureDepthBuffer", width, height, 1);
        albedoTexture = device->createTexture("FeatureCompositeAlbedo", width, height, 1);
        depthTexture = device->createTexture("FeatureCompositeDepth", width, height, 1);
        cameraBuffer = device->createBuffer("FeatureCameraBuffer", 1);
        cubeVertexBuffer = device->createBuffer("FeatureCubeVertexBuffer", cubeData.size());

        GVM_RENDER_SUITE_LOG("Composite mode: create bind groups.");
        BindGroup<CameraBindGroup> cameraBindGroup = device->createBindGroup<CameraBindGroup>(cameraBuffer);
        BindGroup<CheckBoardGBufferBindGroup> checkerBoardBindGroup = device->createBindGroup<CheckBoardGBufferBindGroup>(albedoTexture->createView(), depthTexture->createView());
        BindGroup<GBufferBindGroup> gBufferBindGroup = device->createBindGroup<GBufferBindGroup>(gBufferAlbedo->createView(), depthBuffer->createView());
        GVM_RENDER_SUITE_LOG("Composite mode: create cube, checkerboard and composite pipelines.");
        cube = device->createRenderClass<CubeDraw>(cameraBindGroup);
        checkerBoardBackground = device->createComputeClass<CheckerBoardBackground>(cameraBindGroup, checkerBoardBindGroup);
        composite = device->createComputeClass<Composite>(cameraBindGroup, checkerBoardBindGroup, gBufferBindGroup);

        GVM_RENDER_SUITE_LOG("Composite mode: initialize camera constants.");
        camera.proj = PerspectiveLH(45.0f * 3.1415926535f / 180.0f, float(width) / float(height), 1000.0f, 0.1f);
        camera.projInv = inverse(camera.proj);
        camera.view = lookAt(float3(0.0f, 1.75f, -4.5f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f));
        camera.viewInv = inverse(camera.view);

        std::vector<Camera> cameraUpload = {camera};
        GVM_RENDER_SUITE_LOG("Composite mode: upload camera and cube vertex data.");
        device->graphicsQueue(0)->writeBuffer(BufferRange(cameraBuffer), cameraUpload.data(), sizeof(Camera));
        device->graphicsQueue(0)->writeBuffer(BufferRange(cubeVertexBuffer), cubeData.data(), sizeof(float) * cubeData.size());
    }

public:
    void init(Device device, Swapchain swapchain)
    {
        GVM_RENDER_SUITE_LOG("Renderer init: storing device and swapchain.");
        this->device = device;
        this->swapchain = swapchain;
        localFrameCounter = 0;
        auto initialSwapchainTexture = this->swapchain->queryNextTexture();
        width = int(initialSwapchainTexture.texture->getWidth());
        height = int(initialSwapchainTexture.texture->getHeight());

        if (renderMode == 3)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering composite path.");
            initCompositeResources();
        }
        else if (renderMode == 8)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering pixel-local deferred path.");
            initPixelLocalDeferredResources();
        }
        else if (renderMode == 7)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering offscreen post-process path.");
            initOffscreenPostProcessResources();
        }
        else if (renderMode == 6)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering compute pattern path.");
            initComputePatternResources();
        }
        else if (renderMode == 5)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering fullscreen gradient path.");
            initFullscreenGradientResources();
            presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        }
        else if (renderMode == 4)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering instanced triangle path.");
            initInstancedTriangleResources();
            presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        }
        else if (renderMode == 2)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering textured triangle path.");
            initTexturedTriangleResources();
            presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        }
        else if (renderMode == 1)
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering buffered triangle path.");
            initBufferedTriangleResources();
            presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        }
        else
        {
            GVM_RENDER_SUITE_LOG("Renderer init: entering procedural triangle path.");
            initProceduralTriangleResources();
            presentTexture = device->createTexture("FeaturePresentTexture", width, height, 1);
        }

        GVM_RENDER_SUITE_LOG("Renderer init: completed.");
    }

    void render()
    {
        if (renderMode == 8)
        {
            renderPixelLocalDeferredFrame(true);
        }
        else if (renderMode == 3)
        {
            const float orbit = float(localFrameCounter) * 0.075f;
            const float3 eye = float3(sin(orbit) * 4.0f, 1.75f, -cos(orbit) * 4.0f);
            camera.view = lookAt(eye, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f));
            camera.viewInv = inverse(camera.view);
            std::vector<Camera> cameraUpload = {camera};
            device->graphicsQueue(0)->writeBuffer(BufferRange(cameraBuffer), cameraUpload.data(), sizeof(Camera));

            CubeFrameBuffer cubeFramebuffer;
            cubeFramebuffer.color = gBufferAlbedo->createView();
            cubeFramebuffer.color.loadOp = LoadOp::Clear;
            cubeFramebuffer.color.clearValue = {0.02f, 0.03f, 0.08f, 0.0f};
            cubeFramebuffer.color.storeOp = StoreOp::Store;
            cubeFramebuffer.depthStencil = depthBuffer->createView();
            cubeFramebuffer.depthStencil.depthLoadOp = LoadOp::Clear;
            cubeFramebuffer.depthStencil.depthStoreOp = StoreOp::Store;
            cubeFramebuffer.depthStencil.depthClearValue = 0.0f;

            device->graphicsQueue(0)->computePass("FeatureCheckerBoardPass", checkerBoardBackground(width, height, 1))->renderPass("FeatureCubePass", cubeFramebuffer, cube->setVertexBuffer(cubeVertexBuffer), cube(36, 1, 0, 0))->computePass("FeatureCompositePass", composite(width, height, 1));

            auto nextTexture = swapchain->queryNextTexture();
            device->graphicsQueue(0)->renderToSwapchain(nextTexture, albedoTexture)->submit();
            swapchain->present();
        }
        else if (renderMode == 7)
        {
            FeatureFrameBuffer offscreenFramebuffer;
            offscreenFramebuffer.color = offscreenTexture->createView();
            offscreenFramebuffer.color.loadOp = LoadOp::Clear;
            offscreenFramebuffer.color.clearValue = {0.01f, 0.03f, 0.08f, 1.0f};
            offscreenFramebuffer.color.storeOp = StoreOp::Store;

            FeatureFrameBuffer postProcessFramebuffer;
            postProcessFramebuffer.color = presentTexture->createView();
            postProcessFramebuffer.color.loadOp = LoadOp::Clear;
            postProcessFramebuffer.color.clearValue = {0.01f, 0.03f, 0.08f, 1.0f};
            postProcessFramebuffer.color.storeOp = StoreOp::Store;

            auto nextTexture = swapchain->queryNextTexture();
            device->graphicsQueue(0)->renderPass("FeatureOffscreenInstancedPass", offscreenFramebuffer, instancedTriangle(3, 10, 0, 0))
                ->renderPass("FeaturePostProcessPass", postProcessFramebuffer, postProcess(3, 1, 0, 0))
                ->renderToSwapchain(nextTexture, presentTexture)
                ->submit();
            swapchain->present();
        }
        else if (renderMode == 6)
        {
            auto nextTexture = swapchain->queryNextTexture();
            device->graphicsQueue(0)->computePass("FeatureComputePatternPass", computePattern(width, height, 1))
                ->renderToSwapchain(nextTexture, computePatternTexture)
                ->submit();
            swapchain->present();
        }
        else
        {
            FeatureFrameBuffer framebuffer;
            framebuffer.color = presentTexture->createView();
            framebuffer.color.loadOp = LoadOp::Clear;
            framebuffer.color.clearValue = {0.04f, 0.05f, 0.10f, 1.0f};
            framebuffer.color.storeOp = StoreOp::Store;

            auto queue = device->graphicsQueue(0);
            if (renderMode == 0)
            {
                queue->renderPass("FeatureProceduralTrianglePass", framebuffer, proceduralTriangle(3, 1, 0, 0));
            }
            else if (renderMode == 1)
            {
                queue->renderPass("FeatureBufferedTrianglePass", framebuffer, bufferedTriangle->setVertexBuffer(featureVertexBuffer), bufferedTriangle->setIndexBuffer(featureIndexBuffer), bufferedTriangle(3, 1, 0, 0));
            }
            else if (renderMode == 4)
            {
                queue->renderPass("FeatureInstancedTrianglePass", framebuffer, instancedTriangle(3, 10, 0, 0));
            }
            else if (renderMode == 5)
            {
                queue->renderPass("FeatureFullscreenGradientPass", framebuffer, fullscreenGradient(3, 1, 0, 0));
            }
            else
            {
                queue->renderPass("FeatureTexturedTrianglePass", framebuffer, texturedTriangle->setVertexBuffer(featureVertexBuffer), texturedTriangle->setIndexBuffer(featureIndexBuffer), texturedTriangle(3, 1, 0, 0));
            }

            auto nextTexture = swapchain->queryNextTexture();
            queue->renderToSwapchain(nextTexture, presentTexture)->submit();
            swapchain->present();
        }

        localFrameCounter++;
    }

    /**
     * Renders one pixel-local frame without presenting it so tests can read back the final swapchain target.
     */
    void renderPixelLocalDeferredReadbackFrame()
    {
        renderPixelLocalDeferredFrame(false);
        localFrameCounter++;
    }

    /**
     * Returns the final present texture so render-feature tests can validate screen output through GPU readback.
     */
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, StorageBinding, TextureBinding, CopySrc, CopyDst>, TextureDimension::e2D> getPresentTextureHandle() const
    {
        return presentTexture;
    }

    /**
     * Returns the texture that represents the current render mode's final visible image for readback parity tests.
     */
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, StorageBinding, TextureBinding, CopySrc, CopyDst>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        if (renderMode == 3)
        {
            return albedoTexture;
        }
        if (renderMode == 6)
        {
            return computePatternTexture;
        }
        return presentTexture;
    }

    /**
     * Returns the last swapchain target texture written by renderToSwapchain for final-screen readback.
     */
    Texture<UGL::TextureFormat::PreferredSwapchain, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> getSwapchainStagingTextureHandle() const
    {
        return swapchainStagingTexture;
    }

    void destroy()
    {
    }
};

#endif // GVM_RENDER_FEATURE_SUITE_HPP
