#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_BILLBOARDS_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_BILLBOARDS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglBuffergeometryInstancingBillboardsMaxTextures = 2u;

/** Stores one vertex of the six-segment source circle. */
struct WebglBuffergeometryInstancingBillboardsVertex
{
    float4 position [[Attribute0]];
    float4 texCoord [[Attribute1]];
};

/** Stores the shared camera, mesh transform, and animation time. */
struct WebglBuffergeometryInstancingBillboardsObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 timeAndFlags;
};

/** Stores one seeded translate attribute addressed by RenderEntityInstanceID. */
struct WebglBuffergeometryInstancingBillboardsInstanceData
{
    float4 translate;
};

/** Stores the RawShaderMaterial color multiplier. */
struct WebglBuffergeometryInstancingBillboardsMaterialData
{
    float4 colorMultiplier;
};

/** Defines the sole Scene RenderSet containing one 75,000-instance entity. */
struct WebglBuffergeometryInstancingBillboardsSceneRenderSet : public IRenderSet
{
    /** Declares circle geometry, instance translations, material, and sprite texture. */
    constructor(
        BufferComponent<WebglBuffergeometryInstancingBillboardsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglBuffergeometryInstancingBillboardsObjectData> objects,
        BufferComponent<WebglBuffergeometryInstancingBillboardsInstanceData> instances,
        BufferComponent<WebglBuffergeometryInstancingBillboardsMaterialData> materials,
        (TextureComponent<half4, WebglBuffergeometryInstancingBillboardsMaxTextures> textures))
    {
    }
};

/** Binds the linear sprite sampler used by the TextureComponent. */
struct WebglBuffergeometryInstancingBillboardsSamplerResources final : public IBindGroup
{
    /** Declares the one immutable circle sampler. */
    constructor(Sampler circleSampler [[Binding0]])
    {
    }
};

/** Carries sprite coordinates, procedural scale, and entity identity. */
struct WebglBuffergeometryInstancingBillboardsVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
    float proceduralScale [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the opaque RawShaderMaterial color and depth attachments. */
struct WebglBuffergeometryInstancingBillboardsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a hue value to the exact branchless RGB shape used upstream. */
float3 webglBuffergeometryBillboardsHueToRgb(float hue)
{
    const float wrappedHue = hue - floor(hue);
    const float red =
        clamp(abs(wrappedHue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    const float green =
        clamp(2.0f - abs(wrappedHue * 6.0f - 2.0f), 0.0f, 1.0f);
    const float blue =
        clamp(2.0f - abs(wrappedHue * 6.0f - 4.0f), 0.0f, 1.0f);
    return float3(red, green, blue);
}

/** Draws the exact single instanced circle entity through RenderSet metadata. */
class WebglBuffergeometryInstancingBillboardsMainPass final : public IRenderClass
{
public:
    /** Configures the upstream depth state without blending. */
    constructor(
        RenderSet<WebglBuffergeometryInstancingBillboardsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglBuffergeometryInstancingBillboardsSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the seeded center transform and camera-facing procedural circle scale. */
    WebglBuffergeometryInstancingBillboardsVertexOutput vertex(
        WebglBuffergeometryInstancingBillboardsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryInstancingBillboardsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryInstancingBillboardsInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        float4 viewPosition = mul(
            objectData.modelView,
            float4(instanceData.translate.xyz, 1.0f));
        const float3 translatedTime =
            instanceData.translate.xyz +
            objectData.timeAndFlags.xxx;
        const float proceduralScale =
            sin(translatedTime.x * 2.1f) +
            sin(translatedTime.y * 3.2f) +
            sin(translatedTime.z * 4.3f);
        viewPosition.xyz +=
            inputValue.position.xyz *
            (proceduralScale * 10.0f + 10.0f);

        WebglBuffergeometryInstancingBillboardsVertexOutput outputValue;
        outputValue.position =
            mul(objectData.projection, viewPosition);
        outputValue.position.z =
            (outputValue.position.z +
             outputValue.position.w) *
            0.5f;
        outputValue.texCoord = inputValue.texCoord.xy;
        outputValue.proceduralScale = proceduralScale;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the circle alpha, discards its fringe, and applies procedural HSL color. */
    WebglBuffergeometryInstancingBillboardsFrameBuffer fragment(
        WebglBuffergeometryInstancingBillboardsVertexOutput inputValue)
    {
        auto circleTexture =
            sceneSet->textures->get(inputValue.entityID, 0u);
        const half4 sprite =
            circleTexture->sample(
                samplerResources->circleSampler,
                inputValue.texCoord);
        if (sprite.w <= half(0.5f))
        {
            discard_fragment();
        }
        const WebglBuffergeometryInstancingBillboardsMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 proceduralColor =
            webglBuffergeometryBillboardsHueToRgb(
                inputValue.proceduralScale / 5.0f);
        WebglBuffergeometryInstancingBillboardsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            sprite.xyz *
                half3(proceduralColor) *
                half3(material.colorMultiplier.xyz),
            sprite.w * half(material.colorMultiplier.w));
        return frameBuffer;
    }
};

/** Owns the sole Scene RenderSet, sampler, and deterministic readback targets. */
class WebglBuffergeometryInstancingBillboardsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryInstancingBillboardsSceneRenderSet> sceneSet;
    Sampler circleSampler;
    BindGroup<WebglBuffergeometryInstancingBillboardsSamplerResources> samplerResources;
    RenderClass<WebglBuffergeometryInstancingBillboardsMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates one Scene RenderSet and its single indexed-indirect pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglBuffergeometryInstancingBillboardsSceneRenderSet>();
        circleSampler = device->createSampler({
            .label = "WebglBuffergeometryInstancingBillboardsCircleSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 8,
            .maxAnisotropy = 1,
        });
        samplerResources =
            device->createBindGroup<
                WebglBuffergeometryInstancingBillboardsSamplerResources>(
                circleSampler);
        scenePass =
            device->createRenderClass<
                WebglBuffergeometryInstancingBillboardsMainPass>(
                sceneSet,
                samplerResources);
    }

    /** Allocates the host-sized color and depth attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryInstancingBillboardsRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryInstancingBillboardsDepth32",
            width,
            height,
            1u);
    }

    /** Updates RenderSet metadata and performs its only automatic draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryInstancingBillboardsFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass(
                "WebglBuffergeometryInstancingBillboardsScene",
                frameBuffer,
                scenePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene RenderSet and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
