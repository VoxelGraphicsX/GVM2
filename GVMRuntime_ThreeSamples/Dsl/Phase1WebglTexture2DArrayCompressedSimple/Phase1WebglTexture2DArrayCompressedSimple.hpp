#ifndef GVM_THREE_PHASE1_WEBGL_TEXTURE_2D_ARRAY_COMPRESSED_SIMPLE_HPP
#define GVM_THREE_PHASE1_WEBGL_TEXTURE_2D_ARRAY_COMPRESSED_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the deterministic Spirited Away array layer selected by the fixed clock. */
struct WebglTexture2DArrayCompressedUniforms
{
    float4 layerAndViewport;
};

/** Binds the CPU-decoded r185 KTX2 array and its original linear sampler. */
struct WebglTexture2DArrayCompressedResources final : public IBindGroup
{
    /** Declares the complete resource layout for the WebGL compressed-array example. */
    constructor(
        UniformBuffer<WebglTexture2DArrayCompressedUniforms> uniforms [[Binding0]],
        Texture2DArray<float4> movieFrames [[Binding1]],
        Sampler movieSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the dedicated array-layer fragment shader. */
struct WebglTexture2DArrayCompressedVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic RGBA8 attachment. */
struct WebglTexture2DArrayCompressedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Decodes one sRGB KTX2 sample channel with the r185 transfer constants. */
float webglTexture2DArrayCompressedSrgbToLinear(float value)
{
    if (value <= 0.04045f) return value * 0.0773993808f;
    return pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
}

/** Reproduces the WebGL plane projection, layer selection, and +0.2 shader lift. */
class WebglTexture2DArrayCompressedMainPass final : public IRenderClass
{
public:
    /** Binds the decoded array and disables depth and face culling. */
    constructor(
        BindGroup<WebglTexture2DArrayCompressedResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle while preserving the exact projected plane bounds. */
    WebglTexture2DArrayCompressedVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglTexture2DArrayCompressedVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples one UASTC-decoded movie frame and applies ShaderMaterial output encoding. */
    WebglTexture2DArrayCompressedFrameBuffer fragment(
        WebglTexture2DArrayCompressedVertexOutput inputValue)
    {
        const float2 ndc = inputValue.screenUv * 2.0f - 1.0f;
        const float2 halfExtent =
            float2(0.5389809600f, 0.4311847680f);
        const bool inside =
            abs(ndc.x) <= halfExtent.x &&
            abs(ndc.y) <= halfExtent.y;
        const float2 uv =
            float2(
                ndc.x / (halfExtent.x * 2.0f) + 0.5f,
                0.5f + ndc.y / (halfExtent.y * 2.0f));
        const uint layer = uint(
            clamp(
                floor(resources->uniforms->layerAndViewport.x),
                0.0f,
                5.0f));
        const float3 sampledColor =
            resources->movieFrames
                ->sampleLevel(
                    resources->movieSampler,
                    uv,
                    layer,
                    0.0f)
                .xyz;
        const float3 encoded = inside
            ? float3(
                  webglTexture2DArrayCompressedSrgbToLinear(
                      sampledColor.x) + 0.2f,
                  webglTexture2DArrayCompressedSrgbToLinear(
                      sampledColor.y) + 0.2f,
                  webglTexture2DArrayCompressedSrgbToLinear(
                      sampledColor.z) + 0.2f)
            : float3(0.0f);
        WebglTexture2DArrayCompressedFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGL compressed-array renderer and decoded texture layers. */
class Phase1WebglTexture2DArrayCompressedSimpleRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> movieFrames;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebglTexture2DArrayCompressedUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler movieSampler;
    BindGroup<WebglTexture2DArrayCompressedResources> resources;
    RenderClass<WebglTexture2DArrayCompressedMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the exact linear array sampler and deterministic uniform buffer. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer(
            "WebglTexture2DArrayCompressedUniforms",
            1u);
        movieSampler = device->createSampler({
            .label = "WebglTexture2DArrayCompressedSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the final host-sized RGBA8 texture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglTexture2DArrayCompressedOutput",
            width,
            height,
            1u);
    }

    /** Uploads all six CPU-decoded KTX2 layers without a GPU API bypass. */
    void configureMovieFrames(const eastl::vector<uint8_t> &rgba)
    {
        const uint32_t FrameWidth = 496u;
        const uint32_t FrameHeight = 260u;
        const uint32_t FrameCount = 6u;
        const uint64_t FrameByteCount =
            uint64_t(FrameWidth) * FrameHeight * 4u;
        movieFrames = device->createTexture(
            "WebglTexture2DArrayCompressedFrames",
            FrameWidth,
            FrameHeight,
            1u,
            1u,
            FrameCount);
        for (uint32_t layer = 0u; layer < FrameCount; ++layer)
        {
            graphicsQueue->writeTexture(
                movieFrames,
                rgba.data() + uint64_t(layer) * FrameByteCount,
                FrameByteCount,
                0u,
                layer);
        }
        resources =
            device->createBindGroup<WebglTexture2DArrayCompressedResources>(
                uniformBuffer,
                movieFrames->createView(),
                movieSampler);
        mainPass =
            device->createRenderClass<
                WebglTexture2DArrayCompressedMainPass>(resources);
        graphicsQueue->submit();
    }

    /** Advances the fixed 60 Hz depth step and renders the selected movie frame. */
    void render() override
    {
        const uint32_t elapsedTicks =
            frameIndex == 0u ? 0u : frameIndex - 1u;
        const float layer =
            1.0f + float(elapsedTicks) / 6.0f;
        const WebglTexture2DArrayCompressedUniforms uniforms = {
            float4(layer, float(width), float(height), 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebglTexture2DArrayCompressedFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglTexture2DArrayCompressedMain",
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

    /** Returns the DSL-owned final readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases all dedicated compressed-array resources. */
    void destroy() override
    {
        device->freeTexture(movieFrames);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
