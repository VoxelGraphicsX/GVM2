#pragma once

#include "WebgpuLightprobeData.hpp"

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the helper projection, model-view transform, and world normal transform. */
struct WebgpuLightprobeObjectData
{
    float4x4 projection;
    float4x4 modelView;
    float4x4 normalWorld;
};

/** Stores the mandatory identity instance record. */
struct WebgpuLightprobeInstanceData
{
    float4 reserved;
};

/** Stores the LightProbe intensity and semantic material phase. */
struct WebgpuLightprobeMaterialData
{
    float4 intensityAndPhase;
};

/** Stores all nine spherical-harmonics coefficients in one entity component record. */
struct WebgpuLightprobeShData
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
struct WebgpuLightprobeSceneRenderSet : public IRenderSet
{
    /** Declares exact geometry and all entity-local probe components. */
    constructor(
        BufferComponent<WebgpuLightprobeVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuLightprobeObjectData> objects,
        BufferComponent<WebgpuLightprobeInstanceData> instances,
        BufferComponent<WebgpuLightprobeMaterialData> materials,
        BufferComponent<WebgpuLightprobeShData> sphericalHarmonics)
    {
    }
};

/** Stores the camera basis used to reconstruct the infinite cube background. */
struct WebgpuLightprobeBackgroundUniforms
{
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Binds six explicit cube faces and the camera reconstruction state. */
struct WebgpuLightprobeBackgroundResources final : public IBindGroup
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
        UniformBuffer<WebgpuLightprobeBackgroundUniforms> uniforms [[Binding7]],
        Texture2D<half4> dfgLut [[Binding8]])
    {
    }
};

/** Carries one reconstructed background coordinate. */
struct WebgpuLightprobeScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Carries one Scene entity's view position, world normal, and identity. */
struct WebgpuLightprobeSceneOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the final single-sample Scene color and depth attachments. */
struct WebgpuLightprobeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear working-space channel to the r185 sRGB output transfer. */
float webgpuLightprobeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Selects and samples one explicit cube face with the r185 WebGL coordinate convention. */
float3 webgpuLightprobeSampleCube(
    IN BindGroup<WebgpuLightprobeBackgroundResources> resources,
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
        linearColor = resources->positiveX->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    else if (face == 1u)
    {
        linearColor = resources->negativeY->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    else if (face == 2u)
    {
        linearColor = resources->positiveZ->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    else if (face == 3u)
    {
        linearColor = resources->negativeX->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    else if (face == 4u)
    {
        linearColor = resources->positiveY->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    else
    {
        linearColor = resources->negativeZ->sampleLevel(
            resources->environmentSampler, uv, 0.0f).xyz;
    }
    return linearColor;
}

/** Draws the six-face Pisa background as a geometry-free screen pass. */
class WebgpuLightprobeBackgroundPass final : public IRenderClass
{
public:
    /** Binds the six source faces and disables geometry depth writes. */
    constructor(
        BindGroup<WebgpuLightprobeBackgroundResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuLightprobeScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuLightprobeScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray and emits display-encoded cube radiance. */
    WebgpuLightprobeFrameBuffer fragment(
        WebgpuLightprobeScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float3 direction = normalize(
            resources->uniforms->cameraForwardAndReserved.xyz +
            resources->uniforms->cameraRightAndTanHalfFov.xyz *
                (ndc.x * resources->uniforms->cameraUpAndAspect.w *
                 resources->uniforms->cameraRightAndTanHalfFov.w) +
            resources->uniforms->cameraUpAndAspect.xyz *
                (ndc.y * resources->uniforms->cameraRightAndTanHalfFov.w));
        const float3 linearColor = webgpuLightprobeSampleCube(
            resources, direction);
        WebgpuLightprobeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuLightprobeLinearToSrgb(linearColor.x),
                webgpuLightprobeLinearToSrgb(linearColor.y),
                webgpuLightprobeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the sole helper entity through the Scene's unique RenderSet. */
class WebgpuLightprobeSceneMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and ordinary opaque depth state. */
    constructor(
        RenderSet<WebgpuLightprobeSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuLightprobeBackgroundResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects either packed SphereGeometry entity and exports lighting inputs. */
    WebgpuLightprobeSceneOutput vertex(
        WebgpuLightprobeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuLightprobeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuLightprobeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebgpuLightprobeSceneOutput outputValue;
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
    WebgpuLightprobeFrameBuffer fragment(
        WebgpuLightprobeSceneOutput inputValue)
    {
        const float3 normal = normalize(inputValue.worldNormal);
        const float x = normal.x;
        const float y = normal.y;
        const float z = normal.z;
        const WebgpuLightprobeShData sphericalHarmonics =
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
        const WebgpuLightprobeMaterialData material =
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
            const float2 dfgView = float4(resources->dfgLut->sample(
                resources->environmentSampler,
                float2(roughness, dotNV))).xy;
            const float2 dfgLight = float4(resources->dfgLut->sample(
                resources->environmentSampler,
                float2(roughness, dotNL))).xy;
            const float3 viewEnergy = float3(0.04f) * dfgView.x +
                float3(dfgView.y);
            const float3 lightEnergy = float3(0.04f) * dfgLight.x +
                float3(dfgLight.y);
            const float viewMissing = 1.0f - dfgView.x - dfgView.y;
            const float lightMissing = 1.0f - dfgLight.x - dfgLight.y;
            const float3 averageFresnel = float3(0.04f) +
                (float3(1.0f) - float3(0.04f)) * 0.047619f;
            const float3 multipleScattering =
                viewEnergy * lightEnergy * averageFresnel /
                (float3(1.0f) - viewMissing * lightMissing *
                    averageFresnel * averageFresnel + float3(0.000001f)) *
                viewMissing * lightMissing;
            const float3 directSpecularWithCompensation =
                directSpecular + multipleScattering;
            const float3 directDiffuse = float3(0.318309886f);
            const float3 directLighting =
                (directDiffuse + directSpecularWithCompensation) *
                (dotNL * material.intensityAndPhase.y);
            const float3 indirectDiffuse = irradiance * 0.318309886f *
                material.intensityAndPhase.x;
            const float3 diffuseEnvironment = irradiance * 0.318309886f *
                material.intensityAndPhase.z;
            const float3 reflectedDirectionValue = reflect(-viewDirection, normal);
            const float3 reflectedDirection = float3(
                reflectedDirectionValue.x,
                -reflectedDirectionValue.y,
                reflectedDirectionValue.z);
            const float3 environmentRadiance = webgpuLightprobeSampleCube(
                resources, reflectedDirection);
            const float3 singleScatter = float3(0.04f) * dfgView.x +
                float3(dfgView.y);
            const float energySum = dfgView.x + dfgView.y;
            const float energyMissing = 1.0f - energySum;
            const float3 multiScatter = singleScatter * averageFresnel /
                (float3(1.0f) - energyMissing * averageFresnel +
                    float3(0.000001f)) * energyMissing;
            const float3 cosineWeightedIrradiance = irradiance *
                (0.318309886f * material.intensityAndPhase.z);
            const float3 indirectSpecular =
                environmentRadiance * singleScatter *
                    material.intensityAndPhase.z +
                cosineWeightedIrradiance * multiScatter;
            linearColor = max(
                directLighting + indirectDiffuse + diffuseEnvironment +
                    indirectSpecular,
                float3(0.0f));
        }
        WebgpuLightprobeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuLightprobeLinearToSrgb(linearColor.x),
                webgpuLightprobeLinearToSrgb(linearColor.y),
                webgpuLightprobeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated cube background and unique helper Scene RenderSet pipeline. */
class WebgpuLightprobeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuLightprobeSceneRenderSet> sceneSet;
    Buffer<WebgpuLightprobeBackgroundUniforms, BufferUsage<Uniform, CopyDst>>
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
    Texture<TextureFormat::RG16Float,
            TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> dfgLut;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Sampler environmentSampler;
    BindGroup<WebgpuLightprobeBackgroundResources> backgroundResources;
    RenderClass<WebgpuLightprobeBackgroundPass> backgroundPass;
    RenderClass<WebgpuLightprobeSceneMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Set and immutable sampler for ordinary single-sample output. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuLightprobeSceneRenderSet>();
        backgroundUniformBuffer = device->createBuffer(
            "WebgpuLightprobeBackgroundUniforms", 1u);
        environmentSampler = device->createSampler({
            .label = "WebgpuLightprobeEnvironmentSampler",
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
            "WebgpuLightprobeOutput", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuLightprobeDepth", width, height, 1u);
    }

    /** Uploads all six explicit mip chains and creates the two dedicated passes. */
    void configureEnvironment(
        const eastl::vector<eastl::vector<uint8_t>> &positiveXMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeXMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveYMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeYMips,
        const eastl::vector<eastl::vector<uint8_t>> &positiveZMips,
        const eastl::vector<eastl::vector<uint8_t>> &negativeZMips,
        const eastl::vector<uint> &dfgLutPackedPixels,
        float4 cameraRightAndTanHalfFov,
        float4 cameraUpAndAspect,
        float4 cameraForwardAndReserved)
    {
        positiveX = device->createTexture(
            "WebgpuLightprobePositiveX", 256u, 256u, 1u, 9u);
        negativeX = device->createTexture(
            "WebgpuLightprobeNegativeX", 256u, 256u, 1u, 9u);
        positiveY = device->createTexture(
            "WebgpuLightprobePositiveY", 256u, 256u, 1u, 9u);
        negativeY = device->createTexture(
            "WebgpuLightprobeNegativeY", 256u, 256u, 1u, 9u);
        positiveZ = device->createTexture(
            "WebgpuLightprobePositiveZ", 256u, 256u, 1u, 9u);
        negativeZ = device->createTexture(
            "WebgpuLightprobeNegativeZ", 256u, 256u, 1u, 9u);
        dfgLut = device->createTexture(
            "WebgpuLightprobeDfgLut", 16u, 16u, 1u);
        WebgpuLightprobeBackgroundUniforms uniforms;
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
        graphicsQueue
            ->writeTexture(
                dfgLut,
                dfgLutPackedPixels.data(),
                uint64_t(dfgLutPackedPixels.size()) * sizeof(uint))
            ->submit();
        backgroundResources =
            device->createBindGroup<WebgpuLightprobeBackgroundResources>(
                positiveX->createView(), negativeX->createView(),
                positiveY->createView(), negativeY->createView(),
                positiveZ->createView(), negativeZ->createView(),
                environmentSampler, backgroundUniformBuffer,
                dfgLut->createView());
        backgroundPass =
            device->createRenderClass<WebgpuLightprobeBackgroundPass>(
                backgroundResources);
        mainPass =
            device->createRenderClass<WebgpuLightprobeSceneMainPass>(
                sceneSet, backgroundResources);
    }

    /** Draws the screen background then the helper through Set-only indirect draw. */
    void render() override
    {
        sceneSet->update();
        WebgpuLightprobeFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = outputColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrameBuffer.depth = sceneDepth->createView();
        backgroundFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        backgroundFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuLightprobeFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = outputColor->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Load;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.depth = sceneDepth->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuLightprobeBackground",
                backgroundFrameBuffer,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuLightprobeSceneMain",
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
        device->freeTexture(dfgLut);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};
