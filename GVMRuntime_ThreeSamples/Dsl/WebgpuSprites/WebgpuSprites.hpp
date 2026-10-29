#ifndef GVM_THREE_WEBGPU_SPRITES_HPP
#define GVM_THREE_WEBGPU_SPRITES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuSpritesTextureCapacity = 256u;

/** Stores one camera-facing sprite quad corner and its UV coordinate. */
struct WebgpuSpritesVertex
{
    float4 cornerAndUv [[Attribute0]];
};

/** Stores one sprite center in view space and the shared projection. */
struct WebgpuSpritesObjectData
{
    float4x4 projection;
    float4 viewCenterAndDepth;
};

/** Stores the mandatory one-record non-instanced component. */
struct WebgpuSpritesInstanceData
{
    float4 reserved;
};

/** Stores the fixed white material and opacity multiplier. */
struct WebgpuSpritesMaterialData
{
    float4 colorAndOpacity;
};

/** Stores per-entity rotation, scale, center, and visibility. */
struct WebgpuSpritesStateData
{
    float4 rotationScaleCenter;
    float4 visibilityAndFog;
};

/** Defines the unique 200-entity Scene RenderSet for WebGPU sprites. */
struct WebgpuSpritesSceneRenderSet : public IRenderSet
{
    /** Declares quad geometry, per-entity state, and fixed texture slots. */
    constructor(
        BufferComponent<WebgpuSpritesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuSpritesObjectData> objects,
        BufferComponent<WebgpuSpritesInstanceData> instances,
        BufferComponent<WebgpuSpritesMaterialData> materials,
        (TextureComponent<float4, WebgpuSpritesTextureCapacity> textures),
        BufferComponent<WebgpuSpritesStateData> spriteState)
    {
    }
};

/** Binds the linear mipmapped sampler used by the shared sprite texture. */
struct WebgpuSpritesResources final : public IBindGroup
{
    /** Declares the immutable clamp sampler. */
    constructor(Sampler spriteSampler [[Binding0]])
    {
    }
};

/** Binds the linear Scene image consumed by the final output transfer. */
struct WebgpuSpritesOutputResources final : public IBindGroup
{
    /** Declares the linear Scene texture and centered sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries sampled UV, fog depth, and entity identity to fragments. */
struct WebgpuSpritesVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float fogDepth [[Attribute1]];
    float visibility [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the ordinary single-sample Scene color and depth attachments. */
struct WebgpuSpritesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample RGBA8 attachment. */
struct WebgpuSpritesOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen output coordinates. */
struct WebgpuSpritesScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear working-space channel to the browser transfer. */
float webgpuSpritesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all ordinary sprite entities through one RenderSet-only pass. */
class WebgpuSpritesScenePass final : public IRenderClass
{
public:
    /** Configures SpriteNodeMaterial alpha blending and depth behavior. */
    constructor(
        RenderSet<WebgpuSpritesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSpritesResources> resources [[Slot1]])
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
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one sprite corner with its per-entity scale and rotation. */
    WebgpuSpritesVertexOutput vertex(
        WebgpuSpritesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuSpritesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuSpritesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebgpuSpritesStateData stateData =
            sceneSet->spriteState->get(renderEntityID, 0u);
        const float rotation = stateData.rotationScaleCenter.x;
        const float2 scale = stateData.rotationScaleCenter.yz;
        const float sineValue = sin(rotation);
        const float cosineValue = cos(rotation);
        float2 aligned = inputValue.cornerAndUv.xy * scale;
        aligned += instanceData.reserved.xy;
        const float2 rotated = float2(
            cosineValue * aligned.x - sineValue * aligned.y,
            sineValue * aligned.x + cosineValue * aligned.y);
        const float4 viewPosition = float4(
            objectData.viewCenterAndDepth.xy + rotated,
            objectData.viewCenterAndDepth.z,
            1.0f);
        float4 clipPosition = mul(objectData.projection, viewPosition);
        WebgpuSpritesVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.uv = inputValue.cornerAndUv.zw;
        outputValue.fogDepth = -viewPosition.z;
        outputValue.visibility = stateData.visibilityAndFog.x;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Reproduces the TSL texture-UV color node, alpha, and blue range fog. */
    WebgpuSpritesFrameBuffer fragment(
        WebgpuSpritesVertexOutput inputValue)
    {
        if (inputValue.visibility < 0.5f) discard_fragment();
        const WebgpuSpritesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        auto spriteTexture = sceneSet->textures->get(inputValue.entityID, 0u);
        const float4 sampled = float4(spriteTexture->sample(
            resources->spriteSampler, inputValue.uv));
        float3 linearColor = clamp(
            sampled.xyz * float3(inputValue.uv, 0.0f) * 2.0f,
            float3(0.0f), float3(1.0f));
        const float fogFactor = smoothstep(
            1500.0f, 2100.0f, inputValue.fogDepth);
        linearColor = lerp(linearColor, float3(0.0f, 0.0f, 1.0f), fogFactor);
        const float opacity = clamp(sampled.w * 2.0f, 0.0f, 1.0f) *
            sampled.w * materialData.colorAndOpacity.w;
        WebgpuSpritesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor),
            half(opacity));
        return frameBuffer;
    }
};

/** Applies the renderer output transfer after all linear sprite blending. */
class WebgpuSpritesOutputPass final : public IRenderClass
{
public:
    /** Binds the completed linear Scene image without geometry state. */
    constructor(
        BindGroup<WebgpuSpritesOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen output triangle. */
    WebgpuSpritesScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuSpritesScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Encodes one completed linear Scene sample to final RGBA8. */
    WebgpuSpritesOutputFrameBuffer fragment(
        WebgpuSpritesScreenOutput inputValue)
    {
        const float4 linearSample = float4(resources->sceneColor->sampleLevel(
            resources->sceneSampler, inputValue.uv, 0.0f));
        WebgpuSpritesOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuSpritesLinearToSrgb(linearSample.x),
                webgpuSpritesLinearToSrgb(linearSample.y),
                webgpuSpritesLinearToSrgb(linearSample.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGPU sprite Scene RenderSet and final output. */
class WebgpuSpritesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSpritesSceneRenderSet> sceneSet;
    Sampler spriteSampler;
    BindGroup<WebgpuSpritesResources> resources;
    RenderClass<WebgpuSpritesScenePass> scenePass;
    Sampler outputSampler;
    BindGroup<WebgpuSpritesOutputResources> outputResources;
    RenderClass<WebgpuSpritesOutputPass> outputPass;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one Scene Set, sampler, and dedicated Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuSpritesSceneRenderSet>();
        spriteSampler = device->createSampler({
            .label = "WebgpuSpritesSampler",
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
        resources = device->createBindGroup<WebgpuSpritesResources>(spriteSampler);
        scenePass = device->createRenderClass<WebgpuSpritesScenePass>(
            sceneSet, resources);
        outputSampler = device->createSampler({
            .label = "WebgpuSpritesOutputSampler",
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

    /** Allocates ordinary single-sample final color and depth textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuSpritesLinearRGBA16", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuSpritesRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuSpritesDepth32", width, height, 1u);
        outputResources =
            device->createBindGroup<WebgpuSpritesOutputResources>(
                sceneTexture->createView(), outputSampler);
        outputPass = device->createRenderClass<WebgpuSpritesOutputPass>(
            outputResources);
    }

    /** Updates the Set, performs one automatic indirect draw, and presents. */
    void render() override
    {
        sceneSet->update();
        WebgpuSpritesFrameBuffer frameBuffer;
        frameBuffer.color = sceneTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebgpuSpritesOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuSpritesScene", frameBuffer, scenePass())
            ->renderPass(
                "WebgpuSpritesOutput",
                outputFrameBuffer,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the RenderSet and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
