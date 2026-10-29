#ifndef GVM_THREE_MISC_CONTROLS_ORBIT_HPP
#define GVM_THREE_MISC_CONTROLS_ORBIT_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one triangle-list vertex from the canonical four-sided cone. */
struct MiscControlsOrbitVertex
{
    float4 position [[Attribute0]];
};

/** Stores the camera, view transform, light directions, and fog density. */
struct MiscControlsOrbitObjectData
{
    float4x4 viewProjection;
    float4x4 view;
    float4 whiteLightDirection;
    float4 blueLightDirection;
    float4 fogAndReserved;
};

/** Stores one of the 500 deterministic cone translations. */
struct MiscControlsOrbitInstanceData
{
    float4 translation;
};

/** Stores the Phong diffuse, specular, ambient, and shininess values. */
struct MiscControlsOrbitMaterialData
{
    float4 diffuseAndShininess;
    float4 specularAndReserved;
    float4 ambientAndReserved;
};

/** Defines the unique one-entity RenderSet used by the orbit-control Scene. */
struct MiscControlsOrbitSceneRenderSet : public IRenderSet
{
    /** Declares the exact five-component Manifest schema. */
    constructor(
        BufferComponent<MiscControlsOrbitVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<MiscControlsOrbitObjectData> objects,
        BufferComponent<MiscControlsOrbitInstanceData> instances,
        BufferComponent<MiscControlsOrbitMaterialData> materials)
    {
    }
};

/** Carries view-space geometry and the entity identity to flat Phong shading. */
struct MiscControlsOrbitVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the fixed single-sample RGBA8 and depth output attachments. */
struct MiscControlsOrbitFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel to the Three canvas sRGB transfer. */
float miscControlsOrbitLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the 500 instanced cones through the Scene's only RenderSet. */
class MiscControlsOrbitSceneMainPass final : public IRenderClass
{
public:
    /** Configures Three's opaque front-face triangle and depth semantics. */
    constructor(RenderSet<MiscControlsOrbitSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the per-instance translation selected by RenderEntityInstanceID. */
    MiscControlsOrbitVertexOutput vertex(
        MiscControlsOrbitVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const MiscControlsOrbitObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const MiscControlsOrbitInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 worldPosition = inputValue.position +
            float4(instanceData.translation.xyz, 0.0f);
        const float4 viewPosition = mul(objectData.view, worldPosition);
        MiscControlsOrbitVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates flat MeshPhong lighting, exponential fog, and sRGB output. */
    MiscControlsOrbitFrameBuffer fragment(MiscControlsOrbitVertexOutput inputValue)
    {
        const MiscControlsOrbitObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const MiscControlsOrbitMaterialData materialData =
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
            miscControlsOrbitLinearToSrgb(linearColor.x),
            miscControlsOrbitLinearToSrgb(linearColor.y),
            miscControlsOrbitLinearToSrgb(linearColor.z));
        const float fogDepth = -inputValue.viewPosition.z;
        const float density = objectData.fogAndReserved.x;
        const float fogFactor = 1.0f - exp(
            -density * density * fogDepth * fogDepth);
        outputColor = lerp(outputColor, float3(0.8f), fogFactor);
        MiscControlsOrbitFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(outputColor.x),
            half(outputColor.y),
            half(outputColor.z),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and deterministic single-sample output. */
class MiscControlsOrbitRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<MiscControlsOrbitSceneRenderSet> sceneSet;
    RenderClass<MiscControlsOrbitSceneMainPass> scenePass;
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
        sceneSet = device->createRenderSet<MiscControlsOrbitSceneRenderSet>();
        scenePass = device->createRenderClass<MiscControlsOrbitSceneMainPass>(sceneSet);
    }

    /** Allocates the fixed RGBA8 and depth attachments requested by the Host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("MiscControlsOrbitRGBA8", width, height, 1u);
        depthTexture = device->createTexture("MiscControlsOrbitDepth32", width, height, 1u);
    }

    /** Updates entity metadata and emits one RenderSet indexed-indirect draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        MiscControlsOrbitFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.8f, 0.8f, 0.8f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("MiscControlsOrbitScene", frameBuffer, scenePass())
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
