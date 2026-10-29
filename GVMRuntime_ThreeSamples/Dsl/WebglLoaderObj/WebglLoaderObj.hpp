#ifndef GVM_THREE_WEBGL_LOADER_OBJ_HPP
#define GVM_THREE_WEBGL_LOADER_OBJ_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one OBJ-normalized vertex. */
struct WebglLoaderObjVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores one loader mesh transform and lighting state. */
struct WebglLoaderObjObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 lightPositionAndIntensity;
};

/** Stores the mandatory one-entry instance component. */
struct WebglLoaderObjInstanceData
{
    float4 reserved;
};

/** Stores one OBJ/MTL material color. */
struct WebglLoaderObjMaterialData
{
    float4 baseColor;
};

/** Stores material phase and visibility flags from the loader. */
struct WebglLoaderObjRenderFlagsData
{
    uint4 flags;
};

/** Carries transformed position, normal, and entity identity. */
struct WebglLoaderObjVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines one Scene RenderSet for all expanded OBJ mesh sections. */
struct WebglLoaderObjSceneRenderSet : public IRenderSet
{
    /** Declares geometry, material, texture, and phase components. */
    constructor(
        BufferComponent<WebglLoaderObjVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderObjObjectData> objects,
        BufferComponent<WebglLoaderObjInstanceData> instances,
        BufferComponent<WebglLoaderObjMaterialData> materials,
        (TextureComponent<half4, 8u> textures),
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Binds the trilinear sampler used by decoded OBJ material textures. */
struct WebglLoaderObjSamplerResources final : public IBindGroup
{
    /** Declares the one immutable material sampler. */
    constructor(Sampler materialSampler [[Binding0]])
    {
    }
};

/** Defines ordinary single-sample color/depth output. */
struct WebglLoaderObjFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts linear light to the Three canvas transfer. */
float webglLoaderObjLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms one loader vertex through RenderSet entity data. */
WebglLoaderObjVertexOutput webglLoaderObjTransformVertex(
    IN RenderSet<WebglLoaderObjSceneRenderSet> sceneSet,
    WebglLoaderObjVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLoaderObjObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglLoaderObjInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
    const float4 viewPosition = mul(objectData.modelView, localPosition);
    const float4 normal4 = mul(objectData.modelView, float4(inputValue.normal.xyz, 0.0f));
    float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglLoaderObjVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewPosition.xyz;
    const float3 normal = normal4.xyz;
    outputValue.worldNormal = normalize(normal);
    outputValue.uv = inputValue.uv.xy;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Shades one OBJ section using its material component and directional light. */
WebglLoaderObjFrameBuffer webglLoaderObjShade(
    IN RenderSet<WebglLoaderObjSceneRenderSet> sceneSet,
    BindGroup<WebglLoaderObjSamplerResources> samplerResources,
    WebglLoaderObjVertexOutput inputValue)
{
    const WebglLoaderObjObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglLoaderObjMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
    const uint4 renderFlags = sceneSet->renderFlags->get(inputValue.entityID, 0u);
    const float3 lightVector = objectData.lightPositionAndIntensity.xyz - inputValue.viewPosition;
    const float distanceSquared = max(dot(lightVector, lightVector), 0.0001f);
    const float diffuse = max(dot(normalize(inputValue.worldNormal),
                                  normalize(lightVector)), 0.0f);
    float3 albedo = materialData.baseColor.xyz;
    if (renderFlags.y != 0u)
    {
        albedo *= float3(sceneSet->textures->get(inputValue.entityID, 0u)
            ->sample(samplerResources->materialSampler, inputValue.uv).xyz);
    }
    const float irradiance = 0.31830988618379f *
        (1.0f + diffuse * objectData.lightPositionAndIntensity.w / distanceSquared);
    const float3 linearColor = albedo * irradiance;
    WebglLoaderObjFrameBuffer frameBuffer;
    frameBuffer.color = half4(half3(
        webglLoaderObjLinearToSrgb(linearColor.x),
        webglLoaderObjLinearToSrgb(linearColor.y),
        webglLoaderObjLinearToSrgb(linearColor.z)), half(1.0f));
    return frameBuffer;
}

/** Draws every expanded OBJ section through one indexed-indirect pass. */
class WebglLoaderObjScenePass final : public IRenderClass
{
public:
    /** Configures depth-tested opaque loader rendering. */
    constructor(RenderSet<WebglLoaderObjSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglLoaderObjSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves both RenderEntity builtins from the Scene Set. */
    WebglLoaderObjVertexOutput vertex(
        WebglLoaderObjVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderObjTransformVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the loader material color. */
    WebglLoaderObjFrameBuffer fragment(WebglLoaderObjVertexOutput inputValue)
    {
        const WebglLoaderObjFrameBuffer shaded = webglLoaderObjShade(
            sceneSet, samplerResources, inputValue);
        WebglLoaderObjFrameBuffer frameBuffer;
        frameBuffer.color = shaded.color;
        return frameBuffer;
    }
};

/** Owns the loader Scene RenderSet and explicit single-sample output. */
class WebglLoaderObjRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderObjSceneRenderSet> sceneSet;
    Sampler materialSampler;
    BindGroup<WebglLoaderObjSamplerResources> samplerResources;
    RenderClass<WebglLoaderObjScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one Scene RenderSet and loader pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderObjSceneRenderSet>();
        materialSampler = device->createSampler({
            .label = "WebglLoaderObjMaterialSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglLoaderObjSamplerResources>(
            materialSampler);
        scenePass = device->createRenderClass<WebglLoaderObjScenePass>(
            sceneSet, samplerResources);
    }

    /** Allocates ordinary single-sample capture attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLoaderObjColor", width, height, 1u);
        outputDepth = device->createTexture("WebglLoaderObjDepth", width, height, 1u);
    }

    /** Submits the complete loader Scene with indirect RenderSet drawing. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderObjFrameBuffer frameBuffer;
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
            ->renderPass("WebglLoaderObjScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases RenderSet and ordinary attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglLoaderObjRenderer
#undef WebglLoaderObjFrameBuffer
#undef WebglLoaderObjScenePass
#undef WebglLoaderObjSceneRenderSet

#endif
