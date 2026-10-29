#pragma once

#include "UGL.h"

using namespace UGL;


/** Stores one already projected array-camera vertex and its world-space lighting data. */
struct WebglCameraArrayVertex
{
    float4 clipPosition [[Attribute0]];
    float4 worldPosition [[Attribute1]];
    float4 worldNormalAndTile [[Attribute2]];
    float4 lightUvDepthAndReserved [[Attribute3]];
};

/** Stores the per-entity material phase and linear base color. */
struct WebglCameraArrayMaterialData
{
    float4 baseColorAndPhase;
};

/** Stores the required identity instance component for non-instanced entities. */
struct WebglCameraArrayInstanceData
{
    float4 identity;
};

/** Stores the required object component and the receive-shadow flag. */
struct WebglCameraArrayObjectData
{
    float4 receiveShadowAndReserved;
};

/** Defines the unique two-entity Scene RenderSet for this array-camera example. */
struct WebglCameraArraySceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and mandatory object, instance, and material components. */
    constructor(
        BufferComponent<WebglCameraArrayVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglCameraArrayObjectData> objects,
        BufferComponent<WebglCameraArrayInstanceData> instances,
        BufferComponent<WebglCameraArrayMaterialData> materials)
    {
    }
};

/** Binds the current directional shadow depth texture. */
struct WebglCameraArrayMainResources final : public IBindGroup
{
    /** Declares the raw depth texture used by the private five-sample PCF implementation. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowDepth [[Binding0]])
    {
    }
};

/** Carries entity phase data through the depth-only shadow pass. */
struct WebglCameraArrayShadowVertexOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
};

/** Defines the directional-light depth attachment. */
struct WebglCameraArrayShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Carries projected tile, world-space lighting, and entity data into the main fragment stage. */
struct WebglCameraArrayMainVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 lightUvDepth [[Attribute2]];
    uint entityID [[Attribute3]];
    uint tileIndex [[Attribute4]];
};

/** Defines the final single-sample RGBA8 color and depth attachments. */
struct WebglCameraArrayMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear working-space channel with the r185 sRGB output transfer. */
float WebglCameraArraylinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reads one clamped directional shadow texel. */
float WebglCameraArrayreadShadowDepth(
    IN BindGroup<WebglCameraArrayMainResources> resources,
    uint2 texel)
{
    const uint x = min(texel.x, 511u);
    const uint y = min(texel.y, 511u);
    return resources->shadowDepth->read(uint2(x, y)).x;
}

/** Reproduces one hardware bilinear LessEqual depth comparison. */
float WebglCameraArraybilinearShadowCompare(
    IN BindGroup<WebglCameraArrayMainResources> resources,
    float2 uv,
    float compareDepth)
{
    const float2 texelPosition = uv * 512.0f - 0.5f;
    const float2 lowerPosition = floor(texelPosition);
    const float2 fraction = texelPosition - lowerPosition;
    const uint x0 = uint(clamp(lowerPosition.x, 0.0f, 511.0f));
    const uint y0 = uint(clamp(lowerPosition.y, 0.0f, 511.0f));
    const uint x1 = min(x0 + 1u, 511u);
    const uint y1 = min(y0 + 1u, 511u);
    const float compare00 = compareDepth <= WebglCameraArrayreadShadowDepth(resources, uint2(x0, y0)) ? 1.0f : 0.0f;
    const float compare10 = compareDepth <= WebglCameraArrayreadShadowDepth(resources, uint2(x1, y0)) ? 1.0f : 0.0f;
    const float compare01 = compareDepth <= WebglCameraArrayreadShadowDepth(resources, uint2(x0, y1)) ? 1.0f : 0.0f;
    const float compare11 = compareDepth <= WebglCameraArrayreadShadowDepth(resources, uint2(x1, y1)) ? 1.0f : 0.0f;
    return lerp(
        lerp(compare00, compare10, fraction.x),
        lerp(compare01, compare11, fraction.x),
        fraction.y);
}

/** Evaluates r185's five rotated PCF samples from the current raw depth attachment. */
float WebglCameraArrayshadowFactor(
    IN BindGroup<WebglCameraArrayMainResources> resources,
    float3 lightUvDepth,
    float2 screenPosition)
{
    if (lightUvDepth.x < 0.0f || lightUvDepth.x > 1.0f ||
        lightUvDepth.y < 0.0f || lightUvDepth.y > 1.0f ||
        lightUvDepth.z > 1.0f)
    {
        return 1.0f;
    }
    const float phi = frac(
        52.9829189f * frac(dot(screenPosition, float2(0.06711056f, 0.00583715f)))) *
        6.283185307179586f;
    float shadow = 0.0f;
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        const float radius = sqrt((float(sampleIndex) + 0.5f) / 5.0f) / 512.0f;
        const float theta = float(sampleIndex) * 2.399963229728653f + phi;
        const float2 offset = float2(cos(theta), sin(theta)) * radius;
        shadow += WebglCameraArraybilinearShadowCompare(
            resources,
            lightUvDepth.xy + offset,
            lightUvDepth.z - 0.0005f);
    }
    return shadow * 0.2f;
}

/** Draws only the cylinder caster into the one directional shadow map. */
class WebglCameraArrayShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the same unique Scene RenderSet used by every main invocation. */
    constructor(RenderSet<WebglCameraArraySceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the entity and identity instance component through RenderSet builtins. */
    WebglCameraArrayShadowVertexOutput vertex(
        WebglCameraArrayVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglCameraArrayInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglCameraArrayShadowVertexOutput outputValue;
        outputValue.position = float4(
            inputValue.lightUvDepthAndReserved.xy * 2.0f - 1.0f,
            inputValue.lightUvDepthAndReserved.z,
            1.0f + instanceData.identity.x * 0.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Rejects the receiver plane so only the cylinder writes directional depth. */
    WebglCameraArrayShadowFrameBuffer fragment(
        WebglCameraArrayShadowVertexOutput inputValue)
    {
        if (inputValue.entityID == 0u)
        {
            discard_fragment();
        }
        WebglCameraArrayShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Draws the two entities across their preprojected six-by-six camera tiles. */
class WebglCameraArrayMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet plus the directional depth texture. */
    constructor(
        RenderSet<WebglCameraArraySceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglCameraArrayMainResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves object, instance, tile, world-space, and shadow data from the unique Set. */
    WebglCameraArrayMainVertexOutput vertex(
        WebglCameraArrayVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglCameraArrayObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglCameraArrayInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglCameraArrayMainVertexOutput outputValue;
        outputValue.position = inputValue.clipPosition +
            float4((objectData.receiveShadowAndReserved.y + instanceData.identity.y) * 0.0f);
        outputValue.worldPosition = inputValue.worldPosition.xyz;
        outputValue.worldNormal = inputValue.worldNormalAndTile.xyz;
        outputValue.lightUvDepth = inputValue.lightUvDepthAndReserved.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.tileIndex = uint(inputValue.worldNormalAndTile.w + 0.5f);
        return outputValue;
    }

    /** Evaluates tile clipping, r185 Lambert/Phong lighting, PCF shadowing, and output transfer. */
    WebglCameraArrayMainFrameBuffer fragment(
        WebglCameraArrayMainVertexOutput inputValue)
    {
        const uint tileX = inputValue.tileIndex % 6u;
        const uint tileY = inputValue.tileIndex / 6u;
        const float startX = floor(float(tileX) * 800.0f / 6.0f);
        const float endX = ceil(float(tileX + 1u) * 800.0f / 6.0f);
        const float startY = floor(float(tileY) * 500.0f / 6.0f);
        const float endY = ceil(float(tileY + 1u) * 500.0f / 6.0f);
        if (inputValue.position.x < startX || inputValue.position.x >= endX ||
            inputValue.position.y < startY || inputValue.position.y >= endY)
        {
            discard_fragment();
        }

        const WebglCameraArrayMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const WebglCameraArrayObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 lightDirection = normalize(float3(0.5f, 0.5f, 1.0f));
        const float shadow = objectData.receiveShadowAndReserved.x > 0.5f
            ? WebglCameraArrayshadowFactor(
                resources,
                inputValue.lightUvDepth,
                inputValue.position.xy)
            : 1.0f;
        const float direct = 3.0f * max(dot(normal, lightDirection), 0.0f) * shadow;
        const float ambient = 0.31854680f;
        float3 linearColor =
            materialData.baseColorAndPhase.xyz * ((ambient + direct) * 0.3183098861837907f);
        const float3 cameraPosition = float3(
            float(tileX) / 3.0f - 1.0f,
            float(tileY) / 3.0f - 0.6666666667f,
            3.0f);
        const float3 cameraDirection =
            normalize(cameraPosition - inputValue.worldPosition);
        const float3 halfDirection =
            normalize(lightDirection + cameraDirection);
        const float dotNormalHalf =
            max(dot(normal, halfDirection), 0.0f);
        const float specularDistribution =
            0.3183098861837907f * 16.0f *
            pow(dotNormalHalf, 30.0f);
        linearColor += float3(0.00560539f) *
            (direct * 0.25f * specularDistribution);
        const float3 encoded = float3(
            WebglCameraArraylinearToSrgb(linearColor.x),
            WebglCameraArraylinearToSrgb(linearColor.y),
            WebglCameraArraylinearToSrgb(linearColor.z));
        WebglCameraArrayMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated array-camera RenderSet, shadow map, and single-sample final target. */
class WebglCameraArrayRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglCameraArraySceneRenderSet> sceneSet;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> shadowDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> mainDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    BindGroup<WebglCameraArrayMainResources> mainResources;
    RenderClass<WebglCameraArrayShadowDepthPass> shadowPass;
    RenderClass<WebglCameraArrayMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene RenderSet and immutable directional shadow resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglCameraArraySceneRenderSet>();
        shadowDepth = device->createTexture(
            "CameraArrayShadowDepth",
            512u,
            512u,
            1u);
        mainResources =
            device->createBindGroup<WebglCameraArrayMainResources>(
                shadowDepth->createView());
        shadowPass =
            device->createRenderClass<WebglCameraArrayShadowDepthPass>(
                sceneSet);
        mainPass =
            device->createRenderClass<WebglCameraArrayMainPass>(
                sceneSet,
                mainResources);
    }

    /** Allocates the final ordinary single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        mainDepth = device->createTexture(
            "CameraArrayMainDepth",
            width,
            height,
            1u);
        outputColor = device->createTexture(
            "CameraArrayOutput",
            width,
            height,
            1u);
    }

    /** Updates the unique Set, renders one shadow pass, and submits the tiled Scene. */
    void render() override
    {
        sceneSet->update();
        WebglCameraArrayShadowFrameBuffer shadowFrameBuffer;
        shadowFrameBuffer.depth = shadowDepth->createView();
        shadowFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer.depth.depthClearValue = 1.0f;
        WebglCameraArrayMainFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = outputColor->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Clear;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        mainFrameBuffer.depth = mainDepth->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        mainFrameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("CameraArrayShadow", shadowFrameBuffer, shadowPass())
            ->renderPass("CameraArrayMain", mainFrameBuffer, mainPass())
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
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

    /** Releases the unique Scene RenderSet and every private single-sample texture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(shadowDepth);
        device->freeTexture(mainDepth);
        device->freeTexture(outputColor);
    }
};
