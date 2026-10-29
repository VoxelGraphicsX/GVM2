#pragma once

#include "UGL.h"

using namespace UGL;

/** Stores one authored random triangle vertex or one expanded highlight corner. */
struct WebglInteractiveBuffergeometryVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
    float4 auxiliary [[Attribute3]];
};

/** Stores the camera transform, lighting constants, and fog contract. */
struct WebglInteractiveBuffergeometryObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
    float4 lightAndAmbient;
    float4 fogColorAndRange;
    float4 fogFarAndReserved;
};

/** Stores the immutable per-entity instance record. */
struct WebglInteractiveBuffergeometryInstanceData
{
    float4 reserved;
};

/** Selects the mesh or highlighted-face phase. */
struct WebglInteractiveBuffergeometryMaterialData
{
    float4 colorAndPhase;
};

/** Owns the mesh and dynamic face-outline entities for the one logical Scene. */
struct WebglInteractiveBuffergeometrySceneRenderSet : public IRenderSet
{
    /** Declares the unified geometry and per-entity components required by r185. */
    constructor(
        BufferComponent<WebglInteractiveBuffergeometryVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInteractiveBuffergeometryObjectData> objects,
        BufferComponent<WebglInteractiveBuffergeometryInstanceData> instances,
        BufferComponent<WebglInteractiveBuffergeometryMaterialData> materials)
    {
    }
};

/** Carries the transformed attributes and RenderEntity identity to fragment shading. */
struct WebglInteractiveBuffergeometryVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float4 color [[Attribute2]];
    float phase [[Attribute3]];
    uint entityID [[Attribute4]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglInteractiveBuffergeometryFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear channel with Three r185's sRGB output transfer. */
float webglInteractiveBuffergeometryLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the frozen Blinn-Phong lobe used by MeshPhongMaterial. */
float3 webglInteractiveBuffergeometrySpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf = saturate(dot(normal, halfDirection));
    const float dotViewHalf = saturate(dot(viewDirection, halfDirection));
    const float fresnel = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 fresnelColor = float3(1.0f) * (1.0f - fresnel) + float3(fresnel);
    const float distribution = 0.3183098861837907f * 126.0f *
        pow(dotNormalHalf, 250.0f);
    return fresnelColor * (0.25f * distribution);
}

/** Transforms mesh and line entities through the single Scene RenderSet. */
WebglInteractiveBuffergeometryVertexOutput webglInteractiveBuffergeometryVertexTransform(
    IN RenderSet<WebglInteractiveBuffergeometrySceneRenderSet> sceneSet,
    WebglInteractiveBuffergeometryVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID,
    bool flipNormal)
{
    const WebglInteractiveBuffergeometryObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglInteractiveBuffergeometryMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const WebglInteractiveBuffergeometryInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position +
        float4(instanceData.reserved.xyz, 0.0f);
    const float3 transformedNormal = mul(
        objectData.modelView,
        float4(inputValue.normal.xyz, 0.0f)).xyz;
    float4 view = mul(objectData.modelView, localPosition);
    float4 clipPosition = mul(objectData.projection, view);
    if (materialData.colorAndPhase.w > 0.5f)
    {
        const float4 endView = mul(
            objectData.modelView,
            inputValue.normal + float4(instanceData.reserved.xyz, 0.0f));
        const float4 endClip = mul(objectData.projection, endView);
        const float2 startNdc = clipPosition.xy / clipPosition.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 directionPixels =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 lineNormal = float2(-directionPixels.y, directionPixels.x) /
            max(length(directionPixels), 0.0001f);
        const bool useEnd = inputValue.auxiliary.x > 0.5f;
        clipPosition = useEnd ? endClip : clipPosition;
        view = useEnd ? endView : view;
        clipPosition.xy += lineNormal * inputValue.auxiliary.y /
            objectData.viewport.xy * clipPosition.w;
    }
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglInteractiveBuffergeometryVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = view.xyz;
    outputValue.viewNormal = flipNormal
        ? -transformedNormal
        : transformedNormal;
    outputValue.color = inputValue.color;
    outputValue.phase = materialData.colorAndPhase.w;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws back-facing mesh triangles through the Scene RenderSet. */
class WebglInteractiveBuffergeometryBackPass final : public IRenderClass
{
public:
    /** Configures the first DoubleSide pass and its normal-flip convention. */
    constructor(RenderSet<WebglInteractiveBuffergeometrySceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
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
    /** Resolves the entity and flips the back-side normal. */
    WebglInteractiveBuffergeometryVertexOutput vertex(
        WebglInteractiveBuffergeometryVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveBuffergeometryVertexTransform(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID, false);
    }

    /** Shades mesh fragments and rejects the line entity. */
    WebglInteractiveBuffergeometryFrameBuffer fragment(
        WebglInteractiveBuffergeometryVertexOutput inputValue)
    {
        if (inputValue.phase > 0.5f)
            discard_fragment();
        const WebglInteractiveBuffergeometryObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 light0 = normalize(float3(1.0f, 1.0f, 1.0f));
        const float3 light1 = float3(0.0f, -1.0f, 0.0f);
        const float irradiance0 = max(dot(normal, light0), 0.0f) * 1.5f;
        const float irradiance1 = max(dot(normal, light1), 0.0f) * 4.5f;
        float3 linearColor = inputValue.color.xyz * 0.4019777798219466f *
            (float3(0.17341629055137808f) + float3(irradiance0) + float3(irradiance1)) *
            0.3183098861837907f;
        linearColor += float3(irradiance0) * webglInteractiveBuffergeometrySpecular(light0, viewDirection, normal);
        linearColor += float3(irradiance1) * webglInteractiveBuffergeometrySpecular(light1, viewDirection, normal);
        const float fogFactor = smoothstep(
            objectData.fogColorAndRange.w,
            objectData.fogFarAndReserved.x,
            -inputValue.viewPosition.z);
        const float3 displayColor = lerp(
            float3(webglInteractiveBuffergeometryLinearToSrgb(linearColor.x),
                   webglInteractiveBuffergeometryLinearToSrgb(linearColor.y),
                   webglInteractiveBuffergeometryLinearToSrgb(linearColor.z)),
            float3(objectData.fogColorAndRange.x,
                   objectData.fogColorAndRange.y,
                   objectData.fogColorAndRange.z),
            fogFactor);
        WebglInteractiveBuffergeometryFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws front-facing mesh triangles through the Scene RenderSet. */
class WebglInteractiveBuffergeometryFrontPass final : public IRenderClass
{
public:
    /** Configures the second DoubleSide pass without normal inversion. */
    constructor(RenderSet<WebglInteractiveBuffergeometrySceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
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
    /** Resolves the entity while preserving its authored normal. */
    WebglInteractiveBuffergeometryVertexOutput vertex(
        WebglInteractiveBuffergeometryVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveBuffergeometryVertexTransform(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID, true);
    }

    /** Shades front-facing mesh fragments with the same frozen lighting. */
    WebglInteractiveBuffergeometryFrameBuffer fragment(
        WebglInteractiveBuffergeometryVertexOutput inputValue)
    {
        if (inputValue.phase > 0.5f)
            discard_fragment();
        const WebglInteractiveBuffergeometryObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 light0 = normalize(float3(1.0f, 1.0f, 1.0f));
        const float3 light1 = float3(0.0f, -1.0f, 0.0f);
        const float irradiance0 = max(dot(normal, light0), 0.0f) * 1.5f;
        const float irradiance1 = max(dot(normal, light1), 0.0f) * 4.5f;
        float3 linearColor = inputValue.color.xyz * 0.4019777798219466f *
            (float3(0.17341629055137808f) + float3(irradiance0) + float3(irradiance1)) *
            0.3183098861837907f;
        linearColor += float3(irradiance0) * webglInteractiveBuffergeometrySpecular(light0, viewDirection, normal);
        linearColor += float3(irradiance1) * webglInteractiveBuffergeometrySpecular(light1, viewDirection, normal);
        const float fogFactor = smoothstep(
            objectData.fogColorAndRange.w,
            objectData.fogFarAndReserved.x,
            -inputValue.viewPosition.z);
        const float3 displayColor = lerp(
            float3(webglInteractiveBuffergeometryLinearToSrgb(linearColor.x),
                   webglInteractiveBuffergeometryLinearToSrgb(linearColor.y),
                   webglInteractiveBuffergeometryLinearToSrgb(linearColor.z)),
            float3(objectData.fogColorAndRange.x,
                   objectData.fogColorAndRange.y,
                   objectData.fogColorAndRange.z),
            fogFactor);
        WebglInteractiveBuffergeometryFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the CPU-selected four-point face outline through the same Set. */
class WebglInteractiveBuffergeometryLinePass final : public IRenderClass
{
public:
    /** Configures transparent line blending and depth-tested triangle expansion. */
    constructor(RenderSet<WebglInteractiveBuffergeometrySceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::LessEqual);
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
    /** Transforms the four outline points and preserves the line phase. */
    WebglInteractiveBuffergeometryVertexOutput vertex(
        WebglInteractiveBuffergeometryVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveBuffergeometryVertexTransform(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID, false);
    }

    /** Emits an opaque white line only when the CPU hit entity is enabled. */
    WebglInteractiveBuffergeometryFrameBuffer fragment(
        WebglInteractiveBuffergeometryVertexOutput inputValue)
    {
        if (inputValue.phase < 1.5f)
            discard_fragment();
        const WebglInteractiveBuffergeometryObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const float fogFactor = smoothstep(
            objectData.fogColorAndRange.w,
            objectData.fogFarAndReserved.x,
            -inputValue.viewPosition.z);
        const float3 color = lerp(float3(1.0f),
            float3(objectData.fogColorAndRange.x,
                   objectData.fogColorAndRange.y,
                   objectData.fogColorAndRange.z),
            fogFactor);
        WebglInteractiveBuffergeometryFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one RenderSet and the three scene passes for the example. */
class WebglInteractiveBuffergeometryRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractiveBuffergeometrySceneRenderSet> sceneSet;
    RenderClass<WebglInteractiveBuffergeometryBackPass> backPass;
    RenderClass<WebglInteractiveBuffergeometryFrontPass> frontPass;
    RenderClass<WebglInteractiveBuffergeometryLinePass> linePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene RenderSet and all DSL scene passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractiveBuffergeometrySceneRenderSet>();
        backPass = device->createRenderClass<WebglInteractiveBuffergeometryBackPass>(sceneSet);
        frontPass = device->createRenderClass<WebglInteractiveBuffergeometryFrontPass>(sceneSet);
        linePass = device->createRenderClass<WebglInteractiveBuffergeometryLinePass>(sceneSet);
    }

    /** Allocates the fixed single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglInteractiveBuffergeometryOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglInteractiveBuffergeometryDepth", width, height, 1u);
    }

    /** Executes the back, front, and highlight passes with RenderSet-only draws. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglInteractiveBuffergeometryFrameBuffer backFrame;
        backFrame.color = outputColor->createView();
        backFrame.color.loadOp = LoadOp::Clear;
        backFrame.color.storeOp = StoreOp::Store;
        backFrame.color.clearValue = {0.0196078, 0.0196078, 0.0196078, 1.0};
        backFrame.depth = outputDepth->createView();
        backFrame.depth.depthLoadOp = LoadOp::Clear;
        backFrame.depth.depthStoreOp = StoreOp::Store;
        backFrame.depth.depthClearValue = 1.0f;
        WebglInteractiveBuffergeometryFrameBuffer frontFrame;
        frontFrame.color = outputColor->createView();
        frontFrame.color.loadOp = LoadOp::Load;
        frontFrame.color.storeOp = StoreOp::Store;
        frontFrame.depth = outputDepth->createView();
        frontFrame.depth.depthLoadOp = LoadOp::Load;
        frontFrame.depth.depthStoreOp = StoreOp::Store;
        WebglInteractiveBuffergeometryFrameBuffer lineFrame;
        lineFrame.color = outputColor->createView();
        lineFrame.color.loadOp = LoadOp::Load;
        lineFrame.color.storeOp = StoreOp::Store;
        lineFrame.depth = outputDepth->createView();
        lineFrame.depth.depthLoadOp = LoadOp::Load;
        lineFrame.depth.depthStoreOp = StoreOp::Store;
        graphicsQueue
            ->renderPass("WebglInteractiveBuffergeometryBack", backFrame, backPass())
            ->renderPass("WebglInteractiveBuffergeometryFront", frontFrame, frontPass())
            ->renderPass("WebglInteractiveBuffergeometryLine", lineFrame, linePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL output texture used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and output textures. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};
