#ifndef GVM_THREE_WEBGL_GEOMETRY_CUBE_HPP
#define GVM_THREE_WEBGL_GEOMETRY_CUBE_HPP

#include "UGL.h"
#include "Dsl/TexturedBoxCommon/TexturedBoxCommon.hpp"

using namespace UGL;

static const uint WebglGeometryCubeMaxTextures = 8u;

/** Defines the only Scene RenderSet used by webgl_geometry_cube. */
struct WebglGeometryCubeSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, material, and texture components. */
    constructor(BufferComponent<TexturedBoxVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<TexturedBoxObjectData> objects,
                BufferComponent<TexturedBoxInstanceData> instances,
                BufferComponent<TexturedBoxMaterialData> materials,
                (TextureComponent<half4, WebglGeometryCubeMaxTextures> textures))
    {
    }
};

/** Binds the Three-compatible sampler used with the entity TextureComponent. */
struct WebglGeometryCubeSamplerBindGroup final : public IBindGroup
{
    /** Declares the immutable crate sampler without introducing a standalone texture binding. */
    constructor(Sampler crateSampler [[Binding0]])
    {
    }
};

/** Draws the textured BoxGeometry entity through the Scene RenderSet indexed-indirect path. */
class WebglGeometryCubeScenePass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and its shared texture sampler. */
    constructor(RenderSet<WebglGeometryCubeSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglGeometryCubeSamplerBindGroup> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves transform, material, and instance data through RenderEntity builtins. */
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

    /** Samples the entity texture and applies Three r185's default sRGB output conversion. */
    TexturedBoxFrameBuffer fragment(TexturedBoxVertexOutput inputValue)
    {
        const TexturedBoxMaterialData materialData = sceneSet->materials->get(
            inputValue.entityAndMaterial.x,
            inputValue.entityAndMaterial.y);
        auto crateTexture = sceneSet->textures->get(inputValue.entityAndMaterial.x, 0u);
        const float2 sampleCoord = float2(inputValue.texCoord.x, 1.0f - inputValue.texCoord.y);
        const float4 linearColor = float4(
            crateTexture->sample(samplerResources->crateSampler, sampleCoord)) *
            materialData.baseColor * inputValue.instanceTint;

        TexturedBoxFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            texturedBoxLinearToSrgb(linearColor.x),
            texturedBoxLinearToSrgb(linearColor.y),
            texturedBoxLinearToSrgb(linearColor.z),
            linearColor.w);
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and DSL pass submission for Three r185 webgl_geometry_cube. */
class WebglGeometryCubeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryCubeSceneRenderSet> sceneSet;
    RenderClass<WebglGeometryCubeScenePass> scenePass;
    BindGroup<WebglGeometryCubeSamplerBindGroup> samplerResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Sampler crateSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the Scene RenderSet, sampler, and RenderSet-backed Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);

        sceneSet = device->createRenderSet<WebglGeometryCubeSceneRenderSet>();
        crateSampler = device->createSampler({
            .label = "WebglGeometryCubeCrateSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 8,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglGeometryCubeSamplerBindGroup>(crateSampler);
        scenePass = device->createRenderClass<WebglGeometryCubeScenePass>(
            sceneSet,
            samplerResources);
    }

    /** Allocates the explicit host-sized RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglGeometryCubeOutputRGBA8", width, height, 1u);
        depthTexture = device->createTexture("WebglGeometryCubeDepth32", width, height, 1u);
    }

    /** Updates and draws the Scene through the RenderSet-only indexed-indirect entry point. */
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
            ->renderPass("WebglGeometryCubeScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output used by deterministic test readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the Scene RenderSet and explicit output textures after capture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
