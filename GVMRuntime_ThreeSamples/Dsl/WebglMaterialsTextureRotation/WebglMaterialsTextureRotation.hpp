#ifndef GVM_THREE_WEBGL_MATERIALS_TEXTURE_ROTATION_HPP
#define GVM_THREE_WEBGL_MATERIALS_TEXTURE_ROTATION_HPP

#include "UGL.h"
#include "Dsl/TexturedBoxCommon/TexturedBoxCommon.hpp"

using namespace UGL;

static const uint WebglMaterialsTextureRotationMaxTextures = 8u;

/** Stores MeshBasicMaterial color and the exact Three texture-transform parameters. */
struct WebglMaterialsTextureRotationMaterialData
{
    float4 baseColor;
    float4 offsetAndRepeat;
    float4 rotationAndCenter;
};

/** Defines the only Scene RenderSet used by webgl_materials_texture_rotation. */
struct WebglMaterialsTextureRotationSceneRenderSet : public IRenderSet
{
    /** Declares the consolidated box geometry, entity data, material, and texture pool. */
    constructor(BufferComponent<TexturedBoxVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<TexturedBoxObjectData> objects,
                BufferComponent<TexturedBoxInstanceData> instances,
                BufferComponent<WebglMaterialsTextureRotationMaterialData> materials,
                (TextureComponent<half4, WebglMaterialsTextureRotationMaxTextures> textures))
    {
    }
};

/** Binds the anisotropic Repeat sampler used by the Scene TextureComponent. */
struct WebglMaterialsTextureRotationSamplerBindGroup final : public IBindGroup
{
    /** Declares one sampler without introducing a standalone texture binding. */
    constructor(Sampler uvGridSampler [[Binding0]])
    {
    }
};

/** Applies Matrix3.setUvTransform using Three r185's exact coefficient ordering. */
float2 webglMaterialsTextureRotationUv(float2 uv, float4 offsetAndRepeat, float4 rotationAndCenter)
{
    const float translationX = offsetAndRepeat.x;
    const float translationY = offsetAndRepeat.y;
    const float scaleX = offsetAndRepeat.z;
    const float scaleY = offsetAndRepeat.w;
    const float rotation = rotationAndCenter.x;
    const float centerX = rotationAndCenter.y;
    const float centerY = rotationAndCenter.z;
    const float cosine = cos(rotation);
    const float sine = sin(rotation);
    const float row0X = scaleX * cosine;
    const float row0Y = scaleX * sine;
    const float row0Z = -scaleX * (cosine * centerX + sine * centerY) + centerX + translationX;
    const float row1X = -scaleY * sine;
    const float row1Y = scaleY * cosine;
    const float row1Z = -scaleY * (-sine * centerX + cosine * centerY) + centerY + translationY;
    return float2(
        row0X * uv.x + row0Y * uv.y + row0Z,
        row1X * uv.x + row1Y * uv.y + row1Z);
}

/** Draws the texture-rotation BoxGeometry through the Scene RenderSet indirect path. */
class WebglMaterialsTextureRotationMainPass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and immutable anisotropic Repeat sampler. */
    constructor(RenderSet<WebglMaterialsTextureRotationSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsTextureRotationSamplerBindGroup> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves transform, material, and instance components through entity builtins. */
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

    /** Applies the DSL UV matrix, samples the entity texture, and emits Three sRGB output. */
    TexturedBoxFrameBuffer fragment(TexturedBoxVertexOutput inputValue)
    {
        const WebglMaterialsTextureRotationMaterialData materialData =
            sceneSet->materials->get(
                inputValue.entityAndMaterial.x,
                inputValue.entityAndMaterial.y);
        auto uvGridTexture = sceneSet->textures->get(inputValue.entityAndMaterial.x, 0u);
        const float2 transformedUv = webglMaterialsTextureRotationUv(
            inputValue.texCoord,
            materialData.offsetAndRepeat,
            materialData.rotationAndCenter);
        const float2 sampleCoord = transformedUv;
        const float4 linearColor = float4(
            uvGridTexture->sample(samplerResources->uvGridSampler, sampleCoord)) *
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

/** Owns the unique Scene RenderSet and all GPU work for the texture-rotation case. */
class WebglMaterialsTextureRotationRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsTextureRotationSceneRenderSet> sceneSet;
    RenderClass<WebglMaterialsTextureRotationMainPass> scenePass;
    BindGroup<WebglMaterialsTextureRotationSamplerBindGroup> samplerResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Sampler uvGridSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the Scene RenderSet, anisotropic Repeat sampler, and Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);

        sceneSet = device->createRenderSet<WebglMaterialsTextureRotationSceneRenderSet>();
        uvGridSampler = device->createSampler({
            .label = "WebglMaterialsTextureRotationUvGridSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 10,
            .maxAnisotropy = 16,
        });
        samplerResources =
            device->createBindGroup<WebglMaterialsTextureRotationSamplerBindGroup>(uvGridSampler);
        scenePass = device->createRenderClass<WebglMaterialsTextureRotationMainPass>(
            sceneSet,
            samplerResources);
    }

    /** Allocates the fixed deterministic RGBA8 and native-depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglMaterialsTextureRotationOutputRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglMaterialsTextureRotationDepth32",
            width,
            height,
            1u);
    }

    /** Updates and draws the Scene through one parameterless RenderSet indirect call. */
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
            ->renderPass("WebglMaterialsTextureRotationScene", frameBuffer, scenePass())
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
