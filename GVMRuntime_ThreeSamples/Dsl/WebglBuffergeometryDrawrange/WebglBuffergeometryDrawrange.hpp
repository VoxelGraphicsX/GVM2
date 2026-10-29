#pragma once

#include "UGL.h"

using namespace UGL;

/** Stores one point- or line-expanded corner in the unified Scene geometry. */
struct WebglBuffergeometryDrawrangeVertex
{
    float4 startPosition [[Attribute0]];
    float4 endPosition [[Attribute1]];
    float4 startColor [[Attribute2]];
    float4 endColor [[Attribute3]];
    float4 corner [[Attribute4]];
    float4 lineStartClip [[Attribute5]];
    float4 lineEndClip [[Attribute6]];
    float4 directClip [[Attribute7]];
};

/** Stores the animated Group transform and target viewport. */
struct WebglBuffergeometryDrawrangeObjectData
{
    float4x4 modelViewProjection;
    float4 viewport;
};

/** Stores the required ordinary RenderSet instance component. */
struct WebglBuffergeometryDrawrangeInstanceData
{
    float4 reserved;
};

/** Stores additive phase, base color, and object kind for the uber material. */
struct WebglBuffergeometryDrawrangeMaterialData
{
    float4 colorAndKind;
};

/** Defines the sole Scene RenderSet for helper, points, and connection lines. */
struct WebglBuffergeometryDrawrangeSceneRenderSet : public IRenderSet
{
    /** Declares one packed vertex/index store and the required Scene components. */
    constructor(
        BufferComponent<WebglBuffergeometryDrawrangeVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglBuffergeometryDrawrangeObjectData> objects,
        BufferComponent<WebglBuffergeometryDrawrangeInstanceData> instances,
        BufferComponent<WebglBuffergeometryDrawrangeMaterialData> materials)
    {
    }
};

/** Carries expanded raster coordinates, color, and entity identity to the fragment stage. */
struct WebglBuffergeometryDrawrangeVertexOutput
{
    float4 position [[Position]];
    float4 linePixels [[Attribute0]];
    float3 startColor [[Attribute1]];
    float3 endColor [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the single-sample RGBA8 Scene target and depth buffer. */
struct WebglBuffergeometryDrawrangeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only output target. */
struct WebglBuffergeometryDrawrangeOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel using the r185 output sRGB transfer. */
float webglBuffergeometryDrawrangeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Draws all three upstream renderables through the unique indexed-indirect Set. */
class WebglBuffergeometryDrawrangeMainPass final : public IRenderClass
{
public:
    /** Binds one Scene RenderSet and matches additive transparent depth state. */
    constructor(
        RenderSet<WebglBuffergeometryDrawrangeSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
    }

private:
    /** Projects one packed corner and expands it in deterministic screen pixels. */
    WebglBuffergeometryDrawrangeVertexOutput vertex(
        WebglBuffergeometryDrawrangeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryDrawrangeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryDrawrangeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const float4 instanceOffset = float4(instanceData.reserved.xyz, 0.0f);
        const float4 startClip = mul(
            objectData.modelViewProjection,
            inputValue.startPosition + instanceOffset);
        const float4 endClip = mul(
            objectData.modelViewProjection,
            inputValue.endPosition + instanceOffset);
        const bool isPointPrimitive = materialData.colorAndKind.w > 1.5f;
        float4 clipPosition;
        float4 linePixels = float4(0.0f);
        if (isPointPrimitive)
        {
            clipPosition = startClip;
            const float2 pointViewport = objectData.viewport.xy;
            const float2 pointCenter =
                (startClip.xy / startClip.w + float2(1.0f)) *
                (pointViewport * 0.5f);
            const float2 topPointCenter = float2(
                pointCenter.x,
                pointViewport.y - pointCenter.y);
            const float2 halfPointSize = float2(1.5f);
            const float2 roundedCoverageLow =
                floor((topPointCenter - halfPointSize) * 16.0f + 0.5f) /
                16.0f;
            const float2 roundedCoverageHigh =
                floor((topPointCenter + halfPointSize) * 16.0f + 0.5f) /
                16.0f;
            const float2 coverageSize =
                roundedCoverageHigh - roundedCoverageLow;
            const float2 coverageEdge = floor(
                float2(
                    topPointCenter.x - halfPointSize.x,
                    topPointCenter.y -
                        (coverageSize.y - halfPointSize.y)) * 256.0f +
                float2(0.5f, 0.75f)) /
                256.0f;
            const float2 coverageCenter =
                coverageEdge + coverageSize * 0.5f;
            clipPosition.xy = float2(
                coverageCenter.x / (pointViewport.x * 0.5f) - 1.0f,
                (pointViewport.y - coverageCenter.y) /
                    (pointViewport.y * 0.5f) - 1.0f) * clipPosition.w;
            clipPosition.x += inputValue.corner.x * coverageSize.x /
                (pointViewport.x * 0.5f) * clipPosition.w;
            clipPosition.y += inputValue.corner.y * coverageSize.y /
                (pointViewport.y * 0.5f) * clipPosition.w;
        }
        else
        {
            const float2 startNdc = startClip.xy / startClip.w;
            const float2 endNdc = endClip.xy / endClip.w;
            const float2 directionPixels =
                (endNdc - startNdc) * objectData.viewport.xy;
            const float2 tangent = directionPixels /
                max(length(directionPixels), 0.0001f);
            const float2 normal = float2(-directionPixels.y, directionPixels.x) /
                max(length(directionPixels), 0.0001f);
            const bool useEnd = inputValue.corner.x > 0.5f;
            clipPosition = useEnd ? endClip : startClip;
            const float endpointExtension = 0.0f;
            clipPosition.xy +=
                (normal * inputValue.corner.y + tangent * endpointExtension) /
                objectData.viewport.xy * clipPosition.w;
            linePixels = float4(
                (startNdc.x + 1.0f) * objectData.viewport.x,
                (1.0f - startNdc.y) * objectData.viewport.y,
                (endNdc.x + 1.0f) * objectData.viewport.x,
                (1.0f - endNdc.y) * objectData.viewport.y);
        }
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglBuffergeometryDrawrangeVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.linePixels = linePixels;
        outputValue.startColor = inputValue.startColor.xyz;
        outputValue.endColor = inputValue.endColor.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies exact line coverage and the additive point/line/helper material phases. */
    WebglBuffergeometryDrawrangeFrameBuffer fragment(
        WebglBuffergeometryDrawrangeVertexOutput inputValue)
    {
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float kind = materialData.colorAndKind.w;
        // BoxHelper and connection segments are owned by their dedicated
        // passes; this pass renders only the point-cloud entity.
        if (kind < 1.5f || kind > 2.5f) discard_fragment();
        const float interpolation = clamp(
            dot(inputValue.position.xy - inputValue.linePixels.xy,
                inputValue.linePixels.zw - inputValue.linePixels.xy) /
                max(dot(inputValue.linePixels.zw - inputValue.linePixels.xy,
                        inputValue.linePixels.zw - inputValue.linePixels.xy),
                    0.0001f),
            0.0f,
            1.0f);
        const float3 linearColor = kind < 0.5f
            ? materialData.colorAndKind.xyz
            : kind < 1.5f
                ? lerp(inputValue.startColor, inputValue.endColor, interpolation)
                : float3(1.0f);
        WebglBuffergeometryDrawrangeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglBuffergeometryDrawrangeLinearToSrgb(linearColor.x)),
            half(webglBuffergeometryDrawrangeLinearToSrgb(linearColor.y)),
            half(webglBuffergeometryDrawrangeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Carries expanded line coordinates, endpoint colors, and entity identity. */
struct WebglBuffergeometryDrawrangeNativeLineOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float4 lineRaster [[Attribute1]];
    float3 startColor [[Attribute2]];
    float3 endColor [[Attribute3]];
    uint entityID [[Attribute4]];
};

/** Tests a finite one-pixel segment against WebGL's diamond-exit rule. */
bool webglBuffergeometryDrawrangeCoversDiamond(
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

/** Draws the BoxHelper before transparent particles so its depth order matches Three.js. */
class WebglBuffergeometryDrawrangeBoxLinePass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and keeps the helper's additive depth state. */
    constructor(
        RenderSet<WebglBuffergeometryDrawrangeSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
    }

private:
    /** Projects one helper endpoint and forwards its RenderSet identity. */
    WebglBuffergeometryDrawrangeNativeLineOutput vertex(
        WebglBuffergeometryDrawrangeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const WebglBuffergeometryDrawrangeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryDrawrangeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const bool useEnd = inputValue.corner.x > 0.5f;
        const float4 localPosition = useEnd
            ? inputValue.endPosition
            : inputValue.startPosition;
        float4 clipPosition = mul(
            objectData.modelViewProjection,
            localPosition + float4(instanceData.reserved.xyz, 0.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglBuffergeometryDrawrangeNativeLineOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = materialData.colorAndKind.xyz;
        outputValue.lineRaster = float4(0.0f);
        outputValue.startColor = outputValue.color;
        outputValue.endColor = outputValue.color;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits only the BoxHelper's grey additive line material. */
        WebglBuffergeometryDrawrangeFrameBuffer fragment(
            WebglBuffergeometryDrawrangeNativeLineOutput inputValue)
        {
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (materialData.colorAndKind.w > 0.5f)
            discard_fragment();
        const float3 color = inputValue.color;
        WebglBuffergeometryDrawrangeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglBuffergeometryDrawrangeLinearToSrgb(color.x)),
            half(webglBuffergeometryDrawrangeLinearToSrgb(color.y)),
            half(webglBuffergeometryDrawrangeLinearToSrgb(color.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws connection entities through exact single-sample triangle envelopes. */
class WebglBuffergeometryDrawrangeNativeLinePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and evaluates the single-sample line contract in the fragment stage. */
    constructor(
        RenderSet<WebglBuffergeometryDrawrangeSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
    }

private:
    /** Expands one connection into a conservative screen-space triangle envelope. */
    WebglBuffergeometryDrawrangeNativeLineOutput vertex(
        WebglBuffergeometryDrawrangeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const WebglBuffergeometryDrawrangeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        if (inputValue.directClip.w > 0.0f)
        {
            WebglBuffergeometryDrawrangeNativeLineOutput outputValue;
            outputValue.position = inputValue.directClip;
            outputValue.position.y = -outputValue.position.y;
            outputValue.color = inputValue.startColor.xyz;
            outputValue.lineRaster = float4(0.0f);
            outputValue.startColor = inputValue.startColor.xyz;
            outputValue.endColor = inputValue.endColor.xyz;
            outputValue.entityID = renderEntityID;
            return outputValue;
        }
        const bool isConnection = materialData.colorAndKind.w > 0.5f &&
            materialData.colorAndKind.w < 1.5f &&
            inputValue.lineStartClip.w > 0.0f &&
            inputValue.lineEndClip.w > 0.0f;
        if (isConnection)
        {
            const float4 startClip = inputValue.lineStartClip;
            const float4 endClip = inputValue.lineEndClip;
            const float2 startNdc = startClip.xy / startClip.w;
            const float2 endNdc = endClip.xy / endClip.w;
            const float2 directionPixels = (endNdc - startNdc) * objectData.viewport.xy;
            const float directionLength = max(length(directionPixels), 0.0001f);
            const float2 tangent = directionPixels / directionLength;
            const float2 normal = float2(-directionPixels.y, directionPixels.x) / directionLength;
            const bool useEnd = inputValue.corner.x > 0.5f;
            const float endpointExtension = 0.0f;
            const float endpointSign = useEnd ? 1.0f : -1.0f;
            const float4 endpointClip = useEnd ? endClip : startClip;
            float4 clipPosition = endpointClip;
            clipPosition.xy += (normal * inputValue.corner.y +
                tangent * endpointSign * endpointExtension) /
                objectData.viewport.xy * clipPosition.w;
            clipPosition.y = -clipPosition.y;
            clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
            WebglBuffergeometryDrawrangeNativeLineOutput outputValue;
            outputValue.position = clipPosition;
            // The line material is vertex-colored in the upstream example:
            // each endpoint carries the distance-derived alpha as an RGB
            // value, and additive blending consumes that interpolated value.
            // Preserve it in the native LineList path instead of replacing it
            // with the white material color (which would over-brighten every
            // connection segment).
            outputValue.color = useEnd
                ? inputValue.endColor.xyz
                : inputValue.startColor.xyz;
            outputValue.lineRaster = float4(
                (startNdc.x + 1.0f) * objectData.viewport.x * 0.5f,
                (1.0f - startNdc.y) * objectData.viewport.y * 0.5f,
                (endNdc.x + 1.0f) * objectData.viewport.x * 0.5f,
                (1.0f - endNdc.y) * objectData.viewport.y * 0.5f);
            outputValue.startColor = outputValue.color;
            outputValue.endColor = outputValue.color;
            outputValue.entityID = renderEntityID;
            return outputValue;
        }
        const WebglBuffergeometryDrawrangeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 instanceOffset = float4(instanceData.reserved.xyz, 0.0f);
        const bool useEnd = inputValue.corner.x > 0.5f;
        float4 clipPosition = mul(
            objectData.modelViewProjection,
            (useEnd ? inputValue.endPosition : inputValue.startPosition) +
                instanceOffset);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglBuffergeometryDrawrangeNativeLineOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = useEnd
            ? inputValue.endColor.xyz
            : inputValue.startColor.xyz;
        outputValue.lineRaster = float4(0.0f);
        outputValue.startColor = inputValue.startColor.xyz;
        outputValue.endColor = inputValue.endColor.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits the connection color after filtering the BoxHelper entity. */
    WebglBuffergeometryDrawrangeFrameBuffer fragment(
        WebglBuffergeometryDrawrangeNativeLineOutput inputValue)
    {
        const WebglBuffergeometryDrawrangeMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (materialData.colorAndKind.w < 0.5f ||
            materialData.colorAndKind.w > 1.5f)
            discard_fragment();
        const float2 segmentStart = inputValue.lineRaster.xy;
        const float2 segmentEnd = inputValue.lineRaster.zw;
        const bool hasRasterLine = dot(segmentEnd - segmentStart,
                                       segmentEnd - segmentStart) > 0.0001f;
        if (hasRasterLine && !webglBuffergeometryDrawrangeCoversDiamond(
                inputValue.position.xy, segmentStart, segmentEnd))
            discard_fragment();
        const float interpolation = hasRasterLine
            ? clamp(dot(inputValue.position.xy - segmentStart,
                        segmentEnd - segmentStart) /
                    max(dot(segmentEnd - segmentStart,
                            segmentEnd - segmentStart), 0.0001f),
                0.0f, 1.0f)
            : 0.0f;
        const float3 color = hasRasterLine
            ? lerp(inputValue.startColor, inputValue.endColor, interpolation)
            : inputValue.color;
        const float3 displayColor = float3(
            webglBuffergeometryDrawrangeLinearToSrgb(color.x),
            webglBuffergeometryDrawrangeLinearToSrgb(color.y),
            webglBuffergeometryDrawrangeLinearToSrgb(color.z));
        WebglBuffergeometryDrawrangeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(displayColor.x),
            half(displayColor.y),
            half(displayColor.z),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique DrawRange RenderSet and the single-sample output attachments. */
class WebglBuffergeometryDrawrangeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryDrawrangeSceneRenderSet> sceneSet;
    RenderClass<WebglBuffergeometryDrawrangeMainPass> mainPass;
    RenderClass<WebglBuffergeometryDrawrangeBoxLinePass> boxLinePass;
    RenderClass<WebglBuffergeometryDrawrangeNativeLinePass> nativeLinePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and its ordered geometry passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglBuffergeometryDrawrangeSceneRenderSet>();
        mainPass = device->createRenderClass<WebglBuffergeometryDrawrangeMainPass>(sceneSet);
        boxLinePass = device->createRenderClass<WebglBuffergeometryDrawrangeBoxLinePass>(sceneSet);
        nativeLinePass = device->createRenderClass<WebglBuffergeometryDrawrangeNativeLinePass>(sceneSet);
    }

    /** Allocates single-sample output and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture("WebglBuffergeometryDrawrangeOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglBuffergeometryDrawrangeDepth", width, height, 1u);
    }

    /** Renders helper, points, and connection lines in upstream child order. */
    void render() override
    {
        sceneSet->update();
        WebglBuffergeometryDrawrangeFrameBuffer clearFrameBuffer;
        clearFrameBuffer.color = outputTexture->createView();
        clearFrameBuffer.color.loadOp = LoadOp::Clear;
        clearFrameBuffer.color.storeOp = StoreOp::Store;
        clearFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        clearFrameBuffer.depth = depthTexture->createView();
        clearFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        clearFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        clearFrameBuffer.depth.depthClearValue = 1.0f;
        WebglBuffergeometryDrawrangeFrameBuffer loadedFrameBuffer;
        loadedFrameBuffer.color = outputTexture->createView();
        loadedFrameBuffer.color.loadOp = LoadOp::Load;
        loadedFrameBuffer.color.storeOp = StoreOp::Store;
        loadedFrameBuffer.depth = depthTexture->createView();
        loadedFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadedFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass(
                "WebglBuffergeometryDrawrangeBox", clearFrameBuffer, boxLinePass())
            ->renderPass(
                "WebglBuffergeometryDrawrangeNativeLines", loadedFrameBuffer,
                nativeLinePass())
            ->renderPass(
                "WebglBuffergeometryDrawrangeMain", loadedFrameBuffer, mainPass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned texture used by host readback. */
    auto getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the Set and single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};
