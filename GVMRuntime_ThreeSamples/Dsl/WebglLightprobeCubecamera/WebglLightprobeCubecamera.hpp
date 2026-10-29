#pragma once

#include "WebglLightprobeCubecameraData.hpp"

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the helper projection, model-view transform, and world normal transform. */
struct WebglLightprobeCubecameraObjectData
{
    float4x4 projection;
    float4x4 modelView;
    float4x4 normalWorld;
};

/** Stores the mandatory identity instance record. */
struct WebglLightprobeCubecameraInstanceData
{
    float4 reserved;
};

/** Stores the LightProbe intensity and semantic material phase. */
struct WebglLightprobeCubecameraMaterialData
{
    float4 intensityAndPhase;
};

/** Stores one spherical-harmonics coefficient as an aligned component element. */
struct WebglLightprobeCubecameraShData
{
    float4 coefficient;
};

/** Defines the sole RenderSet owned by the logical helper Scene. */
struct WebglLightprobeCubecameraSceneRenderSet : public IRenderSet
{
    /** Declares exact geometry and all entity-local probe components. */
    constructor(
        BufferComponent<WebglLightprobeCubecameraVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLightprobeCubecameraObjectData> objects,
        BufferComponent<WebglLightprobeCubecameraInstanceData> instances,
        BufferComponent<WebglLightprobeCubecameraMaterialData> materials,
        BufferComponent<WebglLightprobeCubecameraShData> sphericalHarmonics)
    {
    }
};

/** Stores the camera basis used to reconstruct the infinite cube background. */
struct WebglLightprobeCubecameraBackgroundUniforms
{
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Binds six explicit cube faces and the camera reconstruction state. */
struct WebglLightprobeCubecameraBackgroundResources final : public IBindGroup
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
        UniformBuffer<WebglLightprobeCubecameraBackgroundUniforms> uniforms [[Binding7]])
    {
    }
};

/** Carries one reconstructed background coordinate. */
struct WebglLightprobeCubecameraScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Carries the helper world normal and entity identity. */
struct WebglLightprobeCubecameraHelperOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the final single-sample Scene color and depth attachments. */
struct WebglLightprobeCubecameraFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear working-space channel to the r185 sRGB output transfer. */
float webglLightprobeCubecameraLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Selects and samples one explicit cube face with the r185 WebGL coordinate convention. */
float3 webglLightprobeCubecameraSampleCube(
    IN BindGroup<WebglLightprobeCubecameraBackgroundResources> resources,
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

/** Draws the six-face Pisa background as a geometry-free screen pass. */
class WebglLightprobeCubecameraBackgroundPass final : public IRenderClass
{
public:
    /** Binds the six source faces and disables geometry depth writes. */
    constructor(
        BindGroup<WebglLightprobeCubecameraBackgroundResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglLightprobeCubecameraScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglLightprobeCubecameraScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the camera ray and emits display-encoded cube radiance. */
    WebglLightprobeCubecameraFrameBuffer fragment(
        WebglLightprobeCubecameraScreenOutput inputValue)
    {
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float3 direction = normalize(
            resources->uniforms->cameraForwardAndReserved.xyz +
            resources->uniforms->cameraRightAndTanHalfFov.xyz *
                (ndc.x * resources->uniforms->cameraUpAndAspect.w *
                 resources->uniforms->cameraRightAndTanHalfFov.w) +
            resources->uniforms->cameraUpAndAspect.xyz *
                (ndc.y * resources->uniforms->cameraRightAndTanHalfFov.w));
        const float3 linearColor = webglLightprobeCubecameraSampleCube(
            resources, direction);
        WebglLightprobeCubecameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglLightprobeCubecameraLinearToSrgb(linearColor.x),
                webglLightprobeCubecameraLinearToSrgb(linearColor.y),
                webglLightprobeCubecameraLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the sole helper entity through the Scene's unique RenderSet. */
class WebglLightprobeCubecameraSceneMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and ordinary opaque depth state. */
    constructor(
        RenderSet<WebglLightprobeCubecameraSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects the exact SphereGeometry helper and exports its world normal. */
    WebglLightprobeCubecameraHelperOutput vertex(
        WebglLightprobeCubecameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLightprobeCubecameraObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLightprobeCubecameraInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglLightprobeCubecameraHelperOutput outputValue;
        const float4 viewPosition = mul(objectData.modelView, inputValue.position);
        outputValue.position = mul(objectData.projection, viewPosition);
        outputValue.worldNormal = normalize(
            mul(objectData.normalWorld, float4(
                inputValue.normal.x,
                inputValue.normal.y,
                -inputValue.normal.z,
                0.0f)).xyz +
            instanceData.reserved.xyz * 0.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the nine-coefficient irradiance polynomial used by LightProbeHelper. */
    WebglLightprobeCubecameraFrameBuffer fragment(
        WebglLightprobeCubecameraHelperOutput inputValue)
    {
        const float3 normal = normalize(inputValue.worldNormal);
        const float x = normal.x;
        const float y = normal.y;
        const float z = normal.z;
        float3 irradiance =
            sceneSet->sphericalHarmonics->get(inputValue.entityID, 0u).coefficient.xyz *
            0.886227f;
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 1u).coefficient.xyz *
            (2.0f * 0.511664f * y);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 2u).coefficient.xyz *
            (2.0f * 0.511664f * z);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 3u).coefficient.xyz *
            (2.0f * 0.511664f * x);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 4u).coefficient.xyz *
            (2.0f * 0.429043f * x * y);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 5u).coefficient.xyz *
            (2.0f * 0.429043f * y * z);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 6u).coefficient.xyz *
            (0.743125f * z * z - 0.247708f);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 7u).coefficient.xyz *
            (2.0f * 0.429043f * x * z);
        irradiance += sceneSet->sphericalHarmonics->get(inputValue.entityID, 8u).coefficient.xyz *
            (0.429043f * (x * x - y * y));
        const WebglLightprobeCubecameraMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 linearColor = max(
            irradiance * (0.318309886f * material.intensityAndPhase.x),
            float3(0.0f));
        WebglLightprobeCubecameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglLightprobeCubecameraLinearToSrgb(linearColor.x),
                webglLightprobeCubecameraLinearToSrgb(linearColor.y),
                webglLightprobeCubecameraLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated cube background and unique helper Scene RenderSet pipeline. */
class WebglLightprobeCubecameraRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLightprobeCubecameraSceneRenderSet> sceneSet;
    Buffer<WebglLightprobeCubecameraBackgroundUniforms, BufferUsage<Uniform, CopyDst>>
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
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Sampler environmentSampler;
    BindGroup<WebglLightprobeCubecameraBackgroundResources> backgroundResources;
    RenderClass<WebglLightprobeCubecameraBackgroundPass> backgroundPass;
    RenderClass<WebglLightprobeCubecameraSceneMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Set and immutable sampler for ordinary single-sample output. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLightprobeCubecameraSceneRenderSet>();
        backgroundUniformBuffer = device->createBuffer(
            "WebglLightprobeCubecameraBackgroundUniforms", 1u);
        environmentSampler = device->createSampler({
            .label = "WebglLightprobeCubecameraEnvironmentSampler",
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
            "WebglLightprobeCubecameraOutput", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglLightprobeCubecameraDepth", width, height, 1u);
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
            "WebglLightprobeCubecameraPositiveX", 256u, 256u, 1u, 9u);
        negativeX = device->createTexture(
            "WebglLightprobeCubecameraNegativeX", 256u, 256u, 1u, 9u);
        positiveY = device->createTexture(
            "WebglLightprobeCubecameraPositiveY", 256u, 256u, 1u, 9u);
        negativeY = device->createTexture(
            "WebglLightprobeCubecameraNegativeY", 256u, 256u, 1u, 9u);
        positiveZ = device->createTexture(
            "WebglLightprobeCubecameraPositiveZ", 256u, 256u, 1u, 9u);
        negativeZ = device->createTexture(
            "WebglLightprobeCubecameraNegativeZ", 256u, 256u, 1u, 9u);
        WebglLightprobeCubecameraBackgroundUniforms uniforms;
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
        backgroundResources =
            device->createBindGroup<WebglLightprobeCubecameraBackgroundResources>(
                positiveX->createView(), negativeX->createView(),
                positiveY->createView(), negativeY->createView(),
                positiveZ->createView(), negativeZ->createView(),
                environmentSampler, backgroundUniformBuffer);
        backgroundPass =
            device->createRenderClass<WebglLightprobeCubecameraBackgroundPass>(
                backgroundResources);
        mainPass =
            device->createRenderClass<WebglLightprobeCubecameraSceneMainPass>(sceneSet);
    }

    /** Draws the screen background then the helper through Set-only indirect draw. */
    void render() override
    {
        sceneSet->update();
        WebglLightprobeCubecameraFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = outputColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrameBuffer.depth = sceneDepth->createView();
        backgroundFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        backgroundFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLightprobeCubecameraFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = outputColor->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Load;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.depth = sceneDepth->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglLightprobeCubecameraBackground",
                backgroundFrameBuffer,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglLightprobeCubecameraSceneMain",
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
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};
