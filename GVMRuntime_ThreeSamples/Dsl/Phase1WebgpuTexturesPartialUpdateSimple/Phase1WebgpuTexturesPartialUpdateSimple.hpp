#ifndef GVM_THREE_PHASE1_WEBGPU_TEXTURES_PARTIAL_UPDATE_SIMPLE_HPP
#define GVM_THREE_PHASE1_WEBGPU_TEXTURES_PARTIAL_UPDATE_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the writable carbon texture and deterministic first patch state. */
struct WebgpuTexturesPartialUpdateComputeResources final : public IBindGroup
{
    /** Declares the only texture mutated by the partial-update compute pass. */
    constructor(
        RWTexture2D<TextureFormat::RGBA8Unorm> carbonTexture [[Binding0]])
    {
    }
};

/** Binds the updated carbon texture to the ordinary plane material. */
struct WebgpuTexturesPartialUpdateRenderResources final : public IBindGroup
{
    /** Declares the sampled texture and its exact non-mipmapped linear sampler. */
    constructor(
        Texture2D<float4> carbonTexture [[Binding0]],
        Sampler carbonSampler [[Binding1]])
    {
    }
};

/** Carries fullscreen coordinates into the dedicated carbon-plane shader. */
struct WebgpuTexturesPartialUpdateVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the deterministic RGBA8 output attachment. */
struct WebgpuTexturesPartialUpdateFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Writes the deterministic first 32x32 r185 subregion into the base texture. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebgpuTexturesPartialUpdateComputePass final : public IComputeClass
{
public:
    /** Binds the existing writable texture without adding a copy API. */
    constructor(
        BindGroup<WebgpuTexturesPartialUpdateComputeResources>
            computeResources [[Slot0]])
    {
    }

private:
    /** Writes one texel of the exact first deterministic color patch. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= 32u || threadID.y >= 32u)
        {
            return;
        }
        const uint2 destination =
            uint2(480u + threadID.x, threadID.y);
        computeResources->carbonTexture->write(
            destination,
            half4(
                half(137.0f / 255.0f),
                half(2.0f / 255.0f),
                half(129.0f / 255.0f),
                half(1.0f / 255.0f)));
    }
};

/** Draws the one ordinary PlaneGeometry with the updated carbon texture. */
class WebgpuTexturesPartialUpdateMainPass final : public IRenderClass
{
public:
    /** Binds the single material and disables culling for the screen-facing plane. */
    constructor(
        BindGroup<WebgpuTexturesPartialUpdateRenderResources>
            renderResources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle used to analytically rasterize the plane. */
    WebgpuTexturesPartialUpdateVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTexturesPartialUpdateVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples the r185 plane with no generated mip chain or CPU rasterization. */
    WebgpuTexturesPartialUpdateFrameBuffer fragment(
        WebgpuTexturesPartialUpdateVertexOutput inputValue)
    {
        const float2 ndc = inputValue.screenUv * 2.0f - 1.0f;
        const float2 halfExtent =
            float2(0.4462960662f, 0.7140737059f);
        const float inside =
            abs(ndc.x) <= halfExtent.x &&
                    abs(ndc.y) <= halfExtent.y
                ? 1.0f
                : 0.0f;
        float2 uv =
            ndc / (halfExtent * 2.0f) + 0.5f;
        uv.y = 1.0f - uv.y;
        const float4 sampled =
            renderResources->carbonTexture->sampleLevel(
                renderResources->carbonSampler,
                uv,
                0.0f);
        WebgpuTexturesPartialUpdateFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(sampled.xyz * inside), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated update compute pass, ordinary plane, and readback target. */
class Phase1WebgpuTexturesPartialUpdateSimpleRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, TextureBinding, CopyDst>,
            TextureDimension::e2D> carbonTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler carbonSampler;
    BindGroup<WebgpuTexturesPartialUpdateComputeResources>
        computeResources;
    BindGroup<WebgpuTexturesPartialUpdateRenderResources>
        renderResources;
    ComputeClass<WebgpuTexturesPartialUpdateComputePass> updatePass;
    RenderClass<WebgpuTexturesPartialUpdateMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the base texture and exact no-mipmap linear sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        carbonTexture = device->createTexture(
            "WebgpuTexturesPartialUpdateCarbon",
            512u,
            512u,
            1u);
        carbonSampler = device->createSampler({
            .label = "WebgpuTexturesPartialUpdateSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        computeResources = device->createBindGroup<
            WebgpuTexturesPartialUpdateComputeResources>(
                carbonTexture->createView());
        renderResources = device->createBindGroup<
            WebgpuTexturesPartialUpdateRenderResources>(
                carbonTexture->createView(),
                carbonSampler);
        updatePass = device->createComputeClass<
            WebgpuTexturesPartialUpdateComputePass>(
                computeResources);
        mainPass = device->createRenderClass<
            WebgpuTexturesPartialUpdateMainPass>(
                renderResources);
    }

    /** Allocates the final RGBA8 readback texture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuTexturesPartialUpdateOutput",
            width,
            height,
            1u);
    }

    /** Uploads the locked 512x512 carbon texture without generating mips. */
    void configureCarbonTexture(
        const eastl::vector<uint8_t> &pixels)
    {
        graphicsQueue
            ->writeTexture(
                carbonTexture,
                pixels.data(),
                uint64_t(pixels.size()))
            ->submit();
    }

    /** Applies the first update at frame seven and renders the ordinary plane. */
    void render() override
    {
        WebgpuTexturesPartialUpdateFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        if (frameIndex == 7u)
        {
            graphicsQueue->computePass(
                "WebgpuTexturesPartialUpdateCompute",
                updatePass(32u, 32u, 1u));
        }
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTexturesPartialUpdateMain",
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

    /** Releases the dedicated base and output textures. */
    void destroy() override
    {
        device->freeTexture(carbonTexture);
        device->freeTexture(outputColor);
    }
};

#endif
