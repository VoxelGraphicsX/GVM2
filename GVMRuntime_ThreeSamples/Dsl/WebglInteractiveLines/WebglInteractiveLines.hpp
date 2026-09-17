#ifndef GVM_THREE_WEBGL_INTERACTIVE_LINES_HPP
#define GVM_THREE_WEBGL_INTERACTIVE_LINES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one endpoint-expanded line segment vertex. */
struct WebglInteractiveLinesVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 endpointSide [[Attribute2]];
};

/** Stores an entity's parent/object matrix. */
struct WebglInteractiveLinesObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewportAndReserved;
};

/** Stores the mandatory one-entry instance component. */
struct WebglInteractiveLinesInstanceData
{
    float4 reserved;
};

/** Stores one line material color and hit marker flag. */
struct WebglInteractiveLinesMaterialData
{
    float4 colorAndFlags;
};

/** Defines the one Scene RenderSet containing every expanded line entity. */
struct WebglInteractiveLinesSceneRenderSet : public IRenderSet
{
    /** Declares unified line vertex/index and per-entity component storage. */
    constructor(
        BufferComponent<WebglInteractiveLinesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInteractiveLinesObjectData> objects,
        BufferComponent<WebglInteractiveLinesInstanceData> instances,
        BufferComponent<WebglInteractiveLinesMaterialData> materials)
    {
    }
};

/** Carries clip position and entity identity to the fragment stage. */
struct WebglInteractiveLinesVertexOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
};

/** Defines the ordinary single-sample color/depth target. */
struct WebglInteractiveLinesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear channel to the canvas sRGB transfer. */
float webglInteractiveLinesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Expands one line endpoint in screen space through the RenderSet object data. */
WebglInteractiveLinesVertexOutput webglInteractiveLinesTransformVertex(
    IN RenderSet<WebglInteractiveLinesSceneRenderSet> sceneSet,
    WebglInteractiveLinesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglInteractiveLinesObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglInteractiveLinesInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 startPosition = inputValue.segmentStart + float4(instanceData.reserved.xyz, 0.0f);
    const float4 endPosition = inputValue.segmentEnd + float4(instanceData.reserved.xyz, 0.0f);
    const float4 startClip = mul(objectData.modelViewProjection, startPosition);
    const float4 endClip = mul(objectData.modelViewProjection, endPosition);
    const bool useEnd = inputValue.endpointSide.x > 0.5f;
    float4 clipPosition = useEnd ? endClip : startClip;
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglInteractiveLinesVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Shades one expanded line with its per-entity color. */
WebglInteractiveLinesFrameBuffer webglInteractiveLinesShade(
    IN RenderSet<WebglInteractiveLinesSceneRenderSet> sceneSet,
    WebglInteractiveLinesVertexOutput inputValue)
{
    const WebglInteractiveLinesMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 linearColor = materialData.colorAndFlags.xyz;
    WebglInteractiveLinesFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webglInteractiveLinesLinearToSrgb(linearColor.x),
        webglInteractiveLinesLinearToSrgb(linearColor.y),
        webglInteractiveLinesLinearToSrgb(linearColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws all line triangles through the single Scene RenderSet. */
class WebglInteractiveLinesScenePass final : public IRenderClass
{
public:
    /** Configures opaque depth-tested line expansion rendering. */
    constructor(RenderSet<WebglInteractiveLinesSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and mandatory instance IDs from the RenderSet draw. */
    WebglInteractiveLinesVertexOutput vertex(
        WebglInteractiveLinesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveLinesTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the line material color. */
    WebglInteractiveLinesFrameBuffer fragment(WebglInteractiveLinesVertexOutput inputValue)
    {
        const WebglInteractiveLinesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (materialData.colorAndFlags.w < 0.5f) discard_fragment();
        const WebglInteractiveLinesFrameBuffer shaded =
            webglInteractiveLinesShade(sceneSet, inputValue);
        WebglInteractiveLinesFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns the one line Scene RenderSet and single-sample output. */
class WebglInteractiveLinesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractiveLinesSceneRenderSet> sceneSet;
    RenderClass<WebglInteractiveLinesScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique line RenderSet and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractiveLinesSceneRenderSet>();
        scenePass = device->createRenderClass<WebglInteractiveLinesScenePass>(sceneSet);
    }

    /** Allocates ordinary single-sample attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglInteractiveLinesColor", width, height, 1u);
        outputDepth = device->createTexture("WebglInteractiveLinesDepth", width, height, 1u);
    }

    /** Submits one RenderSet indexed-indirect line Scene pass. */
    void render() override
    {
        sceneSet->update();
        WebglInteractiveLinesFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.9411765, 0.9411765, 0.9411765, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglInteractiveLinesScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases RenderSet and explicit single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglInteractiveLinesRenderer
#undef WebglInteractiveLinesFrameBuffer
#undef WebglInteractiveLinesScenePass
#undef WebglInteractiveLinesSceneRenderSet

#endif
