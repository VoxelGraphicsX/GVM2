#ifndef GVM_THREE_PHASE1_WEBGPU_TEXTURES_2D_ARRAY_SIMPLE_HPP
#define GVM_THREE_PHASE1_WEBGPU_TEXTURES_2D_ARRAY_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the r185 oscillator-selected volume layer. */
struct WebgpuTextures2DArrayUniforms
{
    float4 layerAndViewport;
};

/** Binds the pinned R8 volume and its deterministic linear sampler. */
struct WebgpuTextures2DArrayResources final : public IBindGroup
{
    /** Declares the complete WebGPU texture-array resource layout. */
    constructor(
        UniformBuffer<WebgpuTextures2DArrayUniforms> uniforms [[Binding0]],
        Texture2DArray<float4> volume [[Binding1]],
        Sampler volumeSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the dedicated slice shader. */
struct WebgpuTextures2DArrayVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic RGBA8 output attachment. */
struct WebgpuTextures2DArrayFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear display value with the renderer output transfer. */
float webgpuTextures2DArrayLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces the r185 WebGPU plane projection and TSL red-channel remap. */
class WebgpuTextures2DArrayMainPass final : public IRenderClass
{
public:
    /** Binds the texture array and disables depth and face culling. */
    constructor(BindGroup<WebgpuTextures2DArrayResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the deterministic head-slice image. */
    WebgpuTextures2DArrayVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTextures2DArrayVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples one array layer and applies the exact TSL -0.1 to 1.8 remap. */
    WebgpuTextures2DArrayFrameBuffer fragment(
        WebgpuTextures2DArrayVertexOutput inputValue)
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
                floor(resources->uniforms->layerAndViewport.x),
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
        const float linearValue = red * 1.9f - 0.1f;
        const float encoded =
            webgpuTextures2DArrayLinearToSrgb(linearValue) *
            inside;
        WebgpuTextures2DArrayFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGPU texture-array pass and immutable volume texture. */
class Phase1WebgpuTextures2DArraySimpleRenderer final : public AbstractRenderer
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
    Buffer<WebgpuTextures2DArrayUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler volumeSampler;
    BindGroup<WebgpuTextures2DArrayResources> resources;
    RenderClass<WebgpuTextures2DArrayMainPass> mainPass;
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
            device->createBuffer("WebgpuTextures2DArrayUniforms", 1u);
        volumeSampler = device->createSampler({
            .label = "WebgpuTextures2DArraySampler",
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
            "WebgpuTextures2DArrayOutput",
            width,
            height,
            1u);
    }

    /** Uploads all 109 pinned R8 array layers without CPU rasterization. */
    void configureVolume(const eastl::vector<uint8_t> &voxels)
    {
        volume = device->createTexture(
            "WebgpuTextures2DArrayVolume",
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
            device->createBindGroup<WebgpuTextures2DArrayResources>(
                uniformBuffer,
                volume->createView(),
                volumeSampler);
        mainPass =
            device->createRenderClass<WebgpuTextures2DArrayMainPass>(
                resources);
        graphicsQueue->submit();
    }

    /** Advances the upstream triangle oscillator and renders one head slice. */
    void render() override
    {
        const float timeSeconds =
            float(frameIndex) / 60.0f;
        const float phase =
            timeSeconds * 0.5f + 0.5f;
        const float fraction =
            phase - floor(phase);
        const float oscillator =
            abs(fraction * 2.0f - 1.0f);
        const float layer =
            (oscillator + 1.0f) * 0.5f * 109.0f;
        const WebgpuTextures2DArrayUniforms uniforms = {
            float4(layer, float(width), float(height), 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebgpuTextures2DArrayFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTextures2DArrayMain",
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
