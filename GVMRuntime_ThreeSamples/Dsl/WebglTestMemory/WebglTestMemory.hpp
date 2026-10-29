#ifndef GVM_THREE_WEBGL_TEST_MEMORY_HPP
#define GVM_THREE_WEBGL_TEST_MEMORY_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglTestMemoryTextureCapacity = 2u;

/** Stores one corner of an expanded r185 wireframe segment. */
struct WebglTestMemoryVertex
{
    float4 position [[Attribute0]];
    float4 lineEnd [[Attribute1]];
    float4 lineData [[Attribute2]];
};

/** Stores the frozen model-view, projection, and viewport state. */
struct WebglTestMemoryObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
};

/** Stores the mandatory identity instance record. */
struct WebglTestMemoryInstanceData
{
    float4 reserved;
};

/** Stores the linear CanvasTexture multiplier. */
struct WebglTestMemoryMaterialData
{
    float4 colorAndOpacity;
};

/** Stores the entity lifetime generation and visibility contract. */
struct WebglTestMemoryRenderFlags
{
    float4 generationAndVisibility;
};

/** Defines the unique dynamically reallocated Scene RenderSet. */
struct WebglTestMemorySceneRenderSet : public IRenderSet
{
    /** Declares packed wireframe geometry and the fixed solid texture slot. */
    constructor(
        BufferComponent<WebglTestMemoryVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglTestMemoryObjectData> objects,
        BufferComponent<WebglTestMemoryInstanceData> instances,
        BufferComponent<WebglTestMemoryMaterialData> materials,
        (TextureComponent<float4, WebglTestMemoryTextureCapacity> textures),
        BufferComponent<WebglTestMemoryRenderFlags> renderFlags)
    {
    }
};

/** Binds the one-pixel CanvasTexture sampler. */
struct WebglTestMemoryResources final : public IBindGroup
{
    /** Declares a nearest clamp sampler because the texture is spatially constant. */
    constructor(Sampler solidSampler [[Binding0]])
    {
    }
};

/** Carries exact line-raster coordinates and entity identity. */
struct WebglTestMemoryVertexOutput
{
    float4 position [[Position]];
    float4 linePixels [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglTestMemoryFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear CanvasTexture channel with Three r185's sRGB transfer. */
float webglTestMemoryLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Tests a finite segment against ANGLE's one-pixel diamond-exit rule. */
bool webglTestMemoryCoversDiamond(
    float2 pixelCenter,
    float2 segmentStart,
    float2 segmentEnd)
{
    const float2 transformedStart = float2(
        segmentStart.x + segmentStart.y - pixelCenter.x - pixelCenter.y,
        segmentStart.x - segmentStart.y - pixelCenter.x + pixelCenter.y);
    const float2 transformedEnd = float2(
        segmentEnd.x + segmentEnd.y - pixelCenter.x - pixelCenter.y,
        segmentEnd.x - segmentEnd.y - pixelCenter.x + pixelCenter.y);
    const float2 delta = transformedEnd - transformedStart;
    const float extent = 0.5f;
    float enter = 0.0f;
    float exit = 1.0f;
    if (abs(delta.x) < 0.000001f)
    {
        if (abs(transformedStart.x) > extent) return false;
    }
    else
    {
        const float first = (-extent - transformedStart.x) / delta.x;
        const float second = (extent - transformedStart.x) / delta.x;
        enter = max(enter, min(first, second));
        exit = min(exit, max(first, second));
    }
    if (abs(delta.y) < 0.000001f)
    {
        if (abs(transformedStart.y) > extent) return false;
    }
    else
    {
        const float first = (-extent - transformedStart.y) / delta.y;
        const float second = (extent - transformedStart.y) / delta.y;
        enter = max(enter, min(first, second));
        exit = min(exit, max(first, second));
    }
    return enter <= exit && exit >= 0.0f && enter <= 1.0f;
}

/** Draws the current temporary sphere entity through Set-only indirect metadata. */
class WebglTestMemoryScenePass final : public IRenderClass
{
public:
    /** Binds the Scene's only Set and its fixed texture sampler. */
    constructor(
        RenderSet<WebglTestMemorySceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglTestMemoryResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects and expands one triangle edge using RenderSet builtins. */
    WebglTestMemoryVertexOutput vertex(
        WebglTestMemoryVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglTestMemoryObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglTestMemoryInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 startView = mul(
            objectData.modelView,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 endView = mul(objectData.modelView, inputValue.lineEnd);
        float4 startClip = mul(objectData.projection, startView);
        const float4 endClip = mul(objectData.projection, endView);
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 directionPixels =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 lineNormal = float2(
            -directionPixels.y, directionPixels.x) /
            max(length(directionPixels), 0.0001f);
        const bool useEnd = inputValue.lineData.x > 0.5f;
        float4 outputClip = useEnd ? endClip : startClip;
        outputClip.xy += lineNormal * inputValue.lineData.y /
            objectData.viewport.xy * outputClip.w;
        const float2 rasterTieBreak = float2(0.0f, 0.0001f);
        const float4 linePixels = float4(
            float2(startNdc.x, -startNdc.y) * objectData.viewport.xy +
                objectData.viewport.xy + rasterTieBreak,
            float2(endNdc.x, -endNdc.y) * objectData.viewport.xy +
                objectData.viewport.xy + rasterTieBreak);
        outputClip.y = -outputClip.y;
        outputClip.z = (outputClip.z + outputClip.w) * 0.5f;
        WebglTestMemoryVertexOutput outputValue;
        outputValue.position = outputClip;
        outputValue.linePixels = linePixels;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies exact single-sample WebGL line coverage and CanvasTexture color. */
    WebglTestMemoryFrameBuffer fragment(
        WebglTestMemoryVertexOutput inputValue)
    {
        const WebglTestMemoryRenderFlags flags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        if (flags.generationAndVisibility.y < 0.5f ||
            !webglTestMemoryCoversDiamond(
                inputValue.position.xy,
                inputValue.linePixels.xy,
                inputValue.linePixels.zw))
        {
            discard_fragment();
        }
        const WebglTestMemoryMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        auto texture = sceneSet->textures->get(inputValue.entityID, 0u);
        const float4 sampled = float4(texture->sampleLevel(
            resources->solidSampler, float2(0.5f), 0.0f));
        const float3 linearColor =
            sampled.xyz * material.colorAndOpacity.xyz;
        WebglTestMemoryFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglTestMemoryLinearToSrgb(linearColor.x)),
            half(webglTestMemoryLinearToSrgb(linearColor.y)),
            half(webglTestMemoryLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dynamically updated Scene Set and single-sample output. */
class WebglTestMemoryRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglTestMemorySceneRenderSet> sceneSet;
    Sampler solidSampler;
    BindGroup<WebglTestMemoryResources> resources;
    RenderClass<WebglTestMemoryScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and immutable solid-texture sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglTestMemorySceneRenderSet>();
        solidSampler = device->createSampler({
            .label = "WebglTestMemorySolidSampler",
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
        resources = device->createBindGroup<WebglTestMemoryResources>(
            solidSampler);
        scenePass = device->createRenderClass<WebglTestMemoryScenePass>(
            sceneSet, resources);
    }

    /** Allocates the ordinary RGBA8 and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglTestMemoryRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglTestMemoryDepth32", width, height, 1u);
    }

    /** Clears white, draws the current Set entity, and presents. */
    void render() override
    {
        sceneSet->update();
        WebglTestMemoryFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglTestMemoryScene", frameBuffer, scenePass())
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

    /** Releases the Scene Set and both owned attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
