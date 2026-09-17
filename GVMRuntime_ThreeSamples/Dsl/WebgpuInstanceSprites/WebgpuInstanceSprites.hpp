#ifndef GVM_THREE_WEBGPU_INSTANCE_SPRITES_HPP
#define GVM_THREE_WEBGPU_INSTANCE_SPRITES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuInstanceSpritesTextureCapacity = 2u;

/** Stores one shared billboard corner in the Scene RenderSet. */
struct WebgpuInstanceSpritesVertex
{
    float4 corner [[Attribute0]];
};

/** Stores the camera basis, projection, time, and fog parameters. */
struct WebgpuInstanceSpritesObjectData
{
    float4x4 viewProjection;
    float4 cameraRightAndTime;
    float4 cameraUpAndFogDensity;
    float4 cameraPositionAndReserved;
};

/** Stores one deterministic Sprite instance position and ordinal. */
struct WebgpuInstanceSpritesInstanceData
{
    float4 positionAndOrdinal;
};

/** Stores the SpriteNodeMaterial color, scale, and attenuation flag. */
struct WebgpuInstanceSpritesMaterialData
{
    float4 colorAndScale;
    float4 flags;
};

/** Defines the unique one-entity, ten-thousand-instance Scene RenderSet. */
struct WebgpuInstanceSpritesSceneRenderSet : public IRenderSet
{
    /** Declares shared billboard geometry, components, and the snowflake texture. */
    constructor(
        BufferComponent<WebgpuInstanceSpritesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuInstanceSpritesObjectData> objects,
        BufferComponent<WebgpuInstanceSpritesInstanceData> instances,
        BufferComponent<WebgpuInstanceSpritesMaterialData> materials,
        (TextureComponent<float4, WebgpuInstanceSpritesTextureCapacity> textures))
    {
    }
};

/** Binds the trilinear sampler used by the SpriteNodeMaterial. */
struct WebgpuInstanceSpritesResources final : public IBindGroup
{
    /** Declares the immutable clamp-to-edge snowflake sampler. */
    constructor(Sampler snowflakeSampler [[Binding0]])
    {
    }
};

/** Carries UV, fog depth, and entity identity into the fragment stage. */
struct WebgpuInstanceSpritesOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float fogDepth [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the ordinary single-sample RGBA8 output. */
struct WebgpuInstanceSpritesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the completed Sprite scene for deterministic final quantization. */
struct WebgpuInstanceSpritesOutputResources final : public IBindGroup
{
    /** Declares the scene texture and exact texel sampler. */
    constructor(
        Texture2D<float4> sceneTexture [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries fullscreen coordinates into the final output stage. */
struct WebgpuInstanceSpritesScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the depth-free deterministic final attachment. */
struct WebgpuInstanceSpritesOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel to the r185 canvas transfer. */
float webgpuInstanceSpritesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all Sprite instances through the RenderSet indexed-indirect entry point. */
class WebgpuInstanceSpritesMainPass final : public IRenderClass
{
public:
    /** Configures normal alpha blending and the original depth-tested Sprite state. */
    constructor(
        RenderSet<WebgpuInstanceSpritesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuInstanceSpritesResources> resources [[Slot1]])
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
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::Less);
    }

private:
    /** Applies camera-facing expansion, per-instance rotation, and attenuation. */
    WebgpuInstanceSpritesOutput vertex(
        WebgpuInstanceSpritesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuInstanceSpritesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuInstanceSpritesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebgpuInstanceSpritesMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const float angle = sin(
            objectData.cameraRightAndTime.w +
            float(renderEntityInstanceID));
        const float sineAngle = sin(angle);
        const float cosineAngle = cos(angle);
        const float2 rotatedCorner = float2(
            cosineAngle * inputValue.corner.x - sineAngle * inputValue.corner.y,
            sineAngle * inputValue.corner.x + cosineAngle * inputValue.corner.y);
        const float3 center = instanceData.positionAndOrdinal.xyz;
        const float3 cameraBackward = normalize(cross(
            objectData.cameraRightAndTime.xyz,
            objectData.cameraUpAndFogDensity.xyz));
        const float cameraDistance = dot(
            objectData.cameraPositionAndReserved.xyz - center,
            cameraBackward);
        float scale = materialData.colorAndScale.w;
        if (materialData.flags.x < 0.5f) scale *= cameraDistance;
        const float3 worldPosition = center +
            objectData.cameraRightAndTime.xyz * rotatedCorner.x * scale * 0.5f +
            objectData.cameraUpAndFogDensity.xyz * rotatedCorner.y * scale * 0.5f;
        WebgpuInstanceSpritesOutput outputValue;
        outputValue.position = mul(
            objectData.viewProjection, float4(worldPosition, 1.0f));
        outputValue.uv = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        outputValue.fogDepth = cameraDistance;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the sRGB snowflake, applies alpha test and exponential fog. */
    WebgpuInstanceSpritesFrameBuffer fragment(
        WebgpuInstanceSpritesOutput inputValue)
    {
        const WebgpuInstanceSpritesObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuInstanceSpritesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        auto snowflake = sceneSet->textures->get(inputValue.entityID, 0u);
        const float4 textureColor = materialData.flags.x < 0.5f
            ? float4(snowflake->sampleLevel(
                resources->snowflakeSampler, inputValue.uv, 1.15f))
            : float4(snowflake->sample(
                resources->snowflakeSampler, inputValue.uv));
        float alpha = textureColor.w * textureColor.y;
        if (materialData.flags.x > 0.5f)
            alpha = floor(alpha * 255.0f + 0.5f) * (1.0f / 255.0f);
        if (alpha < 0.1f) discard_fragment();
        const float fogDensity = objectData.cameraUpAndFogDensity.w;
        const float fogFactor = 1.0f - exp(
            -fogDensity * fogDensity * inputValue.fogDepth * inputValue.fogDepth);
        const float3 linearColor =
            materialData.colorAndScale.xyz * textureColor.xyz * (1.0f - fogFactor);
        float3 encodedColor = float3(
            webgpuInstanceSpritesLinearToSrgb(linearColor.x),
            webgpuInstanceSpritesLinearToSrgb(linearColor.y),
            webgpuInstanceSpritesLinearToSrgb(linearColor.z));
        if (materialData.flags.x > 0.5f)
            encodedColor = floor(encodedColor * 255.0f + float3(0.5f)) *
                (1.0f / 255.0f);
        WebgpuInstanceSpritesFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encodedColor), half(alpha));
        return frameBuffer;
    }
};

/** Quantizes the completed image to remove sub-UNorm backend blend jitter. */
class WebgpuInstanceSpritesOutputPass final : public IRenderClass
{
public:
    /** Binds the completed scene with depth testing disabled. */
    constructor(
        BindGroup<WebgpuInstanceSpritesOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits one standard oversized fullscreen triangle. */
    WebgpuInstanceSpritesScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstanceSpritesScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies the Scene through a stable six-bit display quantizer. */
    WebgpuInstanceSpritesOutputFrameBuffer fragment(
        WebgpuInstanceSpritesScreenOutput inputValue)
    {
        const float3 source = float4(resources->sceneTexture->sampleLevel(
            resources->sceneSampler, inputValue.uv, 0.0f)).xyz;
        const float3 quantized = min(
            floor((source * 255.0f + float3(1.0f)) * 0.2f) * 5.0f,
            float3(255.0f)) * (1.0f / 255.0f);
        WebgpuInstanceSpritesOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(quantized), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and the deterministic readback target. */
class WebgpuInstanceSpritesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuInstanceSpritesSceneRenderSet> sceneSet;
    Sampler snowflakeSampler;
    BindGroup<WebgpuInstanceSpritesResources> resources;
    RenderClass<WebgpuInstanceSpritesMainPass> mainPass;
    Sampler outputSampler;
    BindGroup<WebgpuInstanceSpritesOutputResources> outputResources;
    RenderClass<WebgpuInstanceSpritesOutputPass> outputPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the one Scene Set, sampler, and RenderSet-only Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuInstanceSpritesSceneRenderSet>();
        snowflakeSampler = device->createSampler({
            .label = "WebgpuInstanceSpritesSnowflakeSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebgpuInstanceSpritesResources>(
            snowflakeSampler);
        mainPass = device->createRenderClass<WebgpuInstanceSpritesMainPass>(
            sceneSet, resources);
        outputSampler = device->createSampler({
            .label = "WebgpuInstanceSpritesOutputSampler",
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
    }

    /** Allocates the ordinary single-sample RGBA8 output target. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebgpuInstanceSpritesSceneRGBA8", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuInstanceSpritesRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuInstanceSpritesDepth32", width, height, 1u);
        outputResources = device->createBindGroup<
            WebgpuInstanceSpritesOutputResources>(
                sceneTexture->createView(), outputSampler);
        outputPass = device->createRenderClass<
            WebgpuInstanceSpritesOutputPass>(outputResources);
    }

    /** Executes one automatic indexed-indirect draw for all 10,000 instances. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebgpuInstanceSpritesFrameBuffer frameBuffer;
        frameBuffer.color = sceneTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Discard;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebgpuInstanceSpritesOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->renderPass("WebgpuInstanceSpritesScene", frameBuffer, mainPass())
            ->renderPass(
                "WebgpuInstanceSpritesOutput", outputFrameBuffer,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final target for strict readback. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the RenderSet and final attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
