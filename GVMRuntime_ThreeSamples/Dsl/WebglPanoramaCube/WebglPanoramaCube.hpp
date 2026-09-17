#ifndef GVM_THREE_WEBGL_PANORAMA_CUBE_HPP
#define GVM_THREE_WEBGL_PANORAMA_CUBE_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglPanoramaCubeTextureCapacity = 8u;

/** Stores one inward BoxGeometry vertex and its atlas face slot. */
struct WebglPanoramaCubeVertex
{
    float4 position [[Attribute0]];
    float4 textureCoordinate [[Attribute1]];
    uint4 faceSlot [[Attribute2]];
};

/** Stores the canonical perspective and damped OrbitControls transform. */
struct WebglPanoramaCubeObjectData
{
    float4x4 modelViewProjection;
};

/** Stores the mandatory ordinary-entity instance record. */
struct WebglPanoramaCubeInstanceData
{
    float4 reserved;
};

/** Stores the six-group material contract for the panorama entity. */
struct WebglPanoramaCubeMaterialData
{
    uint4 faceCountAndReserved;
};

/** Defines the unique one-entity Scene RenderSet with six texture slots. */
struct WebglPanoramaCubeSceneRenderSet : public IRenderSet
{
    /** Declares unified geometry and all per-entity panorama components. */
    constructor(
        BufferComponent<WebglPanoramaCubeVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPanoramaCubeObjectData> objects,
        BufferComponent<WebglPanoramaCubeInstanceData> instances,
        BufferComponent<WebglPanoramaCubeMaterialData> materials,
        (TextureComponent<half4, WebglPanoramaCubeTextureCapacity> textures))
    {
    }
};

/** Binds the trilinear sampler shared by the six entity-local textures. */
struct WebglPanoramaCubeResources final : public IBindGroup
{
    /** Declares the immutable clamp sampler without standalone textures. */
    constructor(Sampler panoramaSampler [[Binding0]])
    {
    }
};

/** Carries the selected geometry-group texture slot into the fragment stage. */
struct WebglPanoramaCubeVertexOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
    uint2 entityAndFace [[Attribute1]];
};

/** Defines the final single-sample RGBA8 and depth attachments. */
struct WebglPanoramaCubeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one nonnegative linear-light channel to output sRGB. */
float webglPanoramaCubeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all six inward BoxGeometry groups through the unique Scene Set. */
class WebglPanoramaCubeMainPass final : public IRenderClass
{
public:
    /** Binds one Scene Set and configures Three's opaque front-side state. */
    constructor(
        RenderSet<WebglPanoramaCubeSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPanoramaCubeResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the entity transform and preserves its per-face texture index. */
    WebglPanoramaCubeVertexOutput vertex(
        WebglPanoramaCubeVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPanoramaCubeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPanoramaCubeInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        WebglPanoramaCubeVertexOutput outputValue;
        outputValue.position = mul(
            objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityAndFace = uint2(
            renderEntityID, inputValue.faceSlot.x);
        return outputValue;
    }

    /** Samples the selected sRGB tile and applies the default output transfer. */
    WebglPanoramaCubeFrameBuffer fragment(
        WebglPanoramaCubeVertexOutput inputValue)
    {
        const WebglPanoramaCubeMaterialData materialData =
            sceneSet->materials->get(inputValue.entityAndFace.x, 0u);
        if (inputValue.entityAndFace.y >=
            materialData.faceCountAndReserved.x)
        {
            discard_fragment();
        }
        auto faceTexture = sceneSet->textures->get(
            inputValue.entityAndFace.x,
            inputValue.entityAndFace.y);
        const float2 sampleCoordinate = float2(
            inputValue.textureCoordinate.x,
            1.0f - inputValue.textureCoordinate.y);
        const float3 linearColor = float4(faceTexture->sample(
            resources->panoramaSampler, sampleCoordinate)).xyz;
        WebglPanoramaCubeFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPanoramaCubeLinearToSrgb(linearColor.x)),
            half(webglPanoramaCubeLinearToSrgb(linearColor.y)),
            half(webglPanoramaCubeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique panorama Scene Set and single-sample render targets. */
class WebglPanoramaCubeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPanoramaCubeSceneRenderSet> sceneSet;
    Sampler panoramaSampler;
    BindGroup<WebglPanoramaCubeResources> resources;
    RenderClass<WebglPanoramaCubeMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Set, sampler, and Set-only Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglPanoramaCubeSceneRenderSet>();
        panoramaSampler = device->createSampler({
            .label = "WebglPanoramaCubeSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 10.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebglPanoramaCubeResources>(
            panoramaSampler);
        mainPass = device->createRenderClass<WebglPanoramaCubeMainPass>(
            sceneSet, resources);
    }

    /** Allocates deterministic RGBA8 and depth outputs at the host extent. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglPanoramaCubeOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglPanoramaCubeDepth", width, height, 1u);
    }

    /** Updates entity metadata and draws the sole Scene Set indirectly. */
    void render() override
    {
        sceneSet->update();
        WebglPanoramaCubeFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglPanoramaCubeMain",
                frameBuffer,
                mainPass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 texture used by formal readback. */
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

    /** Releases the Scene Set and explicit single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
