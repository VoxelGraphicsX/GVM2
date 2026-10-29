#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_CUSTOM_ATTRIBUTES_PARTICLES_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_CUSTOM_ATTRIBUTES_PARTICLES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglBuffergeometryCustomAttributesParticlesMaxTextures = 8u;

/** Stores one corner of the triangle-expanded point template. */
struct WebglBuffergeometryCustomAttributesParticlesVertex
{
    float4 corner [[Attribute0]];
};

/** Stores the immutable camera transforms for the sole particle entity. */
struct WebglBuffergeometryCustomAttributesParticlesObjectData
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Stores one logical particle position, target-frame size, and HSL color. */
struct WebglBuffergeometryCustomAttributesParticlesInstanceData
{
    float4 positionAndSize;
    float4 color;
};

/** Stores the dedicated additive material parameters. */
struct WebglBuffergeometryCustomAttributesParticlesMaterialData
{
    float4 colorMultiplier;
};

/** Defines the only Scene RenderSet used by the 100,000-particle example. */
struct WebglBuffergeometryCustomAttributesParticlesSceneRenderSet : public IRenderSet
{
    /** Declares the quad template, instance attributes, material, and spark texture. */
    constructor(
        BufferComponent<WebglBuffergeometryCustomAttributesParticlesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglBuffergeometryCustomAttributesParticlesObjectData> objects,
        BufferComponent<WebglBuffergeometryCustomAttributesParticlesInstanceData> instances,
        BufferComponent<WebglBuffergeometryCustomAttributesParticlesMaterialData> materials,
        (TextureComponent<half4, WebglBuffergeometryCustomAttributesParticlesMaxTextures> textures))
    {
    }
};

/** Binds the trilinear sampler used by the entity TextureComponent. */
struct WebglBuffergeometryCustomAttributesParticlesSamplerResources final : public IBindGroup
{
    /** Declares the one immutable spark sampler. */
    constructor(Sampler sparkSampler [[Binding0]])
    {
    }
};

/** Carries the custom color, local point coordinate, and entity identity. */
struct WebglBuffergeometryCustomAttributesParticlesVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float2 pointCoord [[Attribute1]];
    float pointSize [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the single-sample linear particle accumulation attachment. */
struct WebglBuffergeometryCustomAttributesParticlesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws one 100,000-instance particle entity through RenderSet metadata. */
class WebglBuffergeometryCustomAttributesParticlesMainPass final : public IRenderClass
{
public:
    /** Configures Three's additive, depth-disabled ShaderMaterial state. */
    constructor(
        RenderSet<WebglBuffergeometryCustomAttributesParticlesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglBuffergeometryCustomAttributesParticlesSamplerResources> samplerResources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Expands one instance into a perspective-sized screen-space point quad. */
    WebglBuffergeometryCustomAttributesParticlesVertexOutput vertex(
        WebglBuffergeometryCustomAttributesParticlesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryCustomAttributesParticlesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryCustomAttributesParticlesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 viewPosition = mul(
            objectData.modelViewMatrix,
            float4(instanceData.positionAndSize.xyz, 1.0f));
        const float rawPointSize =
            instanceData.positionAndSize.w *
            (300.0f / -viewPosition.z);
        const float pointSize =
            clamp(rawPointSize, 1.0f, 511.0f);
        float4 clipPosition = mul(objectData.projectionMatrix, viewPosition);
        clipPosition.x += inputValue.corner.x * pointSize / 800.0f * clipPosition.w;
        clipPosition.y += inputValue.corner.y * pointSize / 500.0f * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglBuffergeometryCustomAttributesParticlesVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = instanceData.color.xyz;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        outputValue.pointSize = pointSize;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Accumulates one linear-light particle through Three's additive blend. */
    WebglBuffergeometryCustomAttributesParticlesFrameBuffer fragment(
        WebglBuffergeometryCustomAttributesParticlesVertexOutput inputValue)
    {
        const WebglBuffergeometryCustomAttributesParticlesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        auto sparkTexture = sceneSet->textures->get(inputValue.entityID, 0u);
        const float4 spark = float4(sparkTexture->sample(
            samplerResources->sparkSampler,
            inputValue.pointCoord));

        const float4 linearColor =
            float4(inputValue.color, 1.0f) *
            materialData.colorMultiplier *
            spark;
        WebglBuffergeometryCustomAttributesParticlesFrameBuffer frameBuffer;
        frameBuffer.color = half4(linearColor);
        return frameBuffer;
    }
};

/** Owns the dedicated Scene RenderSet and deterministic particle output. */
class WebglBuffergeometryCustomAttributesParticlesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryCustomAttributesParticlesSceneRenderSet> sceneSet;
    Sampler sparkSampler;
    BindGroup<WebglBuffergeometryCustomAttributesParticlesSamplerResources> samplerResources;
    RenderClass<WebglBuffergeometryCustomAttributesParticlesMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates exactly one Scene RenderSet and its RenderSet-only pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSet>();
        sparkSampler = device->createSampler({
            .label = "WebglBuffergeometryCustomAttributesParticlesSparkSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 5,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<
            WebglBuffergeometryCustomAttributesParticlesSamplerResources>(
                sparkSampler);
        scenePass = device->createRenderClass<
            WebglBuffergeometryCustomAttributesParticlesMainPass>(
                sceneSet,
                samplerResources);
    }

    /** Allocates the host-sized linear scene and deterministic RGBA8 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebglBuffergeometryCustomAttributesParticlesRawRGBA8",
            width,
            height,
            1u);
    }

    /** Updates entity metadata and submits one indexed-indirect RenderSet draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryCustomAttributesParticlesFrameBuffer frameBuffer;
        frameBuffer.color = sceneTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->renderPass(
                "WebglBuffergeometryCustomAttributesParticlesScene",
                frameBuffer,
                scenePass())
            ->renderToSwapchain(
                nextTexture,
                sceneTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final texture used by strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return sceneTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the sole Scene RenderSet and deterministic output. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
    }
};

#endif
