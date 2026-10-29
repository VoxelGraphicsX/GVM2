#ifndef GVM_THREE_WEBGL_MATERIALS_TEXTURE_CANVAS_HPP
#define GVM_THREE_WEBGL_MATERIALS_TEXTURE_CANVAS_HPP

#include "UGL.h"
#include "Dsl/TexturedBoxCommon/TexturedBoxCommon.hpp"

using namespace UGL;

static const uint WebglMaterialsTextureCanvasExtent = 128u;
static const uint WebglMaterialsTextureCanvasMaxTextures = 8u;

/** Stores the base color and four pointer-derived Canvas2D hairline segments for one material. */
struct WebglMaterialsTextureCanvasMaterialData
{
    float4 baseColor;
    float4 segment0;
    float4 segment1;
    float4 segment2;
    float4 segment3;
    uint4 state;
};

/** Defines the only Scene RenderSet used by webgl_materials_texture_canvas. */
struct WebglMaterialsTextureCanvasSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, material, and texture components. */
    constructor(BufferComponent<TexturedBoxVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<TexturedBoxObjectData> objects,
                BufferComponent<TexturedBoxInstanceData> instances,
                BufferComponent<WebglMaterialsTextureCanvasMaterialData> materials,
                (TextureComponent<half4, WebglMaterialsTextureCanvasMaxTextures> textures))
    {
    }
};

/** Binds the sampler used with the entity-owned CanvasTexture base texel. */
struct WebglMaterialsTextureCanvasSamplerBindGroup final : public IBindGroup
{
    /** Declares an immutable clamp-to-edge sampler without a standalone texture binding. */
    constructor(Sampler canvasSampler [[Binding0]])
    {
    }
};

/** Calculates one deterministic one-pixel Canvas2D hairline coverage value. */
inline float webglMaterialsTextureCanvasHairlineCoverage(
    float2 pixelCenter,
    float4 segment,
    float coverageWidth)
{
    const float2 start = segment.xy;
    const float2 end = segment.zw;
    const float2 direction = end - start;
    if (abs(direction.y) >= abs(direction.x))
    {
        if (abs(direction.y) <= 0.000001f)
        {
            return 0.0f;
        }
        const float parameter = (pixelCenter.y - start.y) / direction.y;
        if (parameter < 0.0f || parameter > 1.0f)
        {
            return 0.0f;
        }
        const float lineX = start.x + parameter * direction.x;
        return clamp(1.0f - abs(pixelCenter.x - lineX) / coverageWidth, 0.0f, 1.0f);
    }

    if (abs(direction.x) <= 0.000001f)
    {
        return 0.0f;
    }
    const float parameter = (pixelCenter.x - start.x) / direction.x;
    if (parameter < 0.0f || parameter > 1.0f)
    {
        return 0.0f;
    }
    const float lineY = start.y + parameter * direction.y;
    return clamp(1.0f - abs(pixelCenter.y - lineY) / coverageWidth, 0.0f, 1.0f);
}


/** Replays all active hairline segments and reproduces one quantized RGBA8 canvas texel. */
inline float webglMaterialsTextureCanvasTexelIntensity(
    float2 pixelCenter,
    WebglMaterialsTextureCanvasMaterialData materialData)
{
    const uint activeSegmentCount = materialData.state.x;
    float intensity = 1.0f;
    if (activeSegmentCount >= 1u)
    {
        const float firstCoverage = webglMaterialsTextureCanvasHairlineCoverage(
            pixelCenter,
            materialData.segment0,
            1.3f);
        intensity *= 1.0f - firstCoverage;
    }
    if (activeSegmentCount >= 2u)
    {
        intensity *= 1.0f - webglMaterialsTextureCanvasHairlineCoverage(
            pixelCenter,
            materialData.segment1,
            0.85f);
    }
    if (activeSegmentCount >= 3u)
    {
        intensity *= 1.0f - webglMaterialsTextureCanvasHairlineCoverage(
            pixelCenter,
            materialData.segment2,
            1.25f);
    }
    if (activeSegmentCount >= 4u)
    {
        intensity *= 1.0f - webglMaterialsTextureCanvasHairlineCoverage(
            pixelCenter,
            materialData.segment3,
            1.0f);
    }
    return floor(intensity * 255.0f + 0.5f) / 255.0f;
}

/** Reconstructs clamp-to-edge RGBA8 bilinear sampling of the semantic 128x128 canvas. */
inline float webglMaterialsTextureCanvasSample(
    float2 texCoord,
    WebglMaterialsTextureCanvasMaterialData materialData)
{




    const float2 texelPosition =
        texCoord * float(WebglMaterialsTextureCanvasExtent) - float2(0.5f, 0.5f);
    const float2 lowTexel = floor(texelPosition);
    const float2 highTexel = lowTexel + float2(1.0f, 1.0f);
    const float maximumTexel = float(WebglMaterialsTextureCanvasExtent - 1u);
    const float2 clampedLow = clamp(lowTexel, float2(0.0f), float2(maximumTexel));
    const float2 clampedHigh = clamp(highTexel, float2(0.0f), float2(maximumTexel));
    const float2 interpolation = frac(texelPosition);

    const float lowLow = webglMaterialsTextureCanvasTexelIntensity(
        clampedLow + float2(0.5f, 0.5f),
        materialData);
    const float highLow = webglMaterialsTextureCanvasTexelIntensity(
        float2(clampedHigh.x + 0.5f, clampedLow.y + 0.5f),
        materialData);
    const float lowHigh = webglMaterialsTextureCanvasTexelIntensity(
        float2(clampedLow.x + 0.5f, clampedHigh.y + 0.5f),
        materialData);
    const float highHigh = webglMaterialsTextureCanvasTexelIntensity(
        clampedHigh + float2(0.5f, 0.5f),
        materialData);
    const float lowRow = lerp(lowLow, highLow, interpolation.x);
    const float highRow = lerp(lowHigh, highHigh, interpolation.x);
    return lerp(lowRow, highRow, interpolation.y);
}

/** Draws the CanvasTexture-backed BoxGeometry entity through the Scene RenderSet. */
class WebglMaterialsTextureCanvasScenePass final : public IRenderClass
{
public:
    /** Binds the Scene's unique RenderSet and its shared texture sampler. */
    constructor(RenderSet<WebglMaterialsTextureCanvasSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsTextureCanvasSamplerBindGroup> samplerResources [[Slot1]])
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

    /** Reconstructs CanvasTexture sampling in DSL and applies Three's sRGB output transfer. */
    TexturedBoxFrameBuffer fragment(TexturedBoxVertexOutput inputValue)
    {
        const WebglMaterialsTextureCanvasMaterialData materialData =
            sceneSet->materials->get(
                inputValue.entityAndMaterial.x,
                inputValue.entityAndMaterial.y);
        auto baseTexture = sceneSet->textures->get(inputValue.entityAndMaterial.x, 0u);
        const float4 baseColor = float4(baseTexture->sample(
            samplerResources->canvasSampler,
            inputValue.texCoord));
        const float intensity = webglMaterialsTextureCanvasSample(
            inputValue.texCoord,
            materialData);
        const float4 linearColor = baseColor * materialData.baseColor *
            inputValue.instanceTint * float4(intensity, intensity, intensity, 1.0f);

        TexturedBoxFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            texturedBoxLinearToSrgb(linearColor.x),
            texturedBoxLinearToSrgb(linearColor.y),
            texturedBoxLinearToSrgb(linearColor.z),
            linearColor.w);
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and DSL passes for Three r185 CanvasTexture. */
class WebglMaterialsTextureCanvasRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsTextureCanvasSceneRenderSet> sceneSet;
    RenderClass<WebglMaterialsTextureCanvasScenePass> scenePass;
    BindGroup<WebglMaterialsTextureCanvasSamplerBindGroup> samplerResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Sampler canvasSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the Scene RenderSet, samplers, and RenderSet-backed Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);

        sceneSet = device->createRenderSet<WebglMaterialsTextureCanvasSceneRenderSet>();
        canvasSampler = device->createSampler({
            .label = "WebglMaterialsTextureCanvasSampler",
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
        samplerResources =
            device->createBindGroup<WebglMaterialsTextureCanvasSamplerBindGroup>(canvasSampler);
        scenePass = device->createRenderClass<WebglMaterialsTextureCanvasScenePass>(
            sceneSet,
            samplerResources);
    }

    /** Allocates the explicit host-sized single-sample color and depth resources. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglMaterialsTextureCanvasOutputRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglMaterialsTextureCanvasDepth32", width, height, 1u);
    }

    /** Draws the Scene through RenderSet indirect metadata into the single-sample output. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        TexturedBoxFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputTexture->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrameBuffer.depth = depthTexture->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglMaterialsTextureCanvasScene",
                sceneFrameBuffer,
                scenePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output for deterministic test readback. */
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

    /** Releases the Scene RenderSet and every explicitly created output texture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
