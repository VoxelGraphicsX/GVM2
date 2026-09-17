#ifndef GVM_THREE_WEBGL_LOADER_TEXTURE_TIFF_HPP
#define GVM_THREE_WEBGL_LOADER_TEXTURE_TIFF_HPP

#include "UGL.h"
#include "Dsl/TexturedBoxCommon/TexturedBoxCommon.hpp"

using namespace UGL;

static const uint WebglLoaderTextureTiffMaxTextures = 8u;

/** Defines the unique Scene RenderSet used by webgl_loader_texture_tiff. */
struct WebglLoaderTextureTiffSceneRenderSet : public IRenderSet
{
    /** Declares the three plane entities and their decoded TIFF textures. */
    constructor(BufferComponent<TexturedBoxVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<TexturedBoxObjectData> objects,
                BufferComponent<TexturedBoxInstanceData> instances,
                BufferComponent<TexturedBoxMaterialData> materials,
                (TextureComponent<half4, WebglLoaderTextureTiffMaxTextures> textures))
    {
    }
};

/** Binds the sampler shared by the three TIFF entities. */
struct WebglLoaderTextureTiffSamplerBindGroup final : public IBindGroup
{
    /** Declares Three's linear mipmapped clamp sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Draws all TIFF planes through one RenderSet indexed-indirect submission. */
class WebglLoaderTextureTiffOpaquePass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and shared sampler. */
    constructor(RenderSet<WebglLoaderTextureTiffSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglLoaderTextureTiffSamplerBindGroup> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the current entity transform through RenderSet component reads. */
    TexturedBoxVertexOutput vertex(
        TexturedBoxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const TexturedBoxObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const TexturedBoxInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        TexturedBoxVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, inputValue.position);
        outputValue.texCoord = inputValue.texCoord.xy;
        outputValue.entityAndMaterial = uint2(renderEntityID, objectData.materialAndFlags.x);
        outputValue.instanceTint = instanceData.tint;
        return outputValue;
    }

    /** Samples one entity-local decoded TIFF and applies Three's output transfer. */
    TexturedBoxFrameBuffer fragment(TexturedBoxVertexOutput inputValue)
    {
        const TexturedBoxMaterialData materialData = sceneSet->materials->get(
            inputValue.entityAndMaterial.x,
            inputValue.entityAndMaterial.y);
        auto tiffTexture = sceneSet->textures->get(inputValue.entityAndMaterial.x, 0u);
        const float2 sampleCoord = float2(inputValue.texCoord.x, 1.0f - inputValue.texCoord.y);
        const float4 linearColor = float4(tiffTexture->sample(
            samplerResources->textureSampler,
            sampleCoord)) * materialData.baseColor * inputValue.instanceTint;
        TexturedBoxFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            texturedBoxLinearToSrgb(linearColor.x),
            texturedBoxLinearToSrgb(linearColor.y),
            texturedBoxLinearToSrgb(linearColor.z),
            linearColor.w);
        return frameBuffer;
    }
};

/** Owns the dedicated TIFF Scene RenderSet, pass, and single-sample targets. */
class WebglLoaderTextureTiffRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderTextureTiffSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderTextureTiffOpaquePass> opaquePass;
    BindGroup<WebglLoaderTextureTiffSamplerBindGroup> samplerResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler textureSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the dedicated RenderSet and linear mipmapped sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderTextureTiffSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderTextureTiffSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglLoaderTextureTiffSamplerBindGroup>(
            textureSampler);
        opaquePass = device->createRenderClass<WebglLoaderTextureTiffOpaquePass>(
            sceneSet,
            samplerResources);
    }

    /** Allocates explicit single-sample RGBA8 and depth outputs. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglLoaderTextureTiffOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglLoaderTextureTiffDepth", width, height, 1u);
    }

    /** Draws all entities with the RenderSet-only indexed-indirect entry point. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        TexturedBoxFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("WebglLoaderTextureTiffOpaque", frameBuffer, opaquePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 output used by deterministic readback. */
    auto getReadbackTextureHandle() const
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

    /** Releases the unique RenderSet and explicit output textures. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
