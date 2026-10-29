#ifndef GVM_THREE_WEBGL_HELPERS_HPP
#define GVM_THREE_WEBGL_HELPERS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores either an ordinary mesh vertex or a CPU-expanded clip-space line vertex. */
struct WebglHelpersVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
    float4 lineEndpoints [[Attribute3]];
};

/** Stores the mesh transform and the animated point-light position. */
struct WebglHelpersObjectData
{
    float4x4 modelViewProjection;
    float4x4 model;
    float4 lightPosition;
};

/** Stores the mandatory one-entry non-instanced component. */
struct WebglHelpersInstanceData
{
    float4 reserved;
};

/** Stores the loaded head material parameters. */
struct WebglHelpersMaterialData
{
    float4 baseColorAndRoughness;
};

/** Selects mesh or helper shading and the opaque or transparent phase. */
struct WebglHelpersHelperData
{
    uint4 kindPhaseAndDepth;
    float4 colorOpacity;
};

/** Defines the unique RenderSet owned by the helper example Scene. */
struct WebglHelpersSceneRenderSet : public IRenderSet
{
    /** Declares the manifest-locked six-component Scene ABI. */
    constructor(
        BufferComponent<WebglHelpersVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglHelpersObjectData> objects,
        BufferComponent<WebglHelpersInstanceData> instances,
        BufferComponent<WebglHelpersMaterialData> materials,
        BufferComponent<WebglHelpersHelperData> helperData)
    {
    }
};

/** Carries mesh and helper interpolants into both Scene phases. */
struct WebglHelpersVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float3 vertexColor [[Attribute2]];
    uint2 entityAndPhase [[Attribute3]];
    float4 lineEndpoints [[Attribute4]];
};

/** Defines the ordinary single-sample helper framebuffer. */
struct WebglHelpersFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear helper or material channel to display sRGB. */
float webglHelpersLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms one mesh vertex or forwards one CPU-expanded clip-space line vertex. */
WebglHelpersVertexOutput webglHelpersTransform(
    IN RenderSet<WebglHelpersSceneRenderSet> sceneSet,
    WebglHelpersVertex inputValue,
    uint entityID,
    uint instanceID)
{
    const WebglHelpersObjectData objectData =
        sceneSet->objects->get(entityID, 0u);
    const WebglHelpersHelperData helperData =
        sceneSet->helperData->get(entityID, 0u);
    const WebglHelpersInstanceData instanceData =
        sceneSet->instances->get(entityID, instanceID);
    WebglHelpersVertexOutput outputValue;
    if (helperData.kindPhaseAndDepth.x == 0u)
    {
        const float4 localPosition = float4(
            inputValue.position.xyz + instanceData.reserved.xyz, 1.0f);
        const float4 worldPosition = mul(objectData.model, localPosition);
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = normalize(float3(
            mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
    }
    else
    {
        outputValue.position = inputValue.position;
        outputValue.worldPosition = float3(0.0f);
        outputValue.worldNormal = float3(0.0f, 0.0f, 1.0f);
    }
    outputValue.vertexColor = inputValue.color.xyz;
    outputValue.entityAndPhase = uint2(entityID, helperData.kindPhaseAndDepth.y);
    outputValue.lineEndpoints = inputValue.lineEndpoints;
    return outputValue;
}

/** Tests one fragment against WebGL's one-pixel diamond-exit line rule. */
bool webglHelpersLineCoversPixel(float2 pixelPosition, float4 endpoints)
{
    const float diamondRadius = 0.5002f;
    const float2 first = endpoints.xy + float2(0.0001f, 0.000001f);
    const float2 second = endpoints.zw + float2(0.0001f, 0.000001f);
    const float2 firstDiamond = float2(first.x + first.y, first.x - first.y);
    const float2 secondDiamond =
        float2(second.x + second.y, second.x - second.y);
    const float2 centerDiamond = float2(
        pixelPosition.x + pixelPosition.y,
        pixelPosition.x - pixelPosition.y);
    const float2 delta = secondDiamond - firstDiamond;
    float minimumTime = 0.0f;
    float maximumTime = 1.0f;
    if (abs(delta.x) < 0.000001f)
    {
        if (firstDiamond.x < centerDiamond.x - diamondRadius ||
            firstDiamond.x > centerDiamond.x + diamondRadius) return false;
    }
    else
    {
        const float inverseDelta = 1.0f / delta.x;
        const float time0 =
            (centerDiamond.x - diamondRadius - firstDiamond.x) * inverseDelta;
        const float time1 =
            (centerDiamond.x + diamondRadius - firstDiamond.x) * inverseDelta;
        minimumTime = max(minimumTime, min(time0, time1));
        maximumTime = min(maximumTime, max(time0, time1));
    }
    if (abs(delta.y) < 0.000001f)
    {
        if (firstDiamond.y < centerDiamond.y - diamondRadius ||
            firstDiamond.y > centerDiamond.y + diamondRadius) return false;
    }
    else
    {
        const float inverseDelta = 1.0f / delta.y;
        const float time0 =
            (centerDiamond.y - diamondRadius - firstDiamond.y) * inverseDelta;
        const float time1 =
            (centerDiamond.y + diamondRadius - firstDiamond.y) * inverseDelta;
        minimumTime = max(minimumTime, min(time0, time1));
        maximumTime = min(maximumTime, max(time0, time1));
    }
    return minimumTime <= maximumTime && minimumTime < 0.999999f;
}

/** Evaluates the loaded head or unlit helper output in linear space. */
float4 webglHelpersShade(
    IN RenderSet<WebglHelpersSceneRenderSet> sceneSet,
    WebglHelpersVertexOutput inputValue)
{
    const WebglHelpersHelperData helperData =
        sceneSet->helperData->get(inputValue.entityAndPhase.x, 0u);
    float3 color = inputValue.vertexColor * helperData.colorOpacity.xyz;
    if (helperData.kindPhaseAndDepth.x == 0u)
    {
        const WebglHelpersObjectData objectData =
            sceneSet->objects->get(inputValue.entityAndPhase.x, 0u);
        const WebglHelpersMaterialData materialData =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const float3 lightDirection = normalize(float3(
            objectData.lightPosition.xyz - inputValue.worldPosition));
        const float diffuse = max(dot(inputValue.worldNormal, lightDirection), 0.0f);
        const float inverseSquareAttenuation =
            1.0f / max(dot(objectData.lightPosition.xyz - inputValue.worldPosition,
                           objectData.lightPosition.xyz - inputValue.worldPosition),
                       1.0f);
        color = materialData.baseColorAndRoughness.xyz *
            diffuse * inverseSquareAttenuation * 3.14159265f;
    }
    color = float3(
        webglHelpersLinearToSrgb(color.x),
        webglHelpersLinearToSrgb(color.y),
        webglHelpersLinearToSrgb(color.z));
    return float4(color, helperData.colorOpacity.w);
}

/** Draws the head and all depth-tested opaque helper geometry. */
class WebglHelpersOpaquePass final : public IRenderClass
{
public:
    /** Configures the opaque Scene phase without MSAA. */
    constructor(RenderSet<WebglHelpersSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves one opaque Scene vertex through the unique RenderSet. */
    WebglHelpersVertexOutput vertex(
        WebglHelpersVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        return webglHelpersTransform(sceneSet, inputValue, entityID, instanceID);
    }

    /** Rejects transparent entities and shades mesh or helper data. */
    WebglHelpersFrameBuffer fragment(WebglHelpersVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 0u) discard_fragment();
        const WebglHelpersHelperData helperData =
            sceneSet->helperData->get(inputValue.entityAndPhase.x, 0u);
        if (helperData.kindPhaseAndDepth.x == 1u &&
            !webglHelpersLineCoversPixel(
                inputValue.position.xy, inputValue.lineEndpoints))
            discard_fragment();
        WebglHelpersFrameBuffer outputValue;
        outputValue.color = half4(webglHelpersShade(sceneSet, inputValue));
        return outputValue;
    }
};

/** Draws the depth-disabled transparent wireframe and edge helpers. */
class WebglHelpersTransparentPass final : public IRenderClass
{
public:
    /** Configures the transparent overlay phase without MSAA. */
    constructor(RenderSet<WebglHelpersSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setCullMode(CullMode::None);
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
    /** Resolves one transparent helper vertex through the unique RenderSet. */
    WebglHelpersVertexOutput vertex(
        WebglHelpersVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        return webglHelpersTransform(sceneSet, inputValue, entityID, instanceID);
    }

    /** Rejects opaque entities and blends one unlit helper fragment. */
    WebglHelpersFrameBuffer fragment(WebglHelpersVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 1u) discard_fragment();
        const WebglHelpersHelperData helperData =
            sceneSet->helperData->get(inputValue.entityAndPhase.x, 0u);
        if (helperData.kindPhaseAndDepth.x != 1u) discard_fragment();
        if (!webglHelpersLineCoversPixel(
                inputValue.position.xy, inputValue.lineEndpoints))
            discard_fragment();
        WebglHelpersFrameBuffer outputValue;
        outputValue.color = half4(webglHelpersShade(sceneSet, inputValue));
        return outputValue;
    }
};

/** Owns the unique helper Scene Set and its two material phases. */
class WebglHelpersRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglHelpersSceneRenderSet> sceneSet;
    RenderClass<WebglHelpersOpaquePass> opaquePass;
    RenderClass<WebglHelpersTransparentPass> transparentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both passes and the Scene's only RenderSet. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglHelpersSceneRenderSet>();
        opaquePass = device->createRenderClass<WebglHelpersOpaquePass>(sceneSet);
        transparentPass =
            device->createRenderClass<WebglHelpersTransparentPass>(sceneSet);
    }

    /** Allocates fixed single-sample color and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglHelpersColor", width, height, 1u);
        outputDepth = device->createTexture("WebglHelpersDepth", width, height, 1u);
    }

    /** Renders opaque then transparent geometry from the same Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglHelpersFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglHelpersFrameBuffer transparentFrame;
        transparentFrame.color = outputColor->createView();
        transparentFrame.color.loadOp = LoadOp::Load;
        transparentFrame.color.storeOp = StoreOp::Store;
        transparentFrame.depth = outputDepth->createView();
        transparentFrame.depth.depthLoadOp = LoadOp::Load;
        transparentFrame.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglHelpersOpaque", opaqueFrame, opaquePass())
            ->renderPass("WebglHelpersTransparent", transparentFrame,
                         transparentPass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Exposes the deterministic RGBA8 attachment to the host. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and private single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
