#ifndef GVM_THREE_WEBGL_LOD_HPP
#define GVM_THREE_WEBGL_LOD_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one endpoint of a native single-sample LOD wire segment. */
struct WebglLodVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 startNormal [[Attribute2]];
    float4 endNormal [[Attribute3]];
    float4 endpointAndSide [[Attribute4]];
};

/** Stores one LOD object's transform and directional/point lighting state. */
struct WebglLodObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewport;
    float4 directionalLightDirectionAndIntensity;
    float4 pointLightViewPositionAndIntensity;
    float4 fogNearFar;
};

/** Stores the mandatory one-entry instance component. */
struct WebglLodInstanceData
{
    float4 reserved;
};

/** Stores one wireframe-compatible LOD material color. */
struct WebglLodMaterialData
{
    float4 baseColor;
};

/** Defines the sole Scene RenderSet for all LOD entities. */
struct WebglLodSceneRenderSet : public IRenderSet
{
    /** Declares consolidated LOD geometry and per-entity component storage. */
    constructor(
        BufferComponent<WebglLodVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLodObjectData> objects,
        BufferComponent<WebglLodInstanceData> instances,
        BufferComponent<WebglLodMaterialData> materials,
        BufferComponent<uint4> lodState)
    {
    }
};

/** Carries a transformed line endpoint, interpolated normal, and entity identity. */
struct WebglLodVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    uint entityID [[Attribute2]];
    float visible [[Attribute3]];
    float4 lineRaster [[Attribute4]];
};

/** Defines ordinary single-sample color/depth attachments. */
struct WebglLodFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts linear material light to the canvas transfer. */
float webglLodLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms one LOD vertex through the unique RenderSet entity. */
WebglLodVertexOutput webglLodTransformVertex(
    IN RenderSet<WebglLodSceneRenderSet> sceneSet,
    WebglLodVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLodObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglLodInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebglLodMaterialData materialData = sceneSet->materials->get(renderEntityID, 0u);
    const uint4 lodState = sceneSet->lodState->get(renderEntityID, 0u);
    const float4 startView = mul(objectData.modelView, inputValue.segmentStart);
    const float4 endView = mul(objectData.modelView, inputValue.segmentEnd);
    const float4 startClip = mul(objectData.modelViewProjection, inputValue.segmentStart);
    const float4 endClip = mul(objectData.modelViewProjection, inputValue.segmentEnd);
    const bool useEnd = inputValue.endpointAndSide.x > 0.5f;
    const float2 startNdc = startClip.xy / startClip.w;
    const float2 endNdc = endClip.xy / endClip.w;
    const float2 direction = (endNdc - startNdc) * objectData.viewport.xy;
    const float2 tangent = direction / max(length(direction), 0.0001f);
    const float2 normal = float2(-direction.y, direction.x) /
        max(length(direction), 0.0001f);
    float4 clipPosition = useEnd ? endClip : startClip;
    const float endpointExtension = useEnd ? 0.75f : -0.75f;
    clipPosition.xy +=
        (normal * inputValue.endpointAndSide.y + tangent * endpointExtension) /
        objectData.viewport.xy * clipPosition.w;
    const float4 selectedView = useEnd ? endView : startView;
    const float3 selectedNormal = useEnd ? inputValue.endNormal.xyz : inputValue.startNormal.xyz;
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    const float visible = lodState.y == 0u ? 0.0f : 1.0f;
    if (visible < 0.5f)
    {
        clipPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    WebglLodVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = selectedView.xyz;
    outputValue.viewNormal = normalize(float3(mul(objectData.modelView,
        float4(selectedNormal, 0.0f)).xyz));
    outputValue.entityID = renderEntityID;
    outputValue.visible = visible;
    outputValue.lineRaster = float4(
        (startNdc.x + 1.0f) * objectData.viewport.x,
        (1.0f - startNdc.y) * objectData.viewport.y,
        (endNdc.x + 1.0f) * objectData.viewport.x,
        (1.0f - endNdc.y) * objectData.viewport.y);
    return outputValue;
}

/** Evaluates a simple Lambert material for the selected LOD geometry. */
WebglLodFrameBuffer webglLodShade(
    IN RenderSet<WebglLodSceneRenderSet> sceneSet,
    WebglLodVertexOutput inputValue)
{
    const WebglLodObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglLodMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 normal = normalize(inputValue.viewNormal);
    const float3 directionalInput = objectData.directionalLightDirectionAndIntensity.xyz;
    const float3 directional = normalize(directionalInput);
    const float directionalDiffuse = max(dot(normal, directional), 0.0f) *
        objectData.directionalLightDirectionAndIntensity.w;
    const float3 pointVector = objectData.pointLightViewPositionAndIntensity.xyz -
        inputValue.viewPosition;
    const float pointDiffuse = max(dot(normal, normalize(pointVector)), 0.0f) *
        objectData.pointLightViewPositionAndIntensity.w;
    const float3 pointColor = float3(1.0f, 0.015208514f, 0.0f);
    const float3 linearColor = materialData.baseColor.xyz *
        (directionalDiffuse * float3(1.0f) + pointDiffuse * pointColor) *
        0.3183098861837907f;
    const float distanceToCamera = max(-inputValue.viewPosition.z, 0.0f);
    const float fogFactor = saturate((distanceToCamera - objectData.fogNearFar.x) /
        max(objectData.fogNearFar.y - objectData.fogNearFar.x, 0.0001f));
    const float3 foggedColor = lerp(linearColor, float3(0.0f), fogFactor);
    WebglLodFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webglLodLinearToSrgb(foggedColor.x),
        webglLodLinearToSrgb(foggedColor.y),
        webglLodLinearToSrgb(foggedColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws all selected LOD levels through one indexed-indirect Scene pass. */
class WebglLodMainPass final : public IRenderClass
{
public:
    /** Configures opaque depth-tested LOD rendering. */
    constructor(RenderSet<WebglLodSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance IDs through the RenderSet draw. */
    WebglLodVertexOutput vertex(
        WebglLodVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLodTransformVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes lit LOD color. */
    WebglLodFrameBuffer fragment(WebglLodVertexOutput inputValue)
    {
        if (inputValue.visible < 0.5f) discard_fragment();
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
        clip(diamondExit - diamondEnter);
        clip(0.999999f - diamondExit);
        const WebglLodFrameBuffer shaded = webglLodShade(sceneSet, inputValue);
        WebglLodFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns the unique LOD RenderSet and ordinary single-sample output. */
class WebglLodRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLodSceneRenderSet> sceneSet;
    RenderClass<WebglLodMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the LOD RenderSet and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLodSceneRenderSet>();
        scenePass = device->createRenderClass<WebglLodMainPass>(sceneSet);
    }

    /** Allocates the explicit single-sample capture attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLodColor", width, height, 1u);
        outputDepth = device->createTexture("WebglLodDepth", width, height, 1u);
    }

    /** Submits one RenderSet indexed-indirect LOD pass. */
    void render() override
    {
        sceneSet->update();
        WebglLodFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLodScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target for readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the LOD RenderSet and explicit attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglLodRenderer
#undef WebglLodFrameBuffer
#undef WebglLodMainPass
#undef WebglLodSceneRenderSet

#endif
