#ifndef GVM_THREE_WEBGL_LOADER_BVH_HPP
#define GVM_THREE_WEBGL_LOADER_BVH_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one CPU-expanded skeleton or grid line segment. */
struct WebglLoaderBvhVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 endpointSide [[Attribute2]];
    float4 color [[Attribute3]];
};

/** Stores camera matrices and viewport dimensions for the line scene. */
struct WebglLoaderBvhObjectData
{
    float4x4 modelViewProjection;
    float4 viewportAndReserved;
};

/** Stores the mandatory one-entry non-instanced instance component. */
struct WebglLoaderBvhInstanceData
{
    float4 reserved;
};

/** Stores line phase and depth/visibility flags. */
struct WebglLoaderBvhMaterialData
{
    float4 phaseAndFlags;
};

/** Owns GridHelper and SkeletonHelper line entities in one Scene Set. */
struct WebglLoaderBvhSceneRenderSet : public IRenderSet
{
    /** Declares the packed line union and per-entity component ABI. */
    constructor(
        BufferComponent<WebglLoaderBvhVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderBvhObjectData> objects,
        BufferComponent<WebglLoaderBvhInstanceData> instances,
        BufferComponent<WebglLoaderBvhMaterialData> materials)
    {
    }
};

/** Carries expanded clip position and interpolated endpoint color. */
struct WebglLoaderBvhVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the ordinary single-sample line output target. */
struct WebglLoaderBvhFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear vertex-color channel to the renderer's sRGB canvas. */
float webglLoaderBvhLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Emits one endpoint of a segment through the native one-pixel line path. */
WebglLoaderBvhVertexOutput webglLoaderBvhTransform(
    IN RenderSet<WebglLoaderBvhSceneRenderSet> sceneSet,
    WebglLoaderBvhVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLoaderBvhObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglLoaderBvhInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 start = inputValue.segmentStart +
        float4(instanceData.reserved.xyz, 0.0f);
    const float4 end = inputValue.segmentEnd +
        float4(instanceData.reserved.xyz, 0.0f);
    const float4 startClip = mul(objectData.modelViewProjection, start);
    const float4 endClip = mul(objectData.modelViewProjection, end);
    const bool useEnd = inputValue.endpointSide.x > 0.5f;
    float4 clipPosition = useEnd ? endClip : startClip;
    clipPosition.y = -clipPosition.y;
    // Preserve Three.js' deterministic raster tie-break for GridHelper's
    // horizontal center line without perturbing the skeleton segments.
    if (inputValue.endpointSide.y > 0.5f)
    {
        clipPosition.y += 2.0f / objectData.viewportAndReserved.y *
            clipPosition.w;
    }
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglLoaderBvhVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.color = inputValue.color;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws both line entities via the one Scene RenderSet. */
class WebglLoaderBvhMainPass final : public IRenderClass
{
public:
    /** Configures transparent line output with existing depth semantics. */
    constructor(RenderSet<WebglLoaderBvhSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Resolves entity and instance IDs from the RenderSet indexed draw. */
    WebglLoaderBvhVertexOutput vertex(
        WebglLoaderBvhVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderBvhTransform(sceneSet, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Writes the interpolated SkeletonHelper/GridHelper color. */
    WebglLoaderBvhFrameBuffer fragment(WebglLoaderBvhVertexOutput inputValue)
    {
        const WebglLoaderBvhMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        float3 displayColor = inputValue.color.xyz;
        // SkeletonHelper uses vertex colors in linear working space and the
        // WebGL renderer converts them at the sRGB canvas boundary. GridHelper
        // stores its already-quantized helper colors, so preserve those values.
        if (materialData.phaseAndFlags.x > 0.5f)
        {
            displayColor = float3(
                webglLoaderBvhLinearToSrgb(displayColor.x),
                webglLoaderBvhLinearToSrgb(displayColor.y),
                webglLoaderBvhLinearToSrgb(displayColor.z));
        }
        WebglLoaderBvhFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(inputValue.color.w));
        return frameBuffer;
    }
};

/** Owns the single Scene Set and fixed single-sample output. */
class WebglLoaderBvhRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderBvhSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderBvhMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique line RenderSet and its generated Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderBvhSceneRenderSet>();
        mainPass = device->createRenderClass<WebglLoaderBvhMainPass>(sceneSet);
    }

    /** Allocates ordinary single-sample RGBA8/depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLoaderBvhColor", width, height, 1u);
        outputDepth = device->createTexture("WebglLoaderBvhDepth", width, height, 1u);
    }

    /** Draws GridHelper and SkeletonHelper through indexed-indirect Set metadata. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderBvhFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.9333333f, 0.9333333f, 0.9333333f, 1.0f};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderBvhMain", frameBuffer, mainPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the line Set and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
