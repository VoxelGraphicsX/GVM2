#ifndef GVM_THREE_WEBGL_DEPTH_TEXTURE_HPP
#define GVM_THREE_WEBGL_DEPTH_TEXTURE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one TorusKnotGeometry vertex in the Scene RenderSet. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
};

/** Carries the shared camera transform to the Scene vertex stage. */
struct ThreeBasicObjectData
{
    float4x4 viewProjection;
};

/** Stores one InstancedMesh model transform. */
struct ThreeBasicInstanceData
{
    float4x4 modelMatrix;
};

/** Stores the MeshBasicNodeMaterial color for one Scene entity. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
};

/** Defines the one RenderSet shared by the depth Scene and every geometry draw. */
struct WebglDepthTextureSceneRenderSet : public IRenderSet
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
struct WebglDepthTextureSceneOutput
{
    float4 position [[Position]];
    uint entityID [[Attribute0]];
};

/** Defines the single-sample color and Depth32Float attachments for the Scene pass. */
struct WebglDepthTextureSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the Scene depth attachment for the ordinary fullscreen visualization pass. */
struct WebglDepthTextureScreenResources final : public IBindGroup
{
    /** Declares the sampled Depth32Float texture and deterministic nearest sampler. */
    constructor(Texture2D<TextureFormat::Depth32Float> depthTexture [[Binding0]],
                Sampler depthSampler [[Binding1]])
    {
    }
};

/** Carries fullscreen UV coordinates into the depth visualization fragment stage. */
struct WebglDepthTextureScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the final RGBA8 capture attachment. */
struct WebglDepthTextureOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws every TorusKnot entity through the RenderSet indexed-indirect entry point. */
class WebglDepthTextureScenePass final : public IRenderClass
{
public:
    /** Configures the MeshBasic depth-tested, single-sample Scene state. */
    constructor(RenderSet<WebglDepthTextureSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the canonical camera/entity matrix and reads both RenderSet builtins. */
    WebglDepthTextureSceneOutput vertex(
        ThreeBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ThreeBasicObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const ThreeBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglDepthTextureSceneOutput outputValue;
        outputValue.position = mul(
            objectData.viewProjection,
            mul(instanceData.modelMatrix, inputValue.position));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Writes an opaque white MeshBasic surface while depth is retained in the attachment. */
    WebglDepthTextureSceneFrameBuffer fragment(
        WebglDepthTextureSceneOutput inputValue)
    {
        const ThreeBasicObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        (void)objectData;
        const ThreeBasicMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglDepthTextureSceneFrameBuffer frameBuffer;
        float3 baseColor = materialData.baseColor.xyz;
        baseColor = saturate(baseColor);
        frameBuffer.color = half4(half3(baseColor), half(1.0f));
        return frameBuffer;
    }
};

/** Visualizes the captured DepthTexture without redrawing Scene geometry. */
class WebglDepthTextureScreenPass final : public IRenderClass
{
public:
    /** Configures the fullscreen pass as an ordinary depth-disabled RenderClass. */
    constructor(BindGroup<WebglDepthTextureScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits the standard oversized fullscreen triangle. */
    WebglDepthTextureScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglDepthTextureScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Samples the actual Depth32Float attachment and applies output transfer. */
    WebglDepthTextureOutputFrameBuffer fragment(
        WebglDepthTextureScreenOutput inputValue)
    {
        const float fragmentDepth = resources->depthTexture->sampleLevel(
            resources->depthSampler, inputValue.uv, 0.0f).x;
        const float cameraNear = 0.01f;
        const float cameraFar = 50.0f;
        const float viewZ = cameraNear * cameraFar /
            ((cameraFar - cameraNear) * fragmentDepth - cameraFar);
        const float linearDepth = (viewZ + cameraNear) / (cameraNear - cameraFar);
        const float encodedDepth = saturate(1.0f - linearDepth);
        WebglDepthTextureOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encodedDepth), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet, the depth attachment, and the fullscreen pass. */
class WebglDepthTextureRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglDepthTextureSceneRenderSet> sceneSet;
    RenderClass<WebglDepthTextureScenePass> scenePass;
    RenderClass<WebglDepthTextureScreenPass> screenPass;
    Sampler depthSampler;
    BindGroup<WebglDepthTextureScreenResources> screenResources;
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
        sceneSet = device->createRenderSet<WebglDepthTextureSceneRenderSet>();
        scenePass = device->createRenderClass<WebglDepthTextureScenePass>(sceneSet);
        depthSampler = device->createSampler({
            .label = "WebglDepthTextureDepthSampler",
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
        sceneColor = device->createTexture("WebglDepthTextureSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("WebglDepthTextureDepth32Float", width, height, 1u);
        outputColor = device->createTexture("WebglDepthTextureOutput", width, height, 1u);
        screenResources = device->createBindGroup<WebglDepthTextureScreenResources>(
            sceneDepth->createView(), depthSampler);
        screenPass = device->createRenderClass<WebglDepthTextureScreenPass>(screenResources);
    }

    /** Executes the Scene depth write followed by one depth-texture screen pass. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglDepthTextureSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneColor->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.133333f, 0.133333f, 0.133333f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglDepthTextureOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        graphicsQueue
            ->renderPass("WebglDepthTextureScene", sceneFrame, scenePass())
            ->renderPass("WebglDepthTextureVisualization", outputFrame,
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

#undef WebglDepthTextureRenderer
#undef WebglDepthTextureOutputFrameBuffer
#undef WebglDepthTextureScreenPass
#undef WebglDepthTextureScreenResources
#undef WebglDepthTextureSceneFrameBuffer
#undef WebglDepthTextureScenePass
#undef WebglDepthTextureSceneOutput
#undef WebglDepthTextureSceneRenderSet
#undef ThreeBasicMaterialData
#undef ThreeBasicInstanceData
#undef ThreeBasicObjectData
#undef ThreeBasicVertex
#undef THREE_BASIC_JOIN

#endif
