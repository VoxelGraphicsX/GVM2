#ifndef GVM_THREE_PHASE1_WEBGPU_TEXTURES_2D_ARRAY_COMPRESSED_SIMPLE_HPP
#define GVM_THREE_PHASE1_WEBGPU_TEXTURES_2D_ARRAY_COMPRESSED_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the deterministic WebGPU compressed-array layer. */
struct WebgpuTextures2DArrayCompressedUniforms
{
    float4 layerAndViewport;
};

/** Binds the CPU-decoded r185 KTX2 array and its original linear sampler. */
struct WebgpuTextures2DArrayCompressedResources final : public IBindGroup
{
    /** Declares the complete resource layout for the WebGPU compressed-array example. */
    constructor(
        UniformBuffer<WebgpuTextures2DArrayCompressedUniforms> uniforms [[Binding0]],
        Texture2DArray<float4> movieFrames [[Binding1]],
        Sampler movieSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the WebGPU array-layer shader. */
struct WebgpuTextures2DArrayCompressedVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic WebGPU RGBA8 attachment. */
struct WebgpuTextures2DArrayCompressedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Reproduces the WebGPU NodeMaterial plane, flipped UV, and layer depth. */
class WebgpuTextures2DArrayCompressedMainPass final : public IRenderClass
{
public:
    /** Binds the decoded array and disables depth and face culling. */
    constructor(
        BindGroup<WebgpuTextures2DArrayCompressedResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle while preserving the projected plane bounds. */
    WebgpuTextures2DArrayCompressedVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTextures2DArrayCompressedVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples the sRGB movie layer after the exact TSL UV flip. */
    WebgpuTextures2DArrayCompressedFrameBuffer fragment(
        WebgpuTextures2DArrayCompressedVertexOutput inputValue)
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
        const float3 encoded = inside
            ? resources->movieFrames
                  ->sampleLevel(
                      resources->movieSampler,
                      uv,
                      layer,
                      0.0f)
                  .xyz
            : float3(0.0f);
        WebgpuTextures2DArrayCompressedFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGPU compressed-array renderer and decoded layers. */
class Phase1WebgpuTextures2DArrayCompressedSimpleRenderer final
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
    Buffer<WebgpuTextures2DArrayCompressedUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler movieSampler;
    BindGroup<WebgpuTextures2DArrayCompressedResources> resources;
    RenderClass<WebgpuTextures2DArrayCompressedMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the exact linear sampler and deterministic uniform buffer. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer(
            "WebgpuTextures2DArrayCompressedUniforms",
            1u);
        movieSampler = device->createSampler({
            .label = "WebgpuTextures2DArrayCompressedSampler",
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
            "WebgpuTextures2DArrayCompressedOutput",
            width,
            height,
            1u);
    }

    /** Uploads all six CPU-decoded KTX2 layers through generated DSL APIs. */
    void configureMovieFrames(const eastl::vector<uint8_t> &rgba)
    {
        const uint32_t FrameWidth = 496u;
        const uint32_t FrameHeight = 260u;
        const uint32_t FrameCount = 6u;
        const uint64_t FrameByteCount =
            uint64_t(FrameWidth) * FrameHeight * 4u;
        movieFrames = device->createTexture(
            "WebgpuTextures2DArrayCompressedFrames",
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
            device->createBindGroup<
                WebgpuTextures2DArrayCompressedResources>(
                uniformBuffer,
                movieFrames->createView(),
                movieSampler);
        mainPass =
            device->createRenderClass<
                WebgpuTextures2DArrayCompressedMainPass>(resources);
        graphicsQueue->submit();
    }

    /** Advances the fixed 60 Hz depth step and renders the selected movie frame. */
    void render() override
    {
        const float layer = 1.0f + float(frameIndex) / 6.0f;
        const WebgpuTextures2DArrayCompressedUniforms uniforms = {
            float4(layer, float(width), float(height), 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebgpuTextures2DArrayCompressedFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTextures2DArrayCompressedMain",
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
