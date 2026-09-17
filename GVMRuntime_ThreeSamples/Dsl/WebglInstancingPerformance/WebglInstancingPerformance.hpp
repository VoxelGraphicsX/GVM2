#ifndef GVM_THREE_WEBGL_INSTANCING_PERFORMANCE_HPP
#define GVM_THREE_WEBGL_INSTANCING_PERFORMANCE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Suzanne position and smooth normal. */
struct WebglInstancingPerformanceVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores one entity model transform and shared camera matrix. */
struct WebglInstancingPerformanceObjectData
{
    float4x4 viewProjection;
    float4x4 model;
    float4 modeAndTime;
};

/** Stores three rows of one seeded affine instance transform. */
struct WebglInstancingPerformanceInstanceData
{
    float4 transformRow0;
    float4 transformRow1;
    float4 transformRow2;
};

/** Stores private MeshNormalMaterial opacity controls. */
struct WebglInstancingPerformanceMaterialData
{
    float4 opacityAndFlags;
};

/** Defines the sole Scene RenderSet for Instanced, Merged, and Naive modes. */
struct WebglInstancingPerformanceSceneRenderSet : public IRenderSet
{
    /** Declares the common Suzanne union plus object, instance, and material components. */
    constructor(
        BufferComponent<WebglInstancingPerformanceVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInstancingPerformanceObjectData> objects,
        BufferComponent<WebglInstancingPerformanceInstanceData> instances,
        BufferComponent<WebglInstancingPerformanceMaterialData> materials)
    {
    }
};

/** Carries the final view normal and RenderSet entity identity. */
struct WebglInstancingPerformanceVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the final RGBA8 color and depth attachments. */
struct WebglInstancingPerformanceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the single-sample Scene color for the output copy pass. */
struct WebglInstancingPerformanceResolveResources final : public IBindGroup
{
    /** Declares the single-sample color and output sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries fullscreen coordinates into the resolve pass. */
struct WebglInstancingPerformanceResolveOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the final RGBA8 color-only readback attachment. */
struct WebglInstancingPerformanceOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear color channel to the r185 output transfer. */
float webglInstancingPerformanceLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws every mode through the same RenderSet indexed-indirect entry. */
class WebglInstancingPerformanceMainPass final : public IRenderClass
{
public:
    /** Configures the opaque double-sided MeshNormalMaterial state. */
    constructor(
        RenderSet<WebglInstancingPerformanceSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies entity and instance transforms while consuming both entity builtins. */
    WebglInstancingPerformanceVertexOutput vertex(
        WebglInstancingPerformanceVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingPerformanceObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglInstancingPerformanceInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 instancePosition = float4(
            dot(instanceData.transformRow0, inputValue.position),
            dot(instanceData.transformRow1, inputValue.position),
            dot(instanceData.transformRow2, inputValue.position),
            1.0f);
        const float4 instanceNormal = float4(
            instanceData.transformRow0.x * inputValue.normal.x +
                instanceData.transformRow0.y * inputValue.normal.y +
                instanceData.transformRow0.z * inputValue.normal.z,
            instanceData.transformRow1.x * inputValue.normal.x +
                instanceData.transformRow1.y * inputValue.normal.y +
                instanceData.transformRow1.z * inputValue.normal.z,
            instanceData.transformRow2.x * inputValue.normal.x +
                instanceData.transformRow2.y * inputValue.normal.y +
                instanceData.transformRow2.z * inputValue.normal.z,
            0.0f);
        const float4 worldPosition =
            mul(objectData.model, instancePosition);
        const float4 transformedNormal =
            mul(objectData.model, instanceNormal);
        WebglInstancingPerformanceVertexOutput outputValue;
        outputValue.position =
            mul(objectData.viewProjection, worldPosition);
        outputValue.viewNormal =
            normalize(float3(
                transformedNormal.x,
                transformedNormal.y,
                transformedNormal.z));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Packs the view normal and applies standard output conversion. */
    WebglInstancingPerformanceFrameBuffer fragment(
        WebglInstancingPerformanceVertexOutput inputValue)
    {
        const WebglInstancingPerformanceMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 linearColor =
            normalize(inputValue.viewNormal) * 0.5f +
            float3(0.5f);
        WebglInstancingPerformanceFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor),
            half(materialData.opacityAndFlags.x));
        return frameBuffer;
    }
};

/** Copies the single-sample Scene target into the required 800x500 output. */
class WebglInstancingPerformanceResolvePass final : public IRenderClass
{
public:
    /** Binds only screen-space Scene color resources. */
    constructor(
        BindGroup<WebglInstancingPerformanceResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebglInstancingPerformanceResolveOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglInstancingPerformanceResolveOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads the one centered Scene sample without antialias emulation. */
    WebglInstancingPerformanceOutputFrameBuffer fragment(
        WebglInstancingPerformanceResolveOutput inputValue)
    {
        WebglInstancingPerformanceOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->sceneColor->sample(
            resources->sceneSampler,
            inputValue.uv));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet for all three performance modes. */
class WebglInstancingPerformanceRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInstancingPerformanceSceneRenderSet> sceneSet;
    RenderClass<WebglInstancingPerformanceMainPass> scenePass;
    Sampler resolveSampler;
    BindGroup<WebglInstancingPerformanceResolveResources> resolveResources;
    RenderClass<WebglInstancingPerformanceResolvePass> resolvePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and dedicated normal-material pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglInstancingPerformanceSceneRenderSet>();
        scenePass =
            device->createRenderClass<
                WebglInstancingPerformanceMainPass>(sceneSet);
        resolveSampler = device->createSampler({
            .label = "WebglInstancingPerformanceResolveSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates final color and depth attachments at the host extent. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebglInstancingPerformanceSceneRGBA8",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebglInstancingPerformanceOutputRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglInstancingPerformanceDepth32",
            width,
            height,
            1u);
        resolveResources =
            device->createBindGroup<
                WebglInstancingPerformanceResolveResources>(
                sceneTexture->createView(),
                resolveSampler);
        resolvePass =
            device->createRenderClass<
                WebglInstancingPerformanceResolvePass>(
                resolveResources);
    }

    /** Updates entity metadata and draws all active mode entities automatically. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebglInstancingPerformanceFrameBuffer frameBuffer;
        frameBuffer.color = sceneTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue =
            {1.0f, 1.0f, 1.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglInstancingPerformanceOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {1.0f, 1.0f, 1.0f, 1.0f};
        graphicsQueue
            ->renderPass(
                "WebglInstancingPerformanceScene",
                frameBuffer,
                scenePass())
            ->renderPass(
                "WebglInstancingPerformanceResolve",
                outputFrameBuffer,
                resolvePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene RenderSet and final attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
