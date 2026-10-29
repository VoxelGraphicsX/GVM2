#ifndef GVM_THREE_WEBGPU_TSL_RAGING_SEA_HPP
#define GVM_THREE_WEBGPU_TSL_RAGING_SEA_HPP

#include "UGL.h"
#include "WebgpuTslRagingSeaData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the immutable standalone sea geometry frame state. */
struct WebgpuTslRagingSeaSceneResources final : public IBindGroup
{
    /** Declares the one uniform buffer consumed by the sea scene pass. */
    constructor(
        UniformBuffer<WebgpuTslRagingSeaUniforms> uniforms [[Binding0]],
        Texture2D<half4> dfgLut [[Binding1]],
        Sampler dfgSampler [[Binding2]])
    {
    }
};

/** Binds the linear scene image and final-output state. */
struct WebgpuTslRagingSeaOutputResources final : public IBindGroup
{
    /** Declares the scene texture, sampler, and shared frame state. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler linearSampler [[Binding1]],
        UniformBuffer<WebgpuTslRagingSeaUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries view-space standard-material inputs from the displaced grid. */
struct WebgpuTslRagingSeaVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 localPosition [[Attribute1]];
};

/** Carries analytic coordinates through the output and Inspector pass. */
struct WebgpuTslRagingSeaScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear half-float scene target and depth buffer. */
struct WebgpuTslRagingSeaSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 output target. */
struct WebgpuTslRagingSeaOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Rotates one unsigned value through the MaterialX 32-bit hash domain. */
uint webgpuTslRagingSeaRotateLeft(uint value, uint amount)
{
    return (value << amount) | (value >> (32u - amount));
}

/** Applies Bob Jenkins' final avalanche used by MaterialX integer hashes. */
uint webgpuTslRagingSeaHashFinal(uint a, uint b, uint c)
{
    c ^= b; c -= webgpuTslRagingSeaRotateLeft(b, 14u);
    a ^= c; a -= webgpuTslRagingSeaRotateLeft(c, 11u);
    b ^= a; b -= webgpuTslRagingSeaRotateLeft(a, 25u);
    c ^= b; c -= webgpuTslRagingSeaRotateLeft(b, 16u);
    a ^= c; a -= webgpuTslRagingSeaRotateLeft(c, 4u);
    b ^= a; b -= webgpuTslRagingSeaRotateLeft(a, 14u);
    c ^= b; c -= webgpuTslRagingSeaRotateLeft(b, 24u);
    return c;
}

/** Hashes three signed lattice coordinates with the r185 MaterialX seed. */
uint webgpuTslRagingSeaHash3(int x, int y, int z)
{
    uint a = 0xdeadbf08u + uint(x);
    uint b = 0xdeadbf08u + uint(y);
    uint c = 0xdeadbf08u + uint(z);
    return webgpuTslRagingSeaHashFinal(a, b, c);
}

/** Evaluates the MaterialX three-dimensional gradient selector. */
float webgpuTslRagingSeaGradient(uint hash, float x, float y, float z)
{
    const uint h = hash & 15u;
    const float u = h < 8u ? x : y;
    const float v = h < 4u ? y : ((h == 12u || h == 14u) ? x : z);
    return ((h & 1u) != 0u ? -u : u) +
        ((h & 2u) != 0u ? -v : v);
}

/** Applies MaterialX's quintic interpolation curve. */
float webgpuTslRagingSeaFade(float value)
{
    return value * value * value *
        (value * (value * 6.0f - 15.0f) + 10.0f);
}

/** Reproduces r185 mx_noise_float for one three-dimensional input. */
float webgpuTslRagingSeaNoise(float3 samplePosition)
{
    const int x = int(floor(samplePosition.x));
    const int y = int(floor(samplePosition.y));
    const int z = int(floor(samplePosition.z));
    const float fx = samplePosition.x - float(x);
    const float fy = samplePosition.y - float(y);
    const float fz = samplePosition.z - float(z);
    const float u = webgpuTslRagingSeaFade(fx);
    const float v = webgpuTslRagingSeaFade(fy);
    const float w = webgpuTslRagingSeaFade(fz);
    const float n000 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x, y, z), fx, fy, fz);
    const float n100 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x + 1, y, z), fx - 1.0f, fy, fz);
    const float n010 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x, y + 1, z), fx, fy - 1.0f, fz);
    const float n110 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x + 1, y + 1, z), fx - 1.0f, fy - 1.0f, fz);
    const float n001 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x, y, z + 1), fx, fy, fz - 1.0f);
    const float n101 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x + 1, y, z + 1), fx - 1.0f, fy, fz - 1.0f);
    const float n011 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x, y + 1, z + 1), fx, fy - 1.0f, fz - 1.0f);
    const float n111 = webgpuTslRagingSeaGradient(
        webgpuTslRagingSeaHash3(x + 1, y + 1, z + 1), fx - 1.0f, fy - 1.0f, fz - 1.0f);
    const float lower = lerp(lerp(n000, n100, u), lerp(n010, n110, u), v);
    const float upper = lerp(lerp(n001, n101, u), lerp(n011, n111, u), v);
    return lerp(lower, upper, w) * 0.9820f;
}

/** Evaluates all bounded large and small r185 wave octaves. */
float webgpuTslRagingSeaElevation(
    float3 position,
    float4 cameraPositionAndTime,
    float4 emissiveHighPowerShiftIterations,
    float4 largeFrequencySpeedMultiplier,
    float4 smallFrequencySpeedMultiplier)
{
    const float time = cameraPositionAndTime.w;
    float elevation =
        sin(position.x * largeFrequencySpeedMultiplier.x +
            time * largeFrequencySpeedMultiplier.z) *
        sin(position.z * largeFrequencySpeedMultiplier.y +
            time * largeFrequencySpeedMultiplier.z) *
        largeFrequencySpeedMultiplier.w;
    const float count = emissiveHighPowerShiftIterations.w;
    const float frequency = smallFrequencySpeedMultiplier.x;
    const float speed = smallFrequencySpeedMultiplier.y;
    const float multiplier = smallFrequencySpeedMultiplier.z;
    if (count >= 1.0f)
        elevation -= abs(webgpuTslRagingSeaNoise(float3(
            (position.x + 2.0f) * frequency,
            (position.z + 2.0f) * frequency,
            time * speed))) * multiplier;
    if (count >= 2.0f)
        elevation -= abs(webgpuTslRagingSeaNoise(float3(
            (position.x + 2.0f) * frequency * 2.0f,
            (position.z + 2.0f) * frequency * 2.0f,
            time * speed))) * multiplier * 0.5f;
    if (count >= 3.0f)
        elevation -= abs(webgpuTslRagingSeaNoise(float3(
            (position.x + 2.0f) * frequency * 3.0f,
            (position.z + 2.0f) * frequency * 3.0f,
            time * speed))) * multiplier / 3.0f;
    if (count >= 4.0f)
        elevation -= abs(webgpuTslRagingSeaNoise(float3(
            (position.x + 2.0f) * frequency * 4.0f,
            (position.z + 2.0f) * frequency * 4.0f,
            time * speed))) * multiplier * 0.25f;
    if (count >= 5.0f)
        elevation -= abs(webgpuTslRagingSeaNoise(float3(
            (position.x + 2.0f) * frequency * 5.0f,
            (position.z + 2.0f) * frequency * 5.0f,
            time * speed))) * multiplier * 0.2f;
    return elevation;
}

/** Evaluates the optimized Schlick Fresnel term used by MeshStandardMaterial. */
float3 webgpuTslRagingSeaFresnel(float3 f0, float dotViewHalf)
{
    const float factor =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates one direct r185 GGX specular BRDF at arbitrary roughness. */
float3 webgpuTslRagingSeaGgx(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float roughness,
    float2 dfgView,
    float2 dfgLight)
{
    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float dotNL = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNV = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotNH = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotVH = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator =
        dotNH * dotNH * (alphaSquared - 1.0f) + 1.0f;
    const float distribution =
        alphaSquared /
        max(3.14159265358979323846f * denominator * denominator, 0.000001f);
    const float visibility =
        0.5f /
        max(
            dotNL * sqrt(alphaSquared + (1.0f - alphaSquared) * dotNV * dotNV) +
            dotNV * sqrt(alphaSquared + (1.0f - alphaSquared) * dotNL * dotNL),
            0.000001f);
    const float3 f0 = float3(0.04f);
    const float3 singleScatter =
        webgpuTslRagingSeaFresnel(f0, dotVH) *
        distribution * visibility;
    const float3 viewEnergy = f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy = f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissing = 1.0f - dfgView.x - dfgView.y;
    const float lightMissing = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multipleScatter =
        viewEnergy * lightEnergy * averageFresnel /
        (float3(1.0f) -
         viewMissing * lightMissing *
             averageFresnel * averageFresnel +
         float3(0.000001f)) *
        (viewMissing * lightMissing);
    return singleScatter + multipleScatter;
}

/** Converts one linear output channel through the r185 sRGB transfer. */
float webgpuTslRagingSeaLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns the signed distance to the locked Inspector rounded rectangle. */
float webgpuTslRagingSeaRoundedBoxDistance(
    float2 pixel,
    float2 minimumPoint,
    float2 maximumPoint,
    float leftRadius,
    float rightRadius)
{
    const float radius =
        pixel.x < (minimumPoint.x + maximumPoint.x) * 0.5f
            ? leftRadius
            : rightRadius;
    const float2 center = (minimumPoint + maximumPoint) * 0.5f;
    const float2 halfExtent = (maximumPoint - minimumPoint) * 0.5f;
    const float2 delta =
        abs(pixel - center) - (halfExtent - float2(radius));
    return length(max(delta, float2(0.0f))) +
        min(max(delta.x, delta.y), 0.0f) - radius;
}

/** Draws the displaced PlaneGeometry with private MeshStandardNodeMaterial semantics. */
class WebgpuTslRagingSeaMainPass final : public IRenderClass
{
public:
    /** Configures one ordinary single-sample opaque indexed draw. */
    constructor(BindGroup<WebgpuTslRagingSeaSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies MaterialX displacement while preserving local position for fragment normals. */
    WebgpuTslRagingSeaVertexOutput vertex(
        WebgpuTslRagingSeaVertex inputValue [[VertexInput0]])
    {
        const float elevation =
            webgpuTslRagingSeaElevation(
                inputValue.position,
                resources->uniforms->cameraPositionAndTime,
                resources->uniforms->emissiveHighPowerShiftIterations,
                resources->uniforms->largeFrequencySpeedMultiplier,
                resources->uniforms->smallFrequencySpeedMultiplier);
        float3 position = inputValue.position + float3(0.0f, elevation, 0.0f);
        const float4 viewPosition = mul(
            resources->uniforms->modelView, float4(position, 1.0f));
        WebgpuTslRagingSeaVertexOutput outputValue;
        outputValue.position = mul(
            resources->uniforms->modelViewProjection, float4(position, 1.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.localPosition = inputValue.position;
        return outputValue;
    }

    /** Evaluates direct standard lighting and the height-remapped emissive node. */
    WebgpuTslRagingSeaSceneFrameBuffer fragment(
        WebgpuTslRagingSeaVertexOutput inputValue)
    {
        const float elevation = webgpuTslRagingSeaElevation(
            inputValue.localPosition,
            resources->uniforms->cameraPositionAndTime,
            resources->uniforms->emissiveHighPowerShiftIterations,
            resources->uniforms->largeFrequencySpeedMultiplier,
            resources->uniforms->smallFrequencySpeedMultiplier);
        const float shift =
            resources->uniforms->emissiveHighPowerShiftIterations.z;
        const float3 displacedPosition =
            inputValue.localPosition + float3(0.0f, elevation, 0.0f);
        float3 positionA =
            inputValue.localPosition + float3(shift, 0.0f, 0.0f);
        float3 positionB =
            inputValue.localPosition + float3(0.0f, 0.0f, -shift);
        positionA.y += webgpuTslRagingSeaElevation(
            positionA,
            resources->uniforms->cameraPositionAndTime,
            resources->uniforms->emissiveHighPowerShiftIterations,
            resources->uniforms->largeFrequencySpeedMultiplier,
            resources->uniforms->smallFrequencySpeedMultiplier);
        positionB.y += webgpuTslRagingSeaElevation(
            positionB,
            resources->uniforms->cameraPositionAndTime,
            resources->uniforms->emissiveHighPowerShiftIterations,
            resources->uniforms->largeFrequencySpeedMultiplier,
            resources->uniforms->smallFrequencySpeedMultiplier);
        const float3 localNormal = normalize(cross(
            normalize(positionA - displacedPosition),
            normalize(positionB - displacedPosition)));
        const float3 normal = normalize(float3(mul(
            resources->uniforms->modelView,
            float4(localNormal, 0.0f)).xyz));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(
                resources->uniforms->lightDirectionAndIntensity.xyz));
        const float dotNL = max(dot(normal, lightDirection), 0.0f);
        const float dotNV = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
        const float roughness =
            resources->uniforms->baseColorAndRoughness.w;
        const float2 dfgView = float4(resources->dfgLut->sample(
            resources->dfgSampler,
            float2(roughness, dotNV))).xy;
        const float2 dfgLight = float4(resources->dfgLut->sample(
            resources->dfgSampler,
            float2(roughness, dotNL))).xy;
        const float3 irradiance = float3(
            dotNL * resources->uniforms->lightDirectionAndIntensity.w);
        const float3 baseColor =
            resources->uniforms->baseColorAndRoughness.xyz;
        const float3 directDiffuse =
            irradiance * baseColor * 0.3183098861837907f;
        const float3 directSpecular = irradiance * webgpuTslRagingSeaGgx(
            normal,
            viewDirection,
            lightDirection,
            roughness,
            dfgView,
            dfgLight);
        const float remapped =
            (elevation -
             resources->uniforms->emissiveHighPowerShiftIterations.x) /
            (resources->uniforms->emissiveColorAndLow.w -
             resources->uniforms->emissiveHighPowerShiftIterations.x);
        const float emissiveWeight = pow(
            clamp(remapped, 0.0f, 1.0f),
            resources->uniforms->emissiveHighPowerShiftIterations.y);
        const float3 linearColor =
            directDiffuse + directSpecular +
            resources->uniforms->emissiveColorAndLow.xyz * emissiveWeight;
        WebgpuTslRagingSeaSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Encodes the Scene and composites the deterministic Inspector chrome. */
class WebgpuTslRagingSeaOutputPass final : public IRenderClass
{
public:
    /** Binds the linear scene image and disables fullscreen culling. */
    constructor(BindGroup<WebgpuTslRagingSeaOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one oversized fullscreen triangle. */
    WebgpuTslRagingSeaScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTslRagingSeaScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Applies the output transfer and exact locked top-right bar colors. */
    WebgpuTslRagingSeaOutputFrameBuffer fragment(
        WebgpuTslRagingSeaScreenOutput inputValue)
    {
        const float3 linearColor = resources->sceneColor->sample(
            resources->linearSampler, inputValue.uv).xyz;
        float3 encoded = float3(
            webgpuTslRagingSeaLinearToSrgb(linearColor.x),
            webgpuTslRagingSeaLinearToSrgb(linearColor.y),
            webgpuTslRagingSeaLinearToSrgb(linearColor.z));
        const float2 pixel = inputValue.uv *
            resources->uniforms->viewportAndInspector.xy;
        const float roundedDistance = webgpuTslRagingSeaRoundedBoxDistance(
            pixel,
            float2(614.0f, 15.0f),
            float2(785.0f, 53.0f),
            12.0f,
            6.0f);
        if (roundedDistance > 0.5f)
        {
            const float shadowDistance = webgpuTslRagingSeaRoundedBoxDistance(
                pixel,
                float2(614.0f, 19.0f),
                float2(785.0f, 57.0f),
                12.0f,
                6.0f);
            const float shadowAlpha =
                0.13f *
                exp(-max(shadowDistance, 0.0f) *
                    max(shadowDistance, 0.0f) / 72.0f);
            encoded *= 1.0f - shadowAlpha;
        }
        else
        {
            float3 inspectorColor =
                float3(30.0f, 30.0f, 36.0f) / 255.0f;
            float inspectorAlpha = 0.85f;
            if (pixel.x < 663.0f)
            {
                inspectorColor =
                    float3(23.1818f, 61.8182f, 85.7727f) / 255.0f;
                inspectorAlpha = 0.88f;
            }
            if (roundedDistance > -1.0f)
            {
                inspectorColor =
                    float3(46.1f, 46.1f, 55.8f) / 255.0f;
                inspectorAlpha = 0.899f;
            }
            inspectorAlpha *= clamp(
                0.5f - roundedDistance, 0.0f, 1.0f);
            encoded = encoded * (1.0f - inspectorAlpha) +
                inspectorColor * inspectorAlpha;
        }
        WebgpuTslRagingSeaOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated ordinary Scene draw and single-sample output chain. */
class WebgpuTslRagingSeaRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuTslRagingSeaVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebgpuTslRagingSeaUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RG16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> dfgLutTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler linearSampler;
    Sampler dfgSampler;
    BindGroup<WebgpuTslRagingSeaSceneResources> sceneResources;
    BindGroup<WebgpuTslRagingSeaOutputResources> outputResources;
    RenderClass<WebgpuTslRagingSeaMainPass> mainPass;
    RenderClass<WebgpuTslRagingSeaOutputPass> outputPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Stores device handles and creates the output sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        linearSampler = device->createSampler({
            .label = "WebgpuTslRagingSeaLinearSampler",
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
            .label = "WebgpuTslRagingSeaDfgSampler",
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
    }

    /** Allocates exact single-sample scene, depth, and readback targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuTslRagingSeaScene", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuTslRagingSeaDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuTslRagingSeaOutput", width, height, 1u);
    }

    /** Uploads the PlaneGeometry, selected scenario state, and dedicated passes. */
    void configureScene(
        const eastl::vector<WebgpuTslRagingSeaVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint> &dfgLutPackedPixels,
        WebgpuTslRagingSeaUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer(
            "WebgpuTslRagingSeaVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebgpuTslRagingSeaIndices", indexCount);
        uniformBuffer = device->createBuffer(
            "WebgpuTslRagingSeaUniforms", 1u);
        dfgLutTexture = device->createTexture(
            "WebgpuTslRagingSeaDfgLut", 16u, 16u, 1u);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(
                BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->writeTexture(
                dfgLutTexture,
                dfgLutPackedPixels.data(),
                uint64_t(dfgLutPackedPixels.size()) * sizeof(uint))
            ->submit();
        sceneResources =
            device->createBindGroup<WebgpuTslRagingSeaSceneResources>(
                uniformBuffer,
                dfgLutTexture->createView(),
                dfgSampler);
        outputResources =
            device->createBindGroup<WebgpuTslRagingSeaOutputResources>(
                sceneTexture->createView(), linearSampler, uniformBuffer);
        mainPass = device->createRenderClass<WebgpuTslRagingSeaMainPass>(
            sceneResources);
        outputPass = device->createRenderClass<WebgpuTslRagingSeaOutputPass>(
            outputResources);
    }

    /** Executes the ordinary indexed Scene draw and output pass. */
    void render() override
    {
        WebgpuTslRagingSeaSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = depthTexture->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuTslRagingSeaOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTslRagingSeaScene",
                sceneFrame,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebgpuTslRagingSeaOutput",
                outputFrame,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture for strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured strict readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured strict readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases all dedicated ordinary-Scene resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(dfgLutTexture);
        device->freeTexture(sceneTexture);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
