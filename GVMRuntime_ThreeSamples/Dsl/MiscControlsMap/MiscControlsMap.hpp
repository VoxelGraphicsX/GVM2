#ifndef GVM_THREE_MISC_CONTROLS_MAP_HPP
#define GVM_THREE_MISC_CONTROLS_MAP_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one triangle-list vertex from the translated unit BoxGeometry. */
struct MiscControlsMapVertex
{
    float4 position [[Attribute0]];
};

/** Stores the camera, view transform, light directions, and fog density. */
struct MiscControlsMapObjectData
{
    float4x4 viewProjection;
    float4x4 view;
    float4 whiteLightDirection;
    float4 blueLightDirection;
    float4 fogAndReserved;
};

/** Stores one of the 500 deterministic box translations and heights. */
struct MiscControlsMapInstanceData
{
    float4 translationAndHeight;
};

/** Stores the Phong diffuse, specular, ambient, and shininess values. */
struct MiscControlsMapMaterialData
{
    float4 diffuseAndShininess;
    float4 specularAndReserved;
    float4 ambientAndReserved;
};

/** Defines the unique one-entity RenderSet used by the map-control Scene. */
struct MiscControlsMapSceneRenderSet : public IRenderSet
{
    /** Declares the exact five-component Manifest schema. */
    constructor(
        BufferComponent<MiscControlsMapVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<MiscControlsMapObjectData> objects,
        BufferComponent<MiscControlsMapInstanceData> instances,
        BufferComponent<MiscControlsMapMaterialData> materials)
    {
    }
};

/** Carries view-space geometry and the entity identity to flat Phong shading. */
struct MiscControlsMapVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the fixed single-sample RGBA8 and depth output attachments. */
struct MiscControlsMapFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel to the Three canvas sRGB transfer. */
float miscControlsMapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the 500 instanced city boxes through the Scene's only RenderSet. */
class MiscControlsMapSceneMainPass final : public IRenderClass
{
public:
    /** Configures Three's opaque front-face triangle and depth semantics. */
    constructor(RenderSet<MiscControlsMapSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the per-instance nonuniform scale and translation. */
    MiscControlsMapVertexOutput vertex(
        MiscControlsMapVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const MiscControlsMapObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const MiscControlsMapInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 worldPosition = float4(
            inputValue.position.x * 20.0f + instanceData.translationAndHeight.x,
            inputValue.position.y * instanceData.translationAndHeight.w,
            inputValue.position.z * 20.0f + instanceData.translationAndHeight.z,
            1.0f);
        const float4 viewPosition = mul(objectData.view, worldPosition);
        MiscControlsMapVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates flat MeshPhong lighting, exponential fog, and sRGB output. */
    MiscControlsMapFrameBuffer fragment(MiscControlsMapVertexOutput inputValue)
    {
        const MiscControlsMapObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const MiscControlsMapMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normal = -normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 whiteDirection = normalize(float3(
            objectData.whiteLightDirection.xyz));
        const float3 blueDirection = normalize(float3(
            objectData.blueLightDirection.xyz));
        const float whiteDot = saturate(dot(normal, whiteDirection));
        const float blueDot = saturate(dot(normal, blueDirection));
        const float reciprocalPi = 0.3183098861837907f;
        float3 linearColor = materialData.diffuseAndShininess.xyz * reciprocalPi *
            (materialData.ambientAndReserved.xyz +
                float3(3.0f * whiteDot) +
                float3(0.0f, 0.04798888f, 0.73860398f) * blueDot);
        const float3 specularColor = materialData.specularAndReserved.xyz;
        const float shininess = materialData.diffuseAndShininess.w;
        const float3 whiteHalf = normalize(whiteDirection + viewDirection);
        const float3 blueHalf = normalize(blueDirection + viewDirection);
        const float whiteDotViewHalf = saturate(dot(viewDirection, whiteHalf));
        const float blueDotViewHalf = saturate(dot(viewDirection, blueHalf));
        const float whiteFresnel = exp2(
            (-5.55473f * whiteDotViewHalf - 6.98316f) * whiteDotViewHalf);
        const float blueFresnel = exp2(
            (-5.55473f * blueDotViewHalf - 6.98316f) * blueDotViewHalf);
        const float3 whiteSpecular =
            specularColor * (1.0f - whiteFresnel) + float3(whiteFresnel);
        const float3 blueSpecular =
            specularColor * (1.0f - blueFresnel) + float3(blueFresnel);
        const float distributionScale = reciprocalPi *
            (shininess * 0.5f + 1.0f) * 0.25f;
        linearColor += 3.0f * whiteDot * whiteSpecular * distributionScale *
            pow(saturate(dot(normal, whiteHalf)), shininess);
        linearColor += float3(0.0f, 0.04798888f, 0.73860398f) * blueDot *
            blueSpecular * distributionScale *
            pow(saturate(dot(normal, blueHalf)), shininess);
        float3 outputColor = float3(
            miscControlsMapLinearToSrgb(linearColor.x),
            miscControlsMapLinearToSrgb(linearColor.y),
            miscControlsMapLinearToSrgb(linearColor.z));
        const float fogDepth = -inputValue.viewPosition.z;
        const float density = objectData.fogAndReserved.x;
        const float fogFactor = 1.0f - exp(
            -density * density * fogDepth * fogDepth);
        outputColor = lerp(outputColor, float3(0.8f), fogFactor);
        MiscControlsMapFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(outputColor.x),
            half(outputColor.y),
            half(outputColor.z),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and deterministic single-sample output. */
class MiscControlsMapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<MiscControlsMapSceneRenderSet> sceneSet;
    RenderClass<MiscControlsMapSceneMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
        TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
        TextureUsage<RenderAttachment>,
        TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the sole RenderSet and its only Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<MiscControlsMapSceneRenderSet>();
        scenePass = device->createRenderClass<MiscControlsMapSceneMainPass>(sceneSet);
    }

    /** Allocates the fixed RGBA8 and depth attachments requested by the Host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("MiscControlsMapRGBA8", width, height, 1u);
        depthTexture = device->createTexture("MiscControlsMapDepth32", width, height, 1u);
    }

    /** Updates entity metadata and emits one RenderSet indexed-indirect draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        MiscControlsMapFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.8f, 0.8f, 0.8f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("MiscControlsMapScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 target used by strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
        TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the Scene Set and both deterministic attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
