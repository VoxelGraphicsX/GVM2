#ifndef GVM_THREE_WEBGL_INTERACTIVE_CUBES_HPP
#define GVM_THREE_WEBGL_INTERACTIVE_CUBES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one BoxGeometry vertex and its face normal. */
struct WebglInteractiveCubesVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the per-cube camera transform and directional light. */
struct WebglInteractiveCubesObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 lightDirectionAndIntensity;
};

/** Stores the required one-entry non-instanced component. */
struct WebglInteractiveCubesInstanceData
{
    float4 reserved;
};

/** Stores one deterministic Lambert color and pointer-hit emissive state. */
struct WebglInteractiveCubesMaterialData
{
    float4 baseColor;
    float4 emissive;
};

/** Defines the unique Scene RenderSet containing all 2,000 cube entities. */
struct WebglInteractiveCubesSceneRenderSet : public IRenderSet
{
    /** Declares consolidated BoxGeometry and per-entity transform/material storage. */
    constructor(
        BufferComponent<WebglInteractiveCubesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInteractiveCubesObjectData> objects,
        BufferComponent<WebglInteractiveCubesInstanceData> instances,
        BufferComponent<WebglInteractiveCubesMaterialData> materials)
    {
    }
};

/** Carries world normal and entity identity to the Lambert fragment stage. */
struct WebglInteractiveCubesVertexOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the ordinary single-sample color and depth target. */
struct WebglInteractiveCubesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear channel to the Three canvas transfer. */
float webglInteractiveCubesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms one cube vertex through the entity's RenderSet object record. */
WebglInteractiveCubesVertexOutput webglInteractiveCubesTransformVertex(
    IN RenderSet<WebglInteractiveCubesSceneRenderSet> sceneSet,
    WebglInteractiveCubesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglInteractiveCubesObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglInteractiveCubesInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
    const float4 worldNormal4 = mul(objectData.normalMatrix, float4(inputValue.normal.xyz, 0.0f));
    WebglInteractiveCubesVertexOutput outputValue;
    float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
    // Match the WebGL viewport convention while retaining a single-sample target.
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    outputValue.position = clipPosition;
    const float3 worldNormal = worldNormal4.xyz;
    outputValue.worldNormal = normalize(worldNormal);
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates the r185 directional Lambert material and pointer-hit emissive color. */
WebglInteractiveCubesFrameBuffer webglInteractiveCubesShade(
    IN RenderSet<WebglInteractiveCubesSceneRenderSet> sceneSet,
    WebglInteractiveCubesVertexOutput inputValue)
{
    const WebglInteractiveCubesObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglInteractiveCubesMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 lightDirection = objectData.lightDirectionAndIntensity.xyz;
    const float diffuse = max(dot(normalize(inputValue.worldNormal),
                                  normalize(lightDirection)), 0.0f);
    const float3 linearColor = materialData.baseColor.xyz *
        (diffuse * objectData.lightDirectionAndIntensity.w * 0.31830988618379f) +
        materialData.emissive.xyz;
    WebglInteractiveCubesFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webglInteractiveCubesLinearToSrgb(linearColor.x),
        webglInteractiveCubesLinearToSrgb(linearColor.y),
        webglInteractiveCubesLinearToSrgb(linearColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws all 2,000 cubes through the unique Scene RenderSet indexed-indirect path. */
class WebglInteractiveCubesScenePass final : public IRenderClass
{
public:
    /** Configures opaque depth-tested Lambert rendering. */
    constructor(RenderSet<WebglInteractiveCubesSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves RenderEntityID and the mandatory instance builtin. */
    WebglInteractiveCubesVertexOutput vertex(
        WebglInteractiveCubesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglInteractiveCubesTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes lit RGBA8 cube color. */
    WebglInteractiveCubesFrameBuffer fragment(WebglInteractiveCubesVertexOutput inputValue)
    {
        const WebglInteractiveCubesFrameBuffer shaded =
            webglInteractiveCubesShade(sceneSet, inputValue);
        WebglInteractiveCubesFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and explicit single-sample targets. */
class WebglInteractiveCubesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractiveCubesSceneRenderSet> sceneSet;
    RenderClass<WebglInteractiveCubesScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the dedicated RenderSet and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractiveCubesSceneRenderSet>();
        scenePass = device->createRenderClass<WebglInteractiveCubesScenePass>(sceneSet);
    }

    /** Allocates ordinary single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglInteractiveCubesColor", width, height, 1u);
        outputDepth = device->createTexture("WebglInteractiveCubesDepth", width, height, 1u);
    }

    /** Updates the RenderSet and submits one indexed-indirect Scene pass. */
    void render() override
    {
        sceneSet->update();
        WebglInteractiveCubesFrameBuffer frameBuffer;
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
            ->renderPass("WebglInteractiveCubesScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final RGBA8 target for host readback. */
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

#undef WebglInteractiveCubesRenderer
#undef WebglInteractiveCubesFrameBuffer
#undef WebglInteractiveCubesScenePass
#undef WebglInteractiveCubesSceneRenderSet

#endif
