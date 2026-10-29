#ifndef GVM_THREE_WEBGL_SPRITES_HPP
#define GVM_THREE_WEBGL_SPRITES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglSpritesTextureCapacity = 256u;

/** Stores one shared sprite quad corner and UV coordinate. */
struct WebglSpritesVertex
{
    float4 cornerAndUv [[Attribute0]];
};

/** Stores one camera projection and sprite center in view space. */
struct WebglSpritesObjectData
{
    float4x4 projection;
    float4 viewCenter;
};

/** Stores the mandatory non-instanced entity record. */
struct WebglSpritesInstanceData
{
    float4 reserved;
};

/** Stores linear color, opacity, and texture-transform selection. */
struct WebglSpritesMaterialData
{
    float4 colorAndOpacity;
    float4 uvScaleAndOffset;
};

/** Stores rotation, scale, center, visibility, and fog state. */
struct WebglSpritesStateData
{
    float4 rotationScaleCenterX;
    float4 centerYVisibilityFog;
};

/** Defines the cropped Set type instantiated once for each logical Scene. */
struct WebglSpritesSceneRenderSet : public IRenderSet
{
    /** Declares shared quad geometry, entity state, and texture slots. */
    constructor(
        BufferComponent<WebglSpritesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglSpritesObjectData> objects,
        BufferComponent<WebglSpritesInstanceData> instances,
        BufferComponent<WebglSpritesMaterialData> materials,
        (TextureComponent<float4, WebglSpritesTextureCapacity> textures),
        BufferComponent<WebglSpritesStateData> spriteState)
    {
    }
};

/** Binds the mipmapped sprite sampler shared by both Scenes. */
struct WebglSpritesResources final : public IBindGroup
{
    /** Declares the fixed clamp sampler. */
    constructor(Sampler spriteSampler [[Binding0]])
    {
    }
};

/** Binds the completed linear image for the final output transfer. */
struct WebglSpritesOutputResources final : public IBindGroup
{
    /** Declares the linear Scene texture and centered sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries sprite UV, fog distance, visibility, and entity identity. */
struct WebglSpritesVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float fogDepth [[Attribute1]];
    float visibility [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the shared display-encoded Scene color and depth attachments. */
struct WebglSpritesSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebglSpritesOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen output coordinates. */
struct WebglSpritesScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear working-space channel to the r185 output transfer. */
float webglSpritesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Expands one ordinary entity into a rotated camera-facing quad. */
WebglSpritesVertexOutput webglSpritesVertex(
    IN RenderSet<WebglSpritesSceneRenderSet> sceneSet,
    WebglSpritesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglSpritesObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglSpritesInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebglSpritesStateData stateData =
        sceneSet->spriteState->get(renderEntityID, 0u);
    const float rotation = stateData.rotationScaleCenterX.x;
    const float2 scale = stateData.rotationScaleCenterX.yz;
    const float2 center = float2(
        stateData.rotationScaleCenterX.w,
        stateData.centerYVisibilityFog.x);
    float2 aligned =
        (inputValue.cornerAndUv.xy - (center - float2(0.5f))) * scale;
    aligned += instanceData.reserved.xy;
    const float sineValue = sin(rotation);
    const float cosineValue = cos(rotation);
    const float2 rotated = float2(
        cosineValue * aligned.x - sineValue * aligned.y,
        sineValue * aligned.x + cosineValue * aligned.y);
    const float4 viewPosition = float4(
        objectData.viewCenter.xy + rotated,
        objectData.viewCenter.z,
        1.0f);
    WebglSpritesVertexOutput outputValue;
    outputValue.position = mul(objectData.projection, viewPosition);
    outputValue.uv = inputValue.cornerAndUv.zw;
    outputValue.fogDepth = -viewPosition.z;
    outputValue.visibility = stateData.centerYVisibilityFog.y;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Samples one sprite and applies the entity's linear material color. */
float4 webglSpritesSample(
    IN RenderSet<WebglSpritesSceneRenderSet> sceneSet,
    BindGroup<WebglSpritesResources> resources,
    WebglSpritesVertexOutput inputValue)
{
    const WebglSpritesMaterialData material =
        sceneSet->materials->get(inputValue.entityID, 0u);
    auto spriteTexture = sceneSet->textures->get(inputValue.entityID, 0u);
    const float2 transformedUv =
        inputValue.uv * material.uvScaleAndOffset.xy +
        material.uvScaleAndOffset.zw;
    const float4 sampled = float4(spriteTexture->sample(
        resources->spriteSampler, transformedUv));
    return float4(
        sampled.xyz * material.colorAndOpacity.xyz,
        sampled.w * material.colorAndOpacity.w);
}

/** Draws the 200-entity perspective world Scene through its unique Set. */
class WebglSpritesWorldPass final : public IRenderClass
{
public:
    /** Binds only the perspective Scene Set and shared sprite sampler. */
    constructor(
        RenderSet<WebglSpritesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglSpritesResources> resources [[Slot1]])
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
    /** Expands the current world entity through RenderSet builtins. */
    WebglSpritesVertexOutput vertex(
        WebglSpritesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglSpritesVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Applies the textured material and linear black distance fog. */
    WebglSpritesSceneFrameBuffer fragment(
        WebglSpritesVertexOutput inputValue)
    {
        if (inputValue.visibility < 0.5f) discard_fragment();
        float4 color = webglSpritesSample(sceneSet, resources, inputValue);
        const float fogFactor = smoothstep(1500.0f, 2100.0f, inputValue.fogDepth);
        const float3 encoded = float3(
            webglSpritesLinearToSrgb(color.x),
            webglSpritesLinearToSrgb(color.y),
            webglSpritesLinearToSrgb(color.z));
        WebglSpritesSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(lerp(encoded, float3(0.0f), fogFactor)),
            half(color.w));
        return frameBuffer;
    }
};

/** Draws the five-entity orthographic HUD Scene through its unique Set. */
class WebglSpritesHudPass final : public IRenderClass
{
public:
    /** Binds only the HUD Scene Set and shared sprite sampler. */
    constructor(
        RenderSet<WebglSpritesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglSpritesResources> resources [[Slot1]])
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
    /** Expands the current HUD entity through RenderSet builtins. */
    WebglSpritesVertexOutput vertex(
        WebglSpritesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglSpritesVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Samples the HUD sprite without perspective fog. */
    WebglSpritesSceneFrameBuffer fragment(
        WebglSpritesVertexOutput inputValue)
    {
        if (inputValue.visibility < 0.5f) discard_fragment();
        const float4 linearColor =
            webglSpritesSample(sceneSet, resources, inputValue);
        WebglSpritesSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglSpritesLinearToSrgb(linearColor.x),
                webglSpritesLinearToSrgb(linearColor.y),
                webglSpritesLinearToSrgb(linearColor.z)),
            half(linearColor.w));
        return frameBuffer;
    }
};

/** Encodes the combined linear world and HUD image to final RGBA8. */
class WebglSpritesOutputPass final : public IRenderClass
{
public:
    /** Binds the completed linear image without Scene geometry. */
    constructor(
        BindGroup<WebglSpritesOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen output triangle. */
    WebglSpritesScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglSpritesScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies the completed display-encoded Scene to final RGBA8. */
    WebglSpritesOutputFrameBuffer fragment(
        WebglSpritesScreenOutput inputValue)
    {
        const float4 encodedColor = float4(resources->sceneColor->sampleLevel(
            resources->sceneSampler, inputValue.uv, 0.0f));
        WebglSpritesOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encodedColor.xyz), half(1.0f));
        return frameBuffer;
    }
};

/** Owns two logical Scene Set instances and their ordered geometry passes. */
class WebglSpritesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglSpritesSceneRenderSet> worldSet;
    [[Export]] RenderSet<WebglSpritesSceneRenderSet> hudSet;
    Sampler spriteSampler;
    BindGroup<WebglSpritesResources> resources;
    RenderClass<WebglSpritesWorldPass> worldPass;
    RenderClass<WebglSpritesHudPass> hudPass;
    Sampler outputSampler;
    BindGroup<WebglSpritesOutputResources> outputResources;
    RenderClass<WebglSpritesOutputPass> outputPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> worldDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> hudDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both Scene Sets, dedicated passes, and immutable samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        worldSet = device->createRenderSet<WebglSpritesSceneRenderSet>();
        hudSet = device->createRenderSet<WebglSpritesSceneRenderSet>();
        spriteSampler = device->createSampler({
            .label = "WebglSpritesSampler",
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
        resources = device->createBindGroup<WebglSpritesResources>(spriteSampler);
        worldPass = device->createRenderClass<WebglSpritesWorldPass>(
            worldSet, resources);
        hudPass = device->createRenderClass<WebglSpritesHudPass>(
            hudSet, resources);
        outputSampler = device->createSampler({
            .label = "WebglSpritesOutputSampler",
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

    /** Allocates ordinary single-sample world, HUD, and final targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebglSpritesLinearRGBA16", width, height, 1u);
        worldDepth = device->createTexture(
            "WebglSpritesWorldDepth32", width, height, 1u);
        hudDepth = device->createTexture(
            "WebglSpritesHudDepth32", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglSpritesRGBA8", width, height, 1u);
        outputResources = device->createBindGroup<WebglSpritesOutputResources>(
            sceneTexture->createView(), outputSampler);
        outputPass = device->createRenderClass<WebglSpritesOutputPass>(
            outputResources);
    }

    /** Draws world, clears HUD depth, draws HUD, transfers, and presents. */
    void render() override
    {
        worldSet->update();
        hudSet->update();
        WebglSpritesSceneFrameBuffer worldFrameBuffer;
        worldFrameBuffer.color = sceneTexture->createView();
        worldFrameBuffer.color.loadOp = LoadOp::Clear;
        worldFrameBuffer.color.storeOp = StoreOp::Store;
        worldFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        worldFrameBuffer.depth = worldDepth->createView();
        worldFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        worldFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        worldFrameBuffer.depth.depthClearValue = 1.0f;
        WebglSpritesSceneFrameBuffer hudFrameBuffer;
        hudFrameBuffer.color = sceneTexture->createView();
        hudFrameBuffer.color.loadOp = LoadOp::Load;
        hudFrameBuffer.color.storeOp = StoreOp::Store;
        hudFrameBuffer.depth = hudDepth->createView();
        hudFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        hudFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        hudFrameBuffer.depth.depthClearValue = 1.0f;
        WebglSpritesOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglSpritesWorld", worldFrameBuffer, worldPass())
            ->renderPass("WebglSpritesHud", hudFrameBuffer, hudPass())
            ->renderPass(
                "WebglSpritesOutput",
                outputFrameBuffer,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
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

    /** Releases both Scene Sets and all owned attachments. */
    void destroy() override
    {
        worldSet->destroy();
        hudSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(worldDepth);
        device->freeTexture(hudDepth);
        device->freeTexture(outputTexture);
    }
};

#endif
