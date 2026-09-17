#ifndef GVM_THREE_WEBGL_INTERACTIVE_CUBES_ORTHO_HPP
#define GVM_THREE_WEBGL_INTERACTIVE_CUBES_ORTHO_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one BoxGeometry vertex and its face normal. */
struct WebglInteractiveCubesOrthoVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores one entity's orthographic camera transform and light. */
struct WebglInteractiveCubesOrthoObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 lightDirectionAndIntensity;
};

/** Stores the mandatory one-entry RenderSet instance component. */
struct WebglInteractiveCubesOrthoInstanceData
{
    float4 reserved;
};

/** Stores one deterministic Lambert color and pointer-hit emissive state. */
struct WebglInteractiveCubesOrthoMaterialData
{
    float4 baseColor;
    float4 emissive;
};

/** Defines the sole Scene RenderSet for all 2,000 orthographic cubes. */
struct WebglInteractiveCubesOrthoSceneRenderSet : public IRenderSet
{
    /** Declares consolidated BoxGeometry and per-entity component storage. */
    constructor(
        BufferComponent<WebglInteractiveCubesOrthoVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInteractiveCubesOrthoObjectData> objects,
        BufferComponent<WebglInteractiveCubesOrthoInstanceData> instances,
        BufferComponent<WebglInteractiveCubesOrthoMaterialData> materials)
    {
    }
};

/** Carries transformed position, normal, and RenderSet entity identity. */
struct WebglInteractiveCubesOrthoVertexOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the ordinary single-sample color/depth target. */
struct WebglInteractiveCubesOrthoFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear channel to the Three canvas transfer function. */
float webglInteractiveCubesOrthoLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms one cube vertex using per-entity orthographic matrices. */
WebglInteractiveCubesOrthoVertexOutput webglInteractiveCubesOrthoTransformVertex(
    IN RenderSet<WebglInteractiveCubesOrthoSceneRenderSet> sceneSet,
    WebglInteractiveCubesOrthoVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglInteractiveCubesOrthoObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglInteractiveCubesOrthoInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
    const float4 worldNormal4 = mul(objectData.normalMatrix, float4(inputValue.normal.xyz, 0.0f));
    float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglInteractiveCubesOrthoVertexOutput outputValue;
    outputValue.position = clipPosition;
    const float3 worldNormal = worldNormal4.xyz;
    outputValue.worldNormal = normalize(worldNormal);
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates directional Lambert shading and pointer-hit emissive color. */
WebglInteractiveCubesOrthoFrameBuffer webglInteractiveCubesOrthoShade(
    IN RenderSet<WebglInteractiveCubesOrthoSceneRenderSet> sceneSet,
    WebglInteractiveCubesOrthoVertexOutput inputValue)
{
    const WebglInteractiveCubesOrthoObjectData objectData =
        sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglInteractiveCubesOrthoMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 lightDirection = objectData.lightDirectionAndIntensity.xyz;
    const float diffuse = max(dot(normalize(inputValue.worldNormal),
                                  normalize(lightDirection)), 0.0f);
    const float3 linearColor = materialData.baseColor.xyz *
        (diffuse * objectData.lightDirectionAndIntensity.w * 0.31830988618379f) +
        materialData.emissive.xyz;
    WebglInteractiveCubesOrthoFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webglInteractiveCubesOrthoLinearToSrgb(linearColor.x),
        webglInteractiveCubesOrthoLinearToSrgb(linearColor.y),
        webglInteractiveCubesOrthoLinearToSrgb(linearColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws all cubes through the unique Scene RenderSet indexed-indirect path. */
class WebglInteractiveCubesOrthoScenePass final : public IRenderClass
{
public:
    /** Configures opaque depth-tested orthographic Lambert rendering. */
    constructor(RenderSet<WebglInteractiveCubesOrthoSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves both RenderEntity builtins required by the RenderSet path. */
    WebglInteractiveCubesOrthoVertexOutput vertex(
        WebglInteractiveCubesOrthoVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveCubesOrthoTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the lit RGBA8 cube color. */
    WebglInteractiveCubesOrthoFrameBuffer fragment(
        WebglInteractiveCubesOrthoVertexOutput inputValue)
    {
        const WebglInteractiveCubesOrthoFrameBuffer shaded =
            webglInteractiveCubesOrthoShade(sceneSet, inputValue);
        WebglInteractiveCubesOrthoFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns this shard's one RenderSet and explicit single-sample attachments. */
class WebglInteractiveCubesOrthoRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractiveCubesOrthoSceneRenderSet> sceneSet;
    RenderClass<WebglInteractiveCubesOrthoScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the dedicated orthographic RenderSet and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractiveCubesOrthoSceneRenderSet>();
        scenePass = device->createRenderClass<WebglInteractiveCubesOrthoScenePass>(sceneSet);
    }

    /** Allocates ordinary single-sample capture attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglInteractiveCubesOrthoColor", width, height, 1u);
        outputDepth = device->createTexture("WebglInteractiveCubesOrthoDepth", width, height, 1u);
    }

    /** Submits one indexed-indirect Scene pass and presents the result. */
    void render() override
    {
        sceneSet->update();
        WebglInteractiveCubesOrthoFrameBuffer frameBuffer;
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
            ->renderPass("WebglInteractiveCubesOrthoScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used by host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and explicit single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglInteractiveCubesOrthoRenderer
#undef WebglInteractiveCubesOrthoFrameBuffer
#undef WebglInteractiveCubesOrthoScenePass
#undef WebglInteractiveCubesOrthoSceneRenderSet

#endif
