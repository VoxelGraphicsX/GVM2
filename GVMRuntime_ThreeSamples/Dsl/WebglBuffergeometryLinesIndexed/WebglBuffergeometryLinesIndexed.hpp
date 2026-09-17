#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_INDEXED_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_INDEXED_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one screen-expanded corner of an indexed Koch line segment. */
struct WebglBuffergeometryLinesIndexedVertex
{
    float4 startPosition [[Attribute0]];
    float4 endPosition [[Attribute1]];
    float4 startColor [[Attribute2]];
    float4 endColor [[Attribute3]];
    float4 corner [[Attribute4]];
};

/** Stores the animated parent hierarchy transform and output viewport. */
struct WebglBuffergeometryLinesIndexedObjectData
{
    float4x4 modelViewProjection;
    float4 viewport;
};

/** Stores the required ordinary-entity instance component. */
struct WebglBuffergeometryLinesIndexedInstanceData
{
    float4 reserved;
};

/** Stores the opaque LineBasicMaterial multiplier. */
struct WebglBuffergeometryLinesIndexedMaterialData
{
    float4 color;
};

/** Defines the unique indexed-line Scene RenderSet. */
struct WebglBuffergeometryLinesIndexedSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry, object, instance, and material components. */
    constructor(
        BufferComponent<WebglBuffergeometryLinesIndexedVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglBuffergeometryLinesIndexedObjectData> objects,
        BufferComponent<WebglBuffergeometryLinesIndexedInstanceData> instances,
        BufferComponent<WebglBuffergeometryLinesIndexedMaterialData> materials)
    {
    }
};

/** Carries constant segment endpoints, colors, and entity identity. */
struct WebglBuffergeometryLinesIndexedVertexOutput
{
    float4 position [[Position]];
    float4 lineRaster [[Attribute0]];
    float3 startColor [[Attribute1]];
    float3 endColor [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglBuffergeometryLinesIndexedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear working-space channel to Three's output transfer. */
float webglBuffergeometryLinesIndexedLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all expanded indexed segments through one RenderSet indirect command. */
class WebglBuffergeometryLinesIndexedMainPass final : public IRenderClass
{
public:
    /** Configures the opaque double-sided triangle-list line expansion. */
    constructor(
        RenderSet<WebglBuffergeometryLinesIndexedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies hierarchy animation and expands one referenced segment in screen space. */
    WebglBuffergeometryLinesIndexedVertexOutput vertex(
        WebglBuffergeometryLinesIndexedVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryLinesIndexedObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryLinesIndexedInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 offset = float4(instanceData.reserved.xyz, 0.0f);
        const float4 startClip = mul(
            objectData.modelViewProjection, inputValue.startPosition + offset);
        const float4 endClip = mul(
            objectData.modelViewProjection, inputValue.endPosition + offset);
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 direction =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 tangent = direction /
            max(length(direction), 0.0001f);
        const float2 normal = float2(-direction.y, direction.x) /
            max(length(direction), 0.0001f);
        const bool useEnd = inputValue.corner.x > 0.5f;
        float4 clipPosition = useEnd ? endClip : startClip;
        const float endpointExtension = useEnd ? 0.75f : -0.75f;
        clipPosition.xy +=
            (normal * inputValue.corner.y + tangent * endpointExtension) /
            objectData.viewport.xy * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglBuffergeometryLinesIndexedVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.lineRaster = float4(
            (startNdc.x + 1.0f) * objectData.viewport.x,
            (1.0f - startNdc.y) * objectData.viewport.y,
            (endNdc.x + 1.0f) * objectData.viewport.x,
            (1.0f - endNdc.y) * objectData.viewport.y);
        outputValue.startColor = inputValue.startColor.xyz;
        outputValue.endColor = inputValue.endColor.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits the interpolated LineBasicMaterial vertex color. */
    WebglBuffergeometryLinesIndexedFrameBuffer fragment(
        WebglBuffergeometryLinesIndexedVertexOutput inputValue)
    {
        const WebglBuffergeometryLinesIndexedMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float2 startPoint = inputValue.lineRaster.xy;
        const float2 endPoint = inputValue.lineRaster.zw;
        const float2 direction = endPoint - startPoint;
        const float2 pointOffset = inputValue.position.xy - startPoint;
        const float diamondU = -pointOffset.x - pointOffset.y;
        const float diamondV = -pointOffset.x + pointOffset.y;
        const float directionU = direction.x + direction.y;
        const float directionV = direction.x - direction.y;
        float enterU = -100000.0f;
        float exitU = 100000.0f;
        if (abs(directionU) < 0.000001f)
        {
            if (abs(diamondU) > 0.5f) discard_fragment();
        }
        else
        {
            const float firstU = (-0.5f - diamondU) / directionU;
            const float secondU = (0.5f - diamondU) / directionU;
            enterU = min(firstU, secondU);
            exitU = max(firstU, secondU);
        }
        float enterV = -100000.0f;
        float exitV = 100000.0f;
        if (abs(directionV) < 0.000001f)
        {
            if (abs(diamondV) > 0.5f) discard_fragment();
        }
        else
        {
            const float firstV = (-0.5f - diamondV) / directionV;
            const float secondV = (0.5f - diamondV) / directionV;
            enterV = min(firstV, secondV);
            exitV = max(firstV, secondV);
        }
        const float diamondEnter = max(max(enterU, enterV), 0.0f);
        const float diamondExit = min(min(exitU, exitV), 1.0f);
        const float interpolation = clamp(
            dot(pointOffset, direction) /
                max(dot(direction, direction), 0.0001f),
            0.0f,
            1.0f);
        clip(diamondExit - diamondEnter);
        clip(0.999999f - diamondExit);
        const float3 linearColor = lerp(
            inputValue.startColor,
            inputValue.endColor,
            interpolation) * materialData.color.xyz;
        WebglBuffergeometryLinesIndexedFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webglBuffergeometryLinesIndexedLinearToSrgb(linearColor.x),
                webglBuffergeometryLinesIndexedLinearToSrgb(linearColor.y),
                webglBuffergeometryLinesIndexedLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and deterministic single-sample output. */
class WebglBuffergeometryLinesIndexedRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryLinesIndexedSceneRenderSet> sceneSet;
    RenderClass<WebglBuffergeometryLinesIndexedMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one Scene Set and its dedicated geometry pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglBuffergeometryLinesIndexedSceneRenderSet>();
        mainPass =
            device->createRenderClass<WebglBuffergeometryLinesIndexedMainPass>(
                sceneSet);
    }

    /** Allocates ordinary single-sample RGBA8 and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglBuffergeometryLinesIndexedRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryLinesIndexedDepth32", width, height, 1u);
    }

    /** Updates the Set and submits its automatic indexed-indirect draw. */
    void render() override
    {
        sceneSet->update();
        WebglBuffergeometryLinesIndexedFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglBuffergeometryLinesIndexedMain", frameBuffer, mainPass())
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

    /** Releases the unique Set and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
