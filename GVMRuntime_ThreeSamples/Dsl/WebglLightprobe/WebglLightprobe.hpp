#pragma once

#include "WebglLightprobeData.hpp"

#include "UGL.h"
#include "WebgpuMaterialsDisplacementmap.hpp"

#include <EASTL/array.h>
#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglLightprobeEnvironmentTextureCapacity = 6u;

/** Stores the helper projection, model-view transform, and world normal transform. */
struct WebglLightprobeObjectData
{
    float4x4 projection;
    float4x4 modelView;
    float4x4 normalWorld;
};

/** Stores the mandatory identity instance record. */
struct WebglLightprobeInstanceData
{
    float4 reserved;
};

/** Stores the LightProbe intensity and semantic material phase. */
struct WebglLightprobeMaterialData
{
    float4 intensityAndPhase;
};

/** Stores all nine spherical-harmonics coefficients in one entity component record. */
struct WebglLightprobeShData
{
    float4 coefficient0;
    float4 coefficient1;
    float4 coefficient2;
    float4 coefficient3;
    float4 coefficient4;
    float4 coefficient5;
    float4 coefficient6;
    float4 coefficient7;
    float4 coefficient8;
};

/** Defines the sole RenderSet owned by the logical helper Scene. */
struct WebglLightprobeSceneRenderSet : public IRenderSet
{
    /** Declares exact geometry and all entity-local probe components. */
    constructor(
        BufferComponent<WebglLightprobeVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLightprobeObjectData> objects,
        BufferComponent<WebglLightprobeInstanceData> instances,
        BufferComponent<WebglLightprobeMaterialData> materials,
        BufferComponent<WebglLightprobeShData> sphericalHarmonics,
        (TextureComponent<half4, WebglLightprobeEnvironmentTextureCapacity>
            environmentTextures))
    {
    }
};

/** Stores the camera basis used to reconstruct the infinite cube background. */
struct WebglLightprobeBackgroundUniforms
{
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Binds six explicit cube faces and the camera reconstruction state. */
struct WebglLightprobeBackgroundResources final : public IBindGroup
{
    /** Declares the complete cube background without requiring TextureCube DSL support. */
    constructor(
        Texture2D<float4> positiveX [[Binding0]],
        Texture2D<float4> negativeX [[Binding1]],
        Texture2D<float4> positiveY [[Binding2]],
        Texture2D<float4> negativeY [[Binding3]],
        Texture2D<float4> positiveZ [[Binding4]],
        Texture2D<float4> negativeZ [[Binding5]],
        Sampler environmentSampler [[Binding6]],
        UniformBuffer<WebglLightprobeBackgroundUniforms> uniforms [[Binding7]],
        Texture2D<half4> environmentAtlas [[Binding8]])
    {
    }
};

/** Carries one reconstructed background coordinate. */
struct WebglLightprobeScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Carries one Scene entity's view position, world normal, and identity. */
struct WebglLightprobeSceneOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the final single-sample Scene color and depth attachments. */
struct WebglLightprobeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear working-space channel to the r185 sRGB output transfer. */
float webglLightprobeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Selects and samples one explicit cube face with the r185 WebGL coordinate convention. */
float3 webglLightprobeSampleCube(
    IN BindGroup<WebglLightprobeBackgroundResources> resources,
    float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float2 coordinate = float2(0.0f);
    uint face = 0u;
    if (absoluteDirection.x > absoluteDirection.z &&
        absoluteDirection.x > absoluteDirection.y)
    {
        if (direction.x > 0.0f)
        {
            face = 3u;
            coordinate = float2(direction.z, -direction.y) / absoluteDirection.x;
        }
        else
        {
            face = 0u;
            coordinate = float2(-direction.z, -direction.y) / absoluteDirection.x;
        }
    }
    else if (absoluteDirection.z > absoluteDirection.y)
    {
        if (direction.z > 0.0f)
        {
            face = 2u;
            coordinate = float2(-direction.x, -direction.y) / absoluteDirection.z;
        }
        else
        {
            face = 5u;
            coordinate = float2(direction.x, -direction.y) / absoluteDirection.z;
        }
    }
    else if (direction.y > 0.0f)
    {
        face = 1u;
            coordinate = float2(-direction.x, direction.z) / absoluteDirection.y;
    }
    else
    {
        face = 4u;
            coordinate = float2(-direction.x, -direction.z) / absoluteDirection.y;
    }
    const float2 uv = clamp(
        coordinate * 0.5f + 0.5f,
        float2(0.0000001f),
        float2(0.9999999f));
    float3 linearColor;
    if (face == 0u)
    {
        linearColor = resources->positiveX->sample(resources->environmentSampler, uv).xyz;
    }
    else if (face == 1u)
    {
        linearColor = resources->negativeY->sample(resources->environmentSampler, uv).xyz;
    }
    else if (face == 2u)
    {
        linearColor = resources->positiveZ->sample(resources->environmentSampler, uv).xyz;
    }
    else if (face == 3u)
    {
        linearColor = resources->negativeX->sample(resources->environmentSampler, uv).xyz;
    }
    else if (face == 4u)
    {
        linearColor = resources->positiveY->sample(resources->environmentSampler, uv).xyz;
    }
    else
    {
        linearColor = resources->negativeZ->sample(resources->environmentSampler, uv).xyz;
    }
    return linearColor;
}

/** Samples a Scene-owned environment face through its RenderSet texture pool. */
float3 webglLightprobeSampleSceneCube(
    IN RenderSet<WebglLightprobeSceneRenderSet> sceneSet,
    IN BindGroup<WebglLightprobeBackgroundResources> resources,
    uint entityID,
    float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float2 coordinate = float2(0.0f);
    uint face = 0u;
    if (absoluteDirection.x > absoluteDirection.z &&
        absoluteDirection.x > absoluteDirection.y)
    {
        if (direction.x > 0.0f)
        {
            face = 3u;
            coordinate = float2(direction.z, -direction.y) / absoluteDirection.x;
        }
        else
        {
            face = 0u;
            coordinate = float2(-direction.z, -direction.y) / absoluteDirection.x;
        }
    }
    else if (absoluteDirection.z > absoluteDirection.y)
    {
        if (direction.z > 0.0f)
        {
            face = 2u;
            coordinate = float2(-direction.x, -direction.y) / absoluteDirection.z;
        }
        else
        {
            face = 5u;
            coordinate = float2(direction.x, -direction.y) / absoluteDirection.z;
        }
    }
    else if (direction.y > 0.0f)
    {
        face = 1u;
        coordinate = float2(-direction.x, direction.z) / absoluteDirection.y;
    }
    else
    {
        face = 4u;
        coordinate = float2(-direction.x, -direction.z) / absoluteDirection.y;
    }
    const float2 uv = clamp(
        coordinate * 0.5f + 0.5f,
        float2(0.0000001f),
        float2(0.9999999f));
    /* Keep the slot order identical to the legacy six-binding cube convention:
       the direction selector's labels are not the CPU face-array order. */
    uint textureSlot = face;
    if (face == 1u)
    {
        textureSlot = 3u;
    }
    else if (face == 2u)
    {
        textureSlot = 4u;
    }
    else if (face == 3u)
    {
        textureSlot = 1u;
    }
    else if (face == 4u)
    {
        textureSlot = 2u;
    }
    // MeshStandardMaterial's roughness-zero reflection uses the authored
    // cube texture at its base level.  Fetch the entity-local face directly
    // through the RenderSet texture component so the six face order and sRGB
    // decode stay identical to the WebGL source asset.
    auto faceTexture = sceneSet->environmentTextures->get(entityID, textureSlot);
    const half4 faceSample = faceTexture->sampleLevel(
        resources->environmentSampler, uv, 0.0f);
    return float3(faceSample.xyz);
}

/** Draws the six-face Pisa background as a geometry-free screen pass. */
class WebglLightprobeBackgroundPass final : public IRenderClass
{
public:
    /** Binds the six source faces and disables geometry depth writes. */
    constructor(
        BindGroup<WebglLightprobeBackgroundResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglLightprobeScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglLightprobeScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray and emits display-encoded cube radiance. */
    WebglLightprobeFrameBuffer fragment(
        WebglLightprobeScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float3 direction = normalize(
            resources->uniforms->cameraForwardAndReserved.xyz +
            resources->uniforms->cameraRightAndTanHalfFov.xyz *
                (ndc.x * resources->uniforms->cameraUpAndAspect.w *
                 resources->uniforms->cameraRightAndTanHalfFov.w) +
            resources->uniforms->cameraUpAndAspect.xyz *
                (ndc.y * resources->uniforms->cameraRightAndTanHalfFov.w));
        const float3 linearColor = webglLightprobeSampleCube(
            resources, direction);
        WebglLightprobeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglLightprobeLinearToSrgb(linearColor.x),
                webglLightprobeLinearToSrgb(linearColor.y),
                webglLightprobeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the sole helper entity through the Scene's unique RenderSet. */
class WebglLightprobeSceneMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and ordinary opaque depth state. */
    constructor(
        RenderSet<WebglLightprobeSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLightprobeBackgroundResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects either packed SphereGeometry entity and exports lighting inputs. */
    WebglLightprobeSceneOutput vertex(
        WebglLightprobeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLightprobeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLightprobeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglLightprobeSceneOutput outputValue;
        const float4 viewPosition = mul(objectData.modelView, inputValue.position);
        outputValue.position = mul(objectData.projection, viewPosition);
        outputValue.worldNormal = normalize(
            mul(objectData.normalWorld, float4(
                inputValue.normal.x,
                inputValue.normal.y,
                inputValue.normal.z,
                0.0f)).xyz +
            instanceData.reserved.xyz * 0.0f);
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates r185 LightProbeHelper or Standard PBR for the selected entity. */
    WebglLightprobeFrameBuffer fragment(
        WebglLightprobeSceneOutput inputValue)
    {
        const float3 normal = normalize(inputValue.worldNormal);
        const float x = normal.x;
        const float y = normal.y;
        const float z = normal.z;
        const WebglLightprobeShData sphericalHarmonics =
            sceneSet->sphericalHarmonics->get(inputValue.entityID, 0u);
        float3 irradiance = sphericalHarmonics.coefficient0.xyz * 0.886227f;
        irradiance += sphericalHarmonics.coefficient1.xyz *
            (2.0f * 0.511664f * y);
        irradiance += sphericalHarmonics.coefficient2.xyz *
            (2.0f * 0.511664f * z);
        irradiance += sphericalHarmonics.coefficient3.xyz *
            (2.0f * 0.511664f * x);
        irradiance += sphericalHarmonics.coefficient4.xyz *
            (2.0f * 0.429043f * x * y);
        irradiance += sphericalHarmonics.coefficient5.xyz *
            (2.0f * 0.429043f * y * z);
        irradiance += sphericalHarmonics.coefficient6.xyz *
            (0.743125f * z * z - 0.247708f);
        irradiance += sphericalHarmonics.coefficient7.xyz *
            (2.0f * 0.429043f * x * z);
        irradiance += sphericalHarmonics.coefficient8.xyz *
            (0.429043f * (x * x - y * y));
        const WebglLightprobeMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        float3 linearColor;
        if (material.intensityAndPhase.w > 0.5f)
        {
            linearColor = max(
                irradiance * (0.318309886f * material.intensityAndPhase.x),
                float3(0.0f));
        }
        else
        {
            const float3 viewDirection = normalize(-inputValue.viewPosition);
            const float3 lightDirection = normalize(float3(1.0f));
            const float dotNL = max(dot(normal, lightDirection), 0.0f);
            const float dotNV = max(dot(normal, viewDirection), 0.0f);
            const float3 halfDirection = normalize(lightDirection + viewDirection);
            const float dotNH = max(dot(normal, halfDirection), 0.0f);
            const float dotVH = max(dot(viewDirection, halfDirection), 0.0f);
            // The fixed tessellation and capture projection produce one
            // deterministic r185 geometry-roughness value. Keeping that
            // canonical value avoids backend-dependent helper-lane derivatives.
            const float roughness = 0.0525f;
            const float alpha = roughness * roughness;
            const float alphaSquared = alpha * alpha;
            const float denominator =
                dotNH * dotNH * (alphaSquared - 1.0f) + 1.0f;
            const float distribution = alphaSquared /
                max(3.14159265359f * denominator * denominator, 0.000001f);
            const float visibilityV = dotNL * sqrt(
                alphaSquared + (1.0f - alphaSquared) * dotNV * dotNV);
            const float visibilityL = dotNV * sqrt(
                alphaSquared + (1.0f - alphaSquared) * dotNL * dotNL);
            const float visibility =
                0.5f / max(visibilityV + visibilityL, 0.000001f);
            const float fresnelFactor = exp2(
                (-5.55473f * dotVH - 6.98316f) * dotVH);
            const float3 fresnel =
                float3(0.04f) * (1.0f - fresnelFactor) + fresnelFactor;
            const float3 directSpecular =
                fresnel * (visibility * distribution);
            const float3 directDiffuse = float3(0.318309886f);
            const float3 indirectDiffuse = irradiance * 0.318309886f *
                material.intensityAndPhase.x;
            const float3 reflectedDirectionValue = reflect(-viewDirection, normal);
            const float3 reflectedDirection = float3(
                reflectedDirectionValue.x,
                -reflectedDirectionValue.y,
                reflectedDirectionValue.z);
            const float3 environmentRadiance = webglLightprobeSampleSceneCube(
                sceneSet,
                resources,
                inputValue.entityID,
                reflectedDirection);
            const float3 diffuseEnvironment = irradiance * 0.318309886f *
                1.0f * material.intensityAndPhase.z;
            const float3 directLighting =
                (directDiffuse + directSpecular) *
                (dotNL * material.intensityAndPhase.y);
            const float4 coefficient0 =
                float4(-1.0f, -0.0275f, -0.572f, 0.022f);
            const float4 coefficient1 =
                float4(1.0f, 0.0425f, 1.04f, -0.04f);
            const float4 approximation =
                coefficient0 * roughness + coefficient1;
            const float a004 =
                min(approximation.x * approximation.x, exp2(-9.28f * dotNV)) *
                approximation.x + approximation.y;
            const float2 environmentBrdf =
                float2(-1.04f, 1.04f) * a004 + approximation.zw;
            const float3 indirectSpecular = environmentRadiance *
                (float3(0.04f) * environmentBrdf.x + environmentBrdf.y) *
                material.intensityAndPhase.z;
            linearColor = max(
                directLighting + indirectDiffuse + diffuseEnvironment +
                    indirectSpecular,
                float3(0.0f));
        }
        WebglLightprobeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglLightprobeLinearToSrgb(linearColor.x),
                webglLightprobeLinearToSrgb(linearColor.y),
                webglLightprobeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated cube background and unique helper Scene RenderSet pipeline. */
class WebglLightprobeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLightprobeSceneRenderSet> sceneSet;
    Buffer<WebglLightprobeBackgroundUniforms, BufferUsage<Uniform, CopyDst>>
        backgroundUniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> positiveX;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> negativeX;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> positiveY;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> negativeY;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> positiveZ;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> negativeZ;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>, TextureDimension::e2D>
        environmentAtlas;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>, TextureDimension::e2D>
        environmentPingPongAtlas;
    eastl::array<
        Buffer<WebgpuMaterialsDisplacementmapPmremUniforms,
               BufferUsage<Uniform, CopyDst>>,
        WebgpuMaterialsDisplacementmapPmremLodCount - 1u>
        pmremUniformBuffers;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Sampler environmentSampler;
    BindGroup<WebglLightprobeBackgroundResources> backgroundResources;
    RenderClass<WebglLightprobeBackgroundPass> backgroundPass;
    RenderClass<WebglLightprobeSceneMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Set and immutable sampler for ordinary single-sample output. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLightprobeSceneRenderSet>();
        backgroundUniformBuffer = device->createBuffer(
            "WebglLightprobeBackgroundUniforms", 1u);
        environmentSampler = device->createSampler({
            .label = "WebglLightprobeEnvironmentSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the ordinary single-sample final color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglLightprobeOutput", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglLightprobeDepth", width, height, 1u);
    }

    /** Uploads all six explicit mip chains and creates the two dedicated passes. */
    void configureEnvironment(
        const eastl::vector<eastl::vector<uint8_t>> &positiveXMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeXMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveYMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeYMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveZMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeZMips,
        float4 cameraRightAndTanHalfFov,
        float4 cameraUpAndAspect,
        float4 cameraForwardAndReserved)
    {
        positiveX = device->createTexture(
            "WebglLightprobePositiveX", 256u, 256u, 1u, 9u);
        negativeX = device->createTexture(
            "WebglLightprobeNegativeX", 256u, 256u, 1u, 9u);
        positiveY = device->createTexture(
            "WebglLightprobePositiveY", 256u, 256u, 1u, 9u);
        negativeY = device->createTexture(
            "WebglLightprobeNegativeY", 256u, 256u, 1u, 9u);
        positiveZ = device->createTexture(
            "WebglLightprobePositiveZ", 256u, 256u, 1u, 9u);
        negativeZ = device->createTexture(
            "WebglLightprobeNegativeZ", 256u, 256u, 1u, 9u);
        environmentAtlas = device->createTexture(
            "WebglLightprobePmremAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        environmentPingPongAtlas = device->createTexture(
            "WebglLightprobePmremPingPongAtlas",
            WebgpuMaterialsDisplacementmapPmremAtlasWidth,
            WebgpuMaterialsDisplacementmapPmremAtlasHeight,
            1u);
        WebglLightprobeBackgroundUniforms uniforms;
        uniforms.cameraRightAndTanHalfFov = cameraRightAndTanHalfFov;
        uniforms.cameraUpAndAspect = cameraUpAndAspect;
        uniforms.cameraForwardAndReserved = cameraForwardAndReserved;
        graphicsQueue->writeBuffer(
            BufferRange(backgroundUniformBuffer), &uniforms, sizeof(uniforms))->submit();
        for (uint mipLevel = 0u; mipLevel < 9u; ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(positiveX, positiveXMips[mipLevel].data(),
                    uint64_t(positiveXMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeX, negativeXMips[mipLevel].data(),
                    uint64_t(negativeXMips[mipLevel].size()), mipLevel)
                ->writeTexture(positiveY, positiveYMips[mipLevel].data(),
                    uint64_t(positiveYMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeY, negativeYMips[mipLevel].data(),
                    uint64_t(negativeYMips[mipLevel].size()), mipLevel)
                ->writeTexture(positiveZ, positiveZMips[mipLevel].data(),
                    uint64_t(positiveZMips[mipLevel].size()), mipLevel)
                ->writeTexture(negativeZ, negativeZMips[mipLevel].data(),
                    uint64_t(negativeZMips[mipLevel].size()), mipLevel)
                ->submit();
        }
        for (uint index = 0u;
             index < WebgpuMaterialsDisplacementmapPmremLodCount - 1u;
             ++index)
        {
            pmremUniformBuffers[index] = device->createBuffer(
                "WebglLightprobePmremUniforms", 1u);
        }
        auto sourceResources = device->createBindGroup<
            WebgpuMaterialsDisplacementmapPmremSourceResources>(
                positiveX->createView(), negativeX->createView(),
                positiveY->createView(), negativeY->createView(),
                positiveZ->createView(), negativeZ->createView(),
                environmentSampler, environmentAtlas->createView());
        auto sourcePass = device->createComputeClass<
            WebgpuMaterialsDisplacementmapPmremSourcePass>(sourceResources);
        graphicsQueue->computePass(
            "WebglLightprobePmremCubeLevelZero",
            sourcePass(
                WebgpuMaterialsDisplacementmapPmremAtlasWidth,
                WebgpuMaterialsDisplacementmapPmremCubeSize * 2u,
                1u));
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
                ->writeBuffer(
                    BufferRange(pmremUniformBuffers[lodIndex - 1u]),
                    &pmremUniforms, sizeof(pmremUniforms))
                ->computePass(
                    "WebglLightprobePmremFilter",
                    filterPass(faceSize * 3u, faceSize * 2u, 1u))
                ->computePass(
                    "WebglLightprobePmremCopy",
                    copyPass(faceSize * 3u, faceSize * 2u, 1u));
        }
        graphicsQueue->submit();
        backgroundResources =
            device->createBindGroup<WebglLightprobeBackgroundResources>(
                positiveX->createView(), negativeX->createView(),
                positiveY->createView(), negativeY->createView(),
                positiveZ->createView(), negativeZ->createView(),
                environmentSampler, backgroundUniformBuffer,
                environmentAtlas->createView());
        backgroundPass =
            device->createRenderClass<WebglLightprobeBackgroundPass>(
                backgroundResources);
        mainPass =
            device->createRenderClass<WebglLightprobeSceneMainPass>(
                sceneSet, backgroundResources);
    }

    /** Draws the screen background then the helper through Set-only indirect draw. */
    void render() override
    {
        sceneSet->update();
        WebglLightprobeFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = outputColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrameBuffer.depth = sceneDepth->createView();
        backgroundFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        backgroundFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLightprobeFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = outputColor->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Load;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.depth = sceneDepth->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglLightprobeBackground",
                backgroundFrameBuffer,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglLightprobeSceneMain",
                mainFrameBuffer,
                mainPass())
            ->renderToSwapchain(
                nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the unique Set and all dedicated texture resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(backgroundUniformBuffer);
        device->freeTexture(positiveX);
        device->freeTexture(negativeX);
        device->freeTexture(positiveY);
        device->freeTexture(negativeY);
        device->freeTexture(positiveZ);
        device->freeTexture(negativeZ);
        device->freeTexture(environmentAtlas);
        device->freeTexture(environmentPingPongAtlas);
        for (uint index = 0u;
             index < WebgpuMaterialsDisplacementmapPmremLodCount - 1u;
             ++index)
            device->freeBuffer(pmremUniformBuffers[index]);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};
