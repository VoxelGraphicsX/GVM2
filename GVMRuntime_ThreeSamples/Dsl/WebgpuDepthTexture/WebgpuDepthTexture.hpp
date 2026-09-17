#ifndef GVM_THREE_WEBGPU_DEPTH_TEXTURE_HPP
#define GVM_THREE_WEBGPU_DEPTH_TEXTURE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one TorusKnotGeometry vertex in the Scene RenderSet. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
};

/** Carries the camera and per-entity transform to the Scene vertex stage. */
struct ThreeBasicObjectData
{
    float4x4 modelViewProjection;
};

/** Keeps the mandatory non-instanced instance component explicit. */
struct ThreeBasicInstanceData
{
    float4 reserved;
};

/** Stores the MeshBasicNodeMaterial color for one Scene entity. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
};

/** Defines the one RenderSet shared by the depth Scene and every geometry draw. */
struct WebgpuDepthTextureSceneRenderSet : public IRenderSet
{
    /** Declares the packed TorusKnot geometry and per-entity components. */
    constructor(BufferComponent<ThreeBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ThreeBasicObjectData> objects,
                BufferComponent<ThreeBasicInstanceData> instances,
                BufferComponent<ThreeBasicMaterialData> materials)
    {
    }
};

/** Carries the transformed position and entity identity into the Scene fragment stage. */
struct WebgpuDepthTextureSceneOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
};

/** Defines the single-sample color and Depth32Float attachments for the Scene pass. */
struct WebgpuDepthTextureSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the Scene depth attachment for the ordinary fullscreen visualization pass. */
struct WebgpuDepthTextureScreenResources final : public IBindGroup
{
    /** Declares the sampled Depth32Float texture and deterministic nearest sampler. */
    constructor(Texture2D<TextureFormat::Depth32Float> depthTexture [[Binding0]],
                Sampler depthSampler [[Binding1]])
    {
    }
};

/** Carries fullscreen UV coordinates into the depth visualization fragment stage. */
struct WebgpuDepthTextureScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the final RGBA8 capture attachment. */
struct WebgpuDepthTextureOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts a depth value to the r185 output color-space transfer. */
float webgpuDepthTextureLinearToSrgb(float value)
{
    const float clamped = saturate(value);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws every TorusKnot entity through the RenderSet indexed-indirect entry point. */
class WebgpuDepthTextureScenePass final : public IRenderClass
{
public:
    /** Configures the MeshBasic depth-tested, single-sample Scene state. */
    constructor(RenderSet<WebgpuDepthTextureSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the canonical camera/entity matrix and reads both RenderSet builtins. */
    WebgpuDepthTextureSceneOutput vertex(
        ThreeBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ThreeBasicObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const ThreeBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebgpuDepthTextureSceneOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, inputValue.position);
        outputValue.position.xy += instanceData.reserved.xy * 0.0f;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Writes an opaque white MeshBasic surface while depth is retained in the attachment. */
    WebgpuDepthTextureSceneFrameBuffer fragment(
        WebgpuDepthTextureSceneOutput inputValue)
    {
        const ThreeBasicObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        (void)objectData;
        const ThreeBasicMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuDepthTextureSceneFrameBuffer frameBuffer;
        float3 baseColor = materialData.baseColor.xyz;
        baseColor = saturate(baseColor);
        frameBuffer.color = half4(half3(baseColor), half(1.0f));
        return frameBuffer;
    }
};

/** Visualizes the captured DepthTexture without redrawing Scene geometry. */
class WebgpuDepthTextureScreenPass final : public IRenderClass
{
public:
    /** Configures the fullscreen pass as an ordinary depth-disabled RenderClass. */
    constructor(BindGroup<WebgpuDepthTextureScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits the standard oversized fullscreen triangle. */
    WebgpuDepthTextureScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuDepthTextureScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Samples the actual Depth32Float attachment and applies output transfer. */
    WebgpuDepthTextureOutputFrameBuffer fragment(
        WebgpuDepthTextureScreenOutput inputValue)
    {
        const float depth = resources->depthTexture->sampleLevel(
            resources->depthSampler, inputValue.uv, 0.0f).x;
        const float encodedDepth = webgpuDepthTextureLinearToSrgb(depth);
        WebgpuDepthTextureOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encodedDepth), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet, the depth attachment, and the fullscreen pass. */
class WebgpuDepthTextureRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuDepthTextureSceneRenderSet> sceneSet;
    RenderClass<WebgpuDepthTextureScenePass> scenePass;
    RenderClass<WebgpuDepthTextureScreenPass> screenPass;
    Sampler depthSampler;
    BindGroup<WebgpuDepthTextureScreenResources> screenResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the RenderSet, geometry pass, sampler, and screen RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuDepthTextureSceneRenderSet>();
        scenePass = device->createRenderClass<WebgpuDepthTextureScenePass>(sceneSet);
        depthSampler = device->createSampler({
            .label = "WebgpuDepthTextureDepthSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates explicit single-sample Scene and output targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneColor = device->createTexture("WebgpuDepthTextureSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("WebgpuDepthTextureDepth32Float", width, height, 1u);
        outputColor = device->createTexture("WebgpuDepthTextureOutput", width, height, 1u);
        screenResources = device->createBindGroup<WebgpuDepthTextureScreenResources>(
            sceneDepth->createView(), depthSampler);
        screenPass = device->createRenderClass<WebgpuDepthTextureScreenPass>(screenResources);
    }

    /** Executes the Scene depth write followed by one depth-texture screen pass. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebgpuDepthTextureSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneColor->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.133333f, 0.133333f, 0.133333f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuDepthTextureOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        graphicsQueue
            ->renderPass("WebgpuDepthTextureScene", sceneFrame, scenePass())
            ->renderPass("WebgpuDepthTextureVisualization", outputFrame,
                         screenPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final RGBA8 screen-pass target for readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the RenderSet and all single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#undef WebgpuDepthTextureRenderer
#undef WebgpuDepthTextureOutputFrameBuffer
#undef WebgpuDepthTextureScreenPass
#undef WebgpuDepthTextureScreenResources
#undef WebgpuDepthTextureSceneFrameBuffer
#undef WebgpuDepthTextureScenePass
#undef WebgpuDepthTextureSceneOutput
#undef WebgpuDepthTextureSceneRenderSet
#undef ThreeBasicMaterialData
#undef ThreeBasicInstanceData
#undef ThreeBasicObjectData
#undef ThreeBasicVertex
#undef THREE_BASIC_JOIN

#endif
