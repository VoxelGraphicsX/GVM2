#ifndef GVM_THREE_PHASE1_LINES_POINTS_RENDER_SET_HPP
#define GVM_THREE_PHASE1_LINES_POINTS_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one endpoint-expanded triangle vertex for a normalized line segment. */
struct Phase1LineVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 startColor [[Attribute2]];
    float4 endColor [[Attribute3]];
    float4 endpointSideAndDistance [[Attribute4]];
};

/** Stores one line entity's transforms, target extent, and fog range. */
struct Phase1LineObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewportAndFog;
};

/** Stores the mandatory non-instanced entry in the shared Scene component schema. */
struct Phase1LineInstanceData
{
    float4 translation;
};

/** Stores base color, dash parameters, and material feature flags for one entity. */
struct Phase1LineMaterialData
{
    float4 baseColor;
    float4 dashAndFlags;
    float4 fogColor;
};

/** Defines the only RenderSet owned by either supported line example Scene. */
struct Phase1LinesSceneRenderSet : public IRenderSet
{
    /** Declares the consolidated geometry and required object, instance, and material components. */
    constructor(BufferComponent<Phase1LineVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<Phase1LineObjectData> objects,
                BufferComponent<Phase1LineInstanceData> instances,
                BufferComponent<Phase1LineMaterialData> materials)
    {
    }
};

/** Stores the centered raster offset used by the single-sample Scene pass. */
struct Phase1LineInvocationData
{
    float4 sampleOffsetAndReserved;
};

/** Binds the centered raster offset beside the unique Scene RenderSet. */
struct Phase1LineInvocationResources final : public IBindGroup
{
    /** Declares the current Scene pass sample offset. */
    constructor(UniformBuffer<Phase1LineInvocationData> invocation [[Binding0]])
    {
    }
};

/** Carries interpolated line color, distance, fog depth, and entity identity. */
struct Phase1LineVertexOutput
{
    float4 position [[Position]];
    float3 linearColor [[Attribute0]];
    float lineDistance [[Attribute1]];
    float fogDepth [[Attribute2]];
    float2 fogRange [[Attribute3]];
    float4 linePixels [[Attribute4]];
    uint renderEntityID [[Attribute5]];
};

/** Defines the single-sample line Scene color and depth target. */
struct Phase1LineSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's output transfer constants. */
float phase1LineLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Draws every line entity through RenderSet indexed-indirect metadata. */
class WebglLinesColorsMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and enables the native line depth contract. */
    constructor(RenderSet<Phase1LinesSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<Phase1LineInvocationResources> invocationResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one segment endpoint in clip space while resolving all entity components. */
    Phase1LineVertexOutput vertex(Phase1LineVertex inputValue [[VertexInput0]],
                                  uint renderEntityID [[RenderEntityID]],
                                  uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const Phase1LineObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const Phase1LineInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const Phase1LineMaterialData materialData = sceneSet->materials->get(renderEntityID, 0u);
        const float4 startPosition = inputValue.segmentStart + float4(instanceData.translation.xyz, 0.0f);
        const float4 endPosition = inputValue.segmentEnd + float4(instanceData.translation.xyz, 0.0f);
        const float4 startClip = mul(objectData.modelViewProjection, startPosition);
        const float4 endClip = mul(objectData.modelViewProjection, endPosition);
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const bool useEnd = inputValue.endpointSideAndDistance.x > 0.5f;
        float4 clipPosition = useEnd ? endClip : startClip;
        clipPosition.xy += invocationResources->invocation->sampleOffsetAndReserved.xy *
                           2.0f / objectData.viewportAndFog.xy * clipPosition.w;

        const float4 viewPosition = mul(objectData.modelView, useEnd ? endPosition : startPosition);
        Phase1LineVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.linearColor = (useEnd ? inputValue.endColor : inputValue.startColor).xyz;
        outputValue.lineDistance = useEnd ? inputValue.endpointSideAndDistance.w : inputValue.endpointSideAndDistance.z;
        outputValue.fogDepth = -viewPosition.z;
        outputValue.fogRange = objectData.viewportAndFog.zw;
        outputValue.renderEntityID = renderEntityID;
        return outputValue;
    }

    /** Applies dashed clipping, linear fog, and the standard r185 output transfer. */
    Phase1LineSceneFrameBuffer fragment(Phase1LineVertexOutput inputValue)
    {
        const Phase1LineMaterialData materialData = sceneSet->materials->get(inputValue.renderEntityID, 0u);
        const bool dashed = materialData.dashAndFlags.z > 0.5f;
        if (dashed &&
            fmod(
                inputValue.lineDistance +
                    materialData.dashAndFlags.w,
                materialData.dashAndFlags.x +
                    materialData.dashAndFlags.y) >
                materialData.dashAndFlags.x)
        {
            discard_fragment();
        }
        const float3 linearColor = inputValue.linearColor * materialData.baseColor.xyz;
        const float fogFactor = smoothstep(inputValue.fogRange.x, inputValue.fogRange.y, inputValue.fogDepth);
        const float3 displayColor = float3(phase1LineLinearToSrgb(linearColor.x),
                                           phase1LineLinearToSrgb(linearColor.y),
                                           phase1LineLinearToSrgb(linearColor.z));
        const float3 displayFogColor = float3(phase1LineLinearToSrgb(materialData.fogColor.x),
                                              phase1LineLinearToSrgb(materialData.fogColor.y),
                                              phase1LineLinearToSrgb(materialData.fogColor.z));
        Phase1LineSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(lerp(displayColor, displayFogColor, fogFactor)), half(1.0f));
        return frameBuffer;
    }
};

/** Tests one interpolated pixel against the exact one-pixel line diamond. */
bool phase1LinesCoversDiamond(float2 pixelCenter, float2 segmentStart, float2 segmentEnd)
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

/** Draws the dashed-line example through its dedicated RenderSet indexed-indirect pass. */
class WebglLinesDashedMainPass final : public IRenderClass
{
public:
    /** Binds the dashed Scene's unique RenderSet and centered raster invocation data. */
    constructor(RenderSet<Phase1LinesSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<Phase1LineInvocationResources> invocationResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the dashed entity components and emits one native line-list endpoint. */
    Phase1LineVertexOutput vertex(Phase1LineVertex inputValue [[VertexInput0]],
                                  uint renderEntityID [[RenderEntityID]],
                                  uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const Phase1LineObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const Phase1LineInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 startPosition = inputValue.segmentStart + float4(instanceData.translation.xyz, 0.0f);
        const float4 endPosition = inputValue.segmentEnd + float4(instanceData.translation.xyz, 0.0f);
        const float4 startClip = mul(objectData.modelViewProjection, startPosition);
        const float4 endClip = mul(objectData.modelViewProjection, endPosition);
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 directionPixels =
            (endNdc - startNdc) * objectData.viewportAndFog.xy;
        const float2 lineNormal = float2(-directionPixels.y, directionPixels.x) /
                                  max(length(directionPixels), 0.0001f);
        const bool useEnd = inputValue.endpointSideAndDistance.x > 0.5f;
        float4 clipPosition = useEnd ? endClip : startClip;
        clipPosition.xy += lineNormal * inputValue.endpointSideAndDistance.y /
                           objectData.viewportAndFog.xy * clipPosition.w;
        clipPosition.xy += invocationResources->invocation->sampleOffsetAndReserved.xy *
                           2.0f / objectData.viewportAndFog.xy * clipPosition.w;

        const float4 viewPosition = mul(objectData.modelView, useEnd ? endPosition : startPosition);
        Phase1LineVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.linearColor = (useEnd ? inputValue.endColor : inputValue.startColor).xyz;
        outputValue.lineDistance = useEnd ? inputValue.endpointSideAndDistance.w : inputValue.endpointSideAndDistance.z;
        outputValue.fogDepth = -viewPosition.z;
        outputValue.fogRange = objectData.viewportAndFog.zw;
        const float2 rasterTieBreak = float2(0.0f, 0.0001f);
        outputValue.linePixels = float4(
            float2(startNdc.x, -startNdc.y) * objectData.viewportAndFog.xy +
                objectData.viewportAndFog.xy + rasterTieBreak,
            float2(endNdc.x, -endNdc.y) * objectData.viewportAndFog.xy +
                objectData.viewportAndFog.xy + rasterTieBreak);
        outputValue.renderEntityID = renderEntityID;
        return outputValue;
    }

    /** Applies the dedicated dash phase, fog, and r185 output transfer. */
    Phase1LineSceneFrameBuffer fragment(Phase1LineVertexOutput inputValue)
    {
        const Phase1LineMaterialData materialData = sceneSet->materials->get(inputValue.renderEntityID, 0u);
        if (fmod(
                inputValue.lineDistance + materialData.dashAndFlags.w,
                materialData.dashAndFlags.x + materialData.dashAndFlags.y) >
            materialData.dashAndFlags.x)
        {
            discard_fragment();
        }
        const float3 linearColor = inputValue.linearColor * materialData.baseColor.xyz;
        const float fogFactor = smoothstep(inputValue.fogRange.x, inputValue.fogRange.y, inputValue.fogDepth);
        const float3 displayColor = float3(phase1LineLinearToSrgb(linearColor.x),
                                           phase1LineLinearToSrgb(linearColor.y),
                                           phase1LineLinearToSrgb(linearColor.z));
        const float3 displayFogColor = float3(phase1LineLinearToSrgb(materialData.fogColor.x),
                                              phase1LineLinearToSrgb(materialData.fogColor.y),
                                              phase1LineLinearToSrgb(materialData.fogColor.z));
        Phase1LineSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(lerp(displayColor, displayFogColor, fogFactor)), half(1.0f));
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet and its single-sample readback and present resources. */
class Phase1LinesPointsRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<Phase1LinesSceneRenderSet> sceneSet;
    Buffer<Phase1LineInvocationData, BufferUsage<Uniform, CopyDst>> invocationBuffer0;
    BindGroup<Phase1LineInvocationResources> invocationResources0;
    RenderClass<WebglLinesColorsMainPass> scenePass0;
    RenderClass<WebglLinesDashedMainPass> dashedScenePass0;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    float backgroundChannel = 0.0f;
    uint useDashedPass = 0u;

public:
    /** Creates the Scene RenderSet and one centered invocation binding. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<Phase1LinesSceneRenderSet>();
        invocationBuffer0 = device->createBuffer("Phase1LinesInvocation0", 1u);
        invocationResources0 = device->createBindGroup<Phase1LineInvocationResources>(invocationBuffer0);
        scenePass0 = device->createRenderClass<WebglLinesColorsMainPass>(sceneSet, invocationResources0);
        dashedScenePass0 = device->createRenderClass<WebglLinesDashedMainPass>(sceneSet, invocationResources0);
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("Phase1LinesOutputRGBA8", width, height, 1u);
        sceneDepth = device->createTexture("Phase1LinesSceneDepth32", width, height, 1u);
    }

    /** Selects the exact black or #111111 clear color for the requested line example. */
    void configureCase(uint dashedCase)
    {
        useDashedPass = dashedCase;
        backgroundChannel = dashedCase != 0u ? 0.06666666666666667f : 0.0f;
    }

    /** Updates the Set, draws all entities once, presents, and submits. */
    void render() override
    {
        sceneSet->update();
        Phase1LineInvocationData invocation;
        invocation.sampleOffsetAndReserved = float4(0.0f);
        Phase1LineSceneFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {backgroundChannel, backgroundChannel, backgroundChannel, 1.0f};
        sceneFrameBuffer.depth = sceneDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        if (useDashedPass != 0u)
        {
            graphicsQueue->writeBuffer(BufferRange(invocationBuffer0), &invocation, sizeof(invocation))
                ->renderPass("Phase1LinesDashedScene", sceneFrameBuffer, dashedScenePass0())
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue->writeBuffer(BufferRange(invocationBuffer0), &invocation, sizeof(invocation))
                ->renderPass("Phase1LinesColorsScene", sceneFrameBuffer, scenePass0())
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
                ->submit();
        }
        swapchain->present();
    }

    /** Returns the DSL-owned final texture for host test readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured final readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured final readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the Scene RenderSet and its single-sample output textures. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
