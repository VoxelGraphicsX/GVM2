#ifndef GVM_THREE_PHASE1_WEBGL_TEXTURE_2D_ARRAY_SIMPLE_HPP
#define GVM_THREE_PHASE1_WEBGL_TEXTURE_2D_ARRAY_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the selected head-volume layer and canonical output extent. */
struct WebglTexture2DArrayUniforms
{
    float4 layerAndViewport;
};

/** Binds the pinned head-volume array and its deterministic sampling state. */
struct WebglTexture2DArrayResources final : public IBindGroup
{
    /** Declares the complete resource layout for the WebGL texture-array example. */
    constructor(
        UniformBuffer<WebglTexture2DArrayUniforms> uniforms [[Binding0]],
        Texture2DArray<float4> volume [[Binding1]],
        Sampler volumeSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the head-slice fragment shader. */
struct WebglTexture2DArrayVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic RGBA8 output attachment. */
struct WebglTexture2DArrayFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Reproduces the r185 WebGL plane projection and selected array-layer sample. */
class WebglTexture2DArrayMainPass final : public IRenderClass
{
public:
    /** Binds the texture array and disables depth and face culling. */
    constructor(BindGroup<WebglTexture2DArrayResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the deterministic head-slice image. */
    WebglTexture2DArrayVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglTexture2DArrayVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples the exact RedFormat layer and applies ShaderMaterial output encoding. */
    WebglTexture2DArrayFrameBuffer fragment(
        WebglTexture2DArrayVertexOutput inputValue)
    {
        const float2 ndc = inputValue.screenUv * 2.0f - 1.0f;
        const float2 halfExtent =
            float2(0.5389809600f, 0.8623695360f);
        const float inside =
            abs(ndc.x) <= halfExtent.x &&
                    abs(ndc.y) <= halfExtent.y
                ? 1.0f
                : 0.0f;
        const float2 uv =
            ndc / (halfExtent * 2.0f) + 0.5f;
        const uint layer = uint(
            clamp(
                floor(
                    resources->uniforms->layerAndViewport.x +
                    0.5f),
                0.0f,
                108.0f));
        const float red =
            resources->volume
                ->sampleLevel(
                    resources->volumeSampler,
                    uv,
                    layer,
                    0.0f)
                .x;
        const float encoded = red * 1.5f * inside;
        WebglTexture2DArrayFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGL texture-array pass and immutable volume texture. */
class Phase1WebglTexture2DArraySimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::R8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> volume;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebglTexture2DArrayUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler volumeSampler;
    BindGroup<WebglTexture2DArrayResources> resources;
    RenderClass<WebglTexture2DArrayMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the exact linear sampler and one-scenario uniform buffer. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer =
            device->createBuffer("WebglTexture2DArrayUniforms", 1u);
        volumeSampler = device->createSampler({
            .label = "WebglTexture2DArraySampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the final RGBA8 readback texture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglTexture2DArrayOutput",
            width,
            height,
            1u);
    }

    /** Uploads all 109 pinned R8 array layers without CPU rasterization. */
    void configureVolume(const eastl::vector<uint8_t> &voxels)
    {
        volume = device->createTexture(
            "WebglTexture2DArrayVolume",
            256u,
            256u,
            1u,
            1u,
            109u);
        const uint64_t layerByteCount = 256u * 256u;
        for (uint layer = 0u; layer < 109u; ++layer)
        {
            graphicsQueue->writeTexture(
                volume,
                voxels.data() + uint64_t(layer) * layerByteCount,
                layerByteCount,
                0u,
                layer);
        }
        resources =
            device->createBindGroup<WebglTexture2DArrayResources>(
                uniformBuffer,
                volume->createView(),
                volumeSampler);
        mainPass =
            device->createRenderClass<WebglTexture2DArrayMainPass>(
                resources);
        graphicsQueue->submit();
    }

    /** Advances the upstream depth oscillator and renders one head slice. */
    void render() override
    {
        const float layer = 55.0f +
            0.4f * float(frameIndex);
        const WebglTexture2DArrayUniforms uniforms = {
            float4(layer, float(width), float(height), 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebglTexture2DArrayFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglTexture2DArrayMain",
                frameBuffer,
                mainPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        frameIndex += 1u;
    }

    /** Returns the final DSL-owned readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the dedicated volume, output, and uniform resources. */
    void destroy() override
    {
        device->freeTexture(volume);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
