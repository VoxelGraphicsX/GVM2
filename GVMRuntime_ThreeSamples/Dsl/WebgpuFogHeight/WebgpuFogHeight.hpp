#ifndef GVM_THREE_WEBGPU_FOG_HEIGHT_HPP
#define GVM_THREE_WEBGPU_FOG_HEIGHT_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the box geometry used by the 100-instance height-fog field. */
struct WebgpuFogHeightVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores camera, mesh, light, and fixed fog controls for the Scene entity. */
struct WebgpuFogHeightObjectData
{
    float4x4 viewProjection;
    float4x4 model;
    float4x4 view;
    float4 cameraPositionAndDensity;
    float4 fogHeightAndReserved;
    float4 lightDirectionAndIntensity;
    float4 lightColor;
};

/** Stores one instance matrix and its stable grid ordinal. */
struct WebgpuFogHeightInstanceData
{
    float4 transformColumn0;
    float4 transformColumn1;
    float4 transformColumn2;
    float4 transformColumn3;
    float4 ordinal;
};

/** Stores the linear MeshPhong base color and ambient strength. */
struct WebgpuFogHeightMaterialData
{
    float4 baseColorAndAmbient;
    float4 specularColorAndShininess;
};

/** Defines one Scene RenderSet with explicit fog and phase components. */
struct WebgpuFogHeightSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, 100 instance records, and fog state. */
    constructor(
        BufferComponent<WebgpuFogHeightVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuFogHeightObjectData> objects,
        BufferComponent<WebgpuFogHeightInstanceData> instances,
        BufferComponent<WebgpuFogHeightMaterialData> materials,
        BufferComponent<float4> fogParameters,
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Carries world position, normal, and entity identity into the fog fragment stage. */
struct WebgpuFogHeightVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the ordinary single-sample RGBA8/depth capture attachments. */
struct WebgpuFogHeightFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear color channel to the r185 canvas output transfer. */
float webgpuFogHeightLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the exponential height fog factor used by the TSL example. */
float webgpuFogHeightFactor(
    WebgpuFogHeightObjectData objectData,
    float3 worldPosition)
{
    const float density = objectData.cameraPositionAndDensity.w;
    const float height = objectData.fogHeightAndReserved.x;
    const float4 viewPosition = mul(objectData.view, float4(worldPosition, 1.0f));
    const float viewZ = max(-viewPosition.z, 0.0f);
    const float heightDistance = max(height - worldPosition.y, 0.0f);
    const float distanceTerm = heightDistance * viewZ;
    return saturate(1.0f - exp(-density * density * distanceTerm * distanceTerm));
}

/** Resolves entity and instance builtins and applies the grid instance matrix. */
WebgpuFogHeightVertexOutput webgpuFogHeightTransformVertex(
    IN RenderSet<WebgpuFogHeightSceneRenderSet> sceneSet,
    WebgpuFogHeightVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebgpuFogHeightObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebgpuFogHeightInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 instancePosition =
        instanceData.transformColumn0 * inputValue.position.x +
        instanceData.transformColumn1 * inputValue.position.y +
        instanceData.transformColumn2 * inputValue.position.z +
        instanceData.transformColumn3 * inputValue.position.w;
    const float4 worldPosition4 = mul(objectData.model, instancePosition);
    const float4 instanceNormal =
        instanceData.transformColumn0 * inputValue.normal.x +
        instanceData.transformColumn1 * inputValue.normal.y +
        instanceData.transformColumn2 * inputValue.normal.z;
    const float4 worldNormal4 = mul(objectData.model, instanceNormal);
    WebgpuFogHeightVertexOutput outputValue;
    outputValue.position = mul(objectData.viewProjection, worldPosition4);
    outputValue.worldPosition = worldPosition4.xyz;
    const float3 worldNormal = float3(worldNormal4.x, worldNormal4.y, worldNormal4.z);
    outputValue.worldNormal = normalize(worldNormal);
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates ambient/directional MeshPhong lighting and exponential height fog. */
WebgpuFogHeightFrameBuffer webgpuFogHeightShade(
    IN RenderSet<WebgpuFogHeightSceneRenderSet> sceneSet,
    WebgpuFogHeightVertexOutput inputValue)
{
    const WebgpuFogHeightObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
    const WebgpuFogHeightMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 normal = normalize(inputValue.worldNormal);
    const float3 lightDirection = normalize(-objectData.lightDirectionAndIntensity.xyz);
    const float diffuse = max(dot(normal, lightDirection), 0.0f);
    const float3 directionalIrradiance = diffuse * objectData.lightColor.xyz *
        objectData.lightDirectionAndIntensity.w;
    const float3 irradiance = float3(materialData.baseColorAndAmbient.w) +
        directionalIrradiance;
    const float3 litColor = materialData.baseColorAndAmbient.xyz * irradiance * (1.0f / 3.14159265359f);
    const float3 viewDirection = normalize(
        objectData.cameraPositionAndDensity.xyz - inputValue.worldPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotHalfNormal = max(dot(normal, halfDirection), 0.0f);
    const float dotViewHalf = max(dot(viewDirection, halfDirection), 0.0f);
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 specularF = materialData.specularColorAndShininess.xyz *
        (1.0f - fresnel) + float3(fresnel);
    const float specularD = (materialData.specularColorAndShininess.w * 0.5f + 1.0f) *
        (1.0f / 3.14159265359f) *
        pow(dotHalfNormal, materialData.specularColorAndShininess.w);
    const float3 directSpecular = objectData.lightColor.xyz * diffuse *
        objectData.lightDirectionAndIntensity.w *
        specularF * 0.25f * specularD;
    const float fogFactor = webgpuFogHeightFactor(objectData, inputValue.worldPosition);
    const float3 fogColor = float3(1.0f, 0.7379109f, 0.5394797f);
    const float3 displayColor = lerp(litColor + directSpecular, fogColor, fogFactor);
    WebgpuFogHeightFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webgpuFogHeightLinearToSrgb(displayColor.x),
        webgpuFogHeightLinearToSrgb(displayColor.y),
        webgpuFogHeightLinearToSrgb(displayColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws the complete 100-instance grid through the RenderSet indirect path. */
class WebgpuFogHeightMainPass final : public IRenderClass
{
public:
    /** Configures opaque double-sided depth-tested MeshPhong rendering. */
    constructor(RenderSet<WebgpuFogHeightSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads RenderEntityID and RenderEntityInstanceID for every cube vertex. */
    WebgpuFogHeightVertexOutput vertex(
        WebgpuFogHeightVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuFogHeightTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the lit, height-fogged RGBA8 Scene color. */
    WebgpuFogHeightFrameBuffer fragment(WebgpuFogHeightVertexOutput inputValue)
    {
        const WebgpuFogHeightFrameBuffer shaded =
            webgpuFogHeightShade(sceneSet, inputValue);
        WebgpuFogHeightFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and explicit single-sample output. */
class WebgpuFogHeightRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuFogHeightSceneRenderSet> sceneSet;
    RenderClass<WebgpuFogHeightMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates this shard's RenderSet and dedicated Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuFogHeightSceneRenderSet>();
        scenePass = device->createRenderClass<WebgpuFogHeightMainPass>(sceneSet);
    }

    /** Allocates ordinary single-sample color and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuFogHeightColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuFogHeightDepth", width, height, 1u);
    }

    /** Updates the RenderSet and submits one indexed-indirect Scene pass. */
    void render() override
    {
        sceneSet->update();
        WebgpuFogHeightFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {1.0, 0.8745098, 0.7568628, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuFogHeightScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target for deterministic readback. */
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

#undef WebgpuFogHeightRenderer
#undef WebgpuFogHeightFrameBuffer
#undef WebgpuFogHeightMainPass
#undef WebgpuFogHeightSceneRenderSet

#endif
