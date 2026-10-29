#ifndef GVM_THREE_WEBGL_INTERACTIVE_POINTS_HPP
#define GVM_THREE_WEBGL_INTERACTIVE_POINTS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglInteractivePointsTextureCapacity = 2u;

/** Stores one fully CPU-expanded point vertex. */
struct WebglInteractivePointsVertex
{
    float4 positionAndSize [[Attribute0]];
    float4 color [[Attribute1]];
    float4 corner [[Attribute2]];
};

/** Stores the current projection and model-view matrices. */
struct WebglInteractivePointsObjectData
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Stores the required non-instanced component record. */
struct WebglInteractivePointsInstanceData
{
    float4 reserved;
};

/** Stores the shared ShaderMaterial color and alpha-test threshold. */
struct WebglInteractivePointsMaterialData
{
    float4 colorAndAlphaTest;
};

/** Defines the only Scene RenderSet used by the interactive points example. */
struct WebglInteractivePointsSceneRenderSet : public IRenderSet
{
    /** Declares the point template and all entity-owned components. */
    constructor(
        BufferComponent<WebglInteractivePointsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInteractivePointsObjectData> objects,
        BufferComponent<WebglInteractivePointsInstanceData> instances,
        BufferComponent<WebglInteractivePointsMaterialData> materials,
        (TextureComponent<float4, WebglInteractivePointsTextureCapacity> textures))
    {
    }
};

/** Binds the immutable linear sampler used for disc.png. */
struct WebglInteractivePointsSamplerResources final : public IBindGroup
{
    /** Declares the sole point-texture sampler. */
    constructor(Sampler pointSampler [[Binding0]])
    {
    }
};

/** Carries point color, PointCoord, and entity identity to the fragment stage. */
struct WebglInteractivePointsVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float2 pointCoord [[Attribute1]];
    float pointSize [[Attribute2]];
    uint entityID [[Attribute3]];
    float4 pointCoverage [[Attribute4]];
};

/** Defines the single-sample RGBA8 and depth output. */
struct WebglInteractivePointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws all 1,538 logical points through one RenderSet indirect command. */
class WebglInteractivePointsMainPass final : public IRenderClass
{
public:
    /** Reproduces the opaque alpha-tested ShaderMaterial depth state. */
    constructor(
        RenderSet<WebglInteractivePointsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglInteractivePointsSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one RenderSet instance into a native-size perspective point quad. */
    WebglInteractivePointsVertexOutput vertex(
        WebglInteractivePointsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebglInteractivePointsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const float4 viewPosition = mul(
            objectData.modelViewMatrix,
            float4(inputValue.positionAndSize.xyz, 1.0f));
        float4 clipPosition = mul(objectData.projectionMatrix, viewPosition);
        const float rawPointSize =
            inputValue.positionAndSize.w * (300.0f / -viewPosition.z);
        const float pointSize = clamp(rawPointSize, 1.0f, 511.0f);
        const float2 pointCenter =
            (clipPosition.xy / clipPosition.w + float2(1.0f)) *
            float2(400.0f, 250.0f);
        const float2 topPointCenter =
            float2(pointCenter.x, 500.0f - pointCenter.y);
        const float halfPointSize = pointSize * 0.5f;
        const float2 roundedCoverageLow = floor(
            (topPointCenter - float2(halfPointSize)) * 16.0f +
            float2(0.5f)) / 16.0f;
        const float2 roundedCoverageHigh = floor(
            (topPointCenter + float2(halfPointSize)) * 16.0f +
            float2(0.5f)) / 16.0f;
        const float2 coverageSize =
            roundedCoverageHigh - roundedCoverageLow;
        const float2 pointCoverageEdge = floor(
            float2(
                topPointCenter.x - halfPointSize,
                topPointCenter.y - (coverageSize.y - halfPointSize)) *
                256.0f + float2(0.5f)) / 256.0f;
        const float2 pointCoverageCenter =
            pointCoverageEdge + coverageSize * 0.5f;
        const float2 conservativeCoverageSize =
            coverageSize;
        clipPosition.x =
            (pointCoverageCenter.x / 400.0f - 1.0f) * clipPosition.w;
        clipPosition.y =
            ((500.0f - pointCoverageCenter.y) / 250.0f - 1.0f) *
            clipPosition.w;
        clipPosition.x +=
            inputValue.corner.x * conservativeCoverageSize.x / 800.0f *
            clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * conservativeCoverageSize.y / 500.0f *
            clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglInteractivePointsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = inputValue.color.xyz;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        outputValue.pointSize = pointSize;
        outputValue.entityID = renderEntityID;
        outputValue.pointCoverage = float4(
            pointCoverageEdge.x,
            pointCoverageEdge.y,
            coverageSize.x,
            coverageSize.y);
        return outputValue;
    }

    /** Applies the raw disc texture and the exact alpha-test cutoff. */
    WebglInteractivePointsFrameBuffer fragment(
        WebglInteractivePointsVertexOutput inputValue)
    {
        const WebglInteractivePointsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        auto pointTexture =
            sceneSet->textures->get(inputValue.entityID, 0u);
        const float2 pointCoord =
            (inputValue.position.xy - inputValue.pointCoverage.xy) /
            inputValue.pointCoverage.zw;
        clip(pointCoord.x);
        clip(pointCoord.y);
        clip(1.0f - pointCoord.x);
        clip(1.0f - pointCoord.y);
        const float4 texel = float4(pointTexture->sample(
            samplerResources->pointSampler,
            pointCoord));
        const float4 fragmentColor =
            float4(inputValue.color, 1.0f) *
            float4(materialData.colorAndAlphaTest.xyz, 1.0f) *
            texel;
        clip(fragmentColor.a - materialData.colorAndAlphaTest.w);
        WebglInteractivePointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(fragmentColor);
        return frameBuffer;
    }
};

/** Owns the dedicated Scene RenderSet and deterministic offscreen output. */
class WebglInteractivePointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInteractivePointsSceneRenderSet> sceneSet;
    Sampler pointSampler;
    BindGroup<WebglInteractivePointsSamplerResources> samplerResources;
    RenderClass<WebglInteractivePointsMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates one Scene RenderSet and its RenderSet-only pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInteractivePointsSceneRenderSet>();
        pointSampler = device->createSampler({
            .label = "WebglInteractivePointsDiscSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 5.0f,
            .maxAnisotropy = 1u,
        });
        samplerResources = device->createBindGroup<
            WebglInteractivePointsSamplerResources>(pointSampler);
        mainPass = device->createRenderClass<WebglInteractivePointsMainPass>(
            sceneSet,
            samplerResources);
    }

    /** Allocates the requested ordinary single-sample output attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglInteractivePointsRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglInteractivePointsDepth", width, height, 1u);
    }

    /** Updates entity metadata and submits one indexed-indirect Scene draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebglInteractivePointsFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass(
                "WebglInteractivePointsScene",
                frameBuffer,
                mainPass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created texture used for strict RGBA8 readback. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the only Scene RenderSet and output resources. */
    void destroy() override
    {
        sceneSet->destroy();
        outputTexture->destroy();
        depthTexture->destroy();
    }
};

#endif
