#ifndef GVM_THREE_WEBGPU_COMPUTE_TEXTURE_HPP
#define GVM_THREE_WEBGPU_COMPUTE_TEXTURE_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputeTextureWidth = 512u;
static const uint WebgpuComputeTextureHeight = 512u;

/** Binds the writable RGBA8 texture populated by the r185 compute algorithm. */
struct WebgpuComputeTextureStorageBindGroup final : public IBindGroup
{
    /** Declares the compute shader's only writable resource. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> storageTexture [[Binding0]])
    {
    }
};

/** Binds the computed texture and its linear sampler to the single plane material. */
struct WebgpuComputeTexturePlaneBindGroup final : public IBindGroup
{
    /** Declares the sampled texture and sampler used by the plane fragment shader. */
    constructor(Texture2D<float4> storageTexture [[Binding0]],
                Sampler storageSampler [[Binding1]])
    {
    }
};

/** Carries the procedural plane position and UV to the fragment stage. */
struct WebgpuComputeTexturePlaneVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the deterministic RGBA8 offscreen output used for capture and presentation. */
struct WebgpuComputeTextureFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Applies the standard sRGB output transfer used by Three's default WebGPU renderer. */
inline float webgpuComputeTextureLinearToSrgb(float linearValue)
{
    const float clampedValue = clamp(linearValue, 0.0f, 1.0f);
    if (clampedValue <= 0.0031308f)
    {
        return clampedValue * 12.92f;
    }
    return 1.055f * pow(clampedValue, 0.4166666666666667f) - 0.055f;
}

/** Reproduces the Three.js r185 storage-texture sine field with one thread per texel. */
class [[LocalWorkGroupSize(8, 8, 1)]] WebgpuComputeTextureComputePass final
    : public IComputeClass
{
public:
    /** Binds the 512 by 512 write-only logical storage texture. */
    constructor(BindGroup<WebgpuComputeTextureStorageBindGroup> storageBindGroup [[Slot0]])
    {
    }

private:
    /** Writes the exact r185 Xst3zN-derived sine expression for one in-range texel. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebgpuComputeTextureWidth ||
            threadID.y >= WebgpuComputeTextureHeight)
        {
            return;
        }

        const float x = float(threadID.x) / 50.0f;
        const float y = float(threadID.y) / 50.0f;

        const float v1 = sin(x);
        const float v2 = sin(y);
        const float v3 = sin(x + y);
        const float v4 = sin(sqrt(x * x + y * y) + 5.0f);
        const float v = v1 + v2 + v3 + v4;

        const float r = sin(v);
        const float g = sin(v + 3.14159265358979323846f);
        const float b = sin(v + 3.14159265358979323846f - 0.5f);
        storageBindGroup->storageTexture->write(
            threadID.xy,
            half4(float4(r, g, b, 1.0f)));
    }
};

/** Samples the computed texture on the example's only renderable plane without a RenderSet. */
class WebgpuComputeTexturePlanePass final : public IRenderClass
{
public:
    /** Binds the computed texture material used by the single plane. */
    constructor(BindGroup<WebgpuComputeTexturePlaneBindGroup> planeBindGroup [[Slot0]])
    {
    }

private:
    /** Generates the r185 PlaneGeometry triangles with backend-corrected sampled-texture UVs. */
    WebgpuComputeTexturePlaneVertexOutput vertex(uint vertexID [[VertexID]])
    {
        float2 position = float2(-0.3125f, 0.5f);
        float2 texCoord = float2(0.0f, 0.0f);
        if (vertexID == 1u || vertexID == 3u)
        {
            position = float2(-0.3125f, -0.5f);
            texCoord = float2(0.0f, 1.0f);
        }
        else if (vertexID == 2u || vertexID == 5u)
        {
            position = float2(0.3125f, 0.5f);
            texCoord = float2(1.0f, 0.0f);
        }
        else if (vertexID == 4u)
        {
            position = float2(0.3125f, -0.5f);
            texCoord = float2(1.0f, 1.0f);
        }

        WebgpuComputeTexturePlaneVertexOutput outputValue;
        outputValue.position = float4(position, 0.0f, 1.0f);
        outputValue.texCoord = texCoord;
        return outputValue;
    }

    /** Samples the linear storage texture and writes display-encoded RGBA8 plane color. */
    WebgpuComputeTextureFrameBuffer fragment(WebgpuComputeTexturePlaneVertexOutput inputValue)
    {
        const float4 sampledValue = planeBindGroup->storageTexture->sample(
            planeBindGroup->storageSampler,
            inputValue.texCoord);
        const float3 outputColor = float3(
            webgpuComputeTextureLinearToSrgb(sampledValue.x),
            webgpuComputeTextureLinearToSrgb(sampledValue.y),
            webgpuComputeTextureLinearToSrgb(sampledValue.z));

        WebgpuComputeTextureFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(outputColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the compute texture, single ordinary RenderClass plane, and final RGBA8 target. */
class WebgpuComputeTextureRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D>
        storageTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Sampler storageSampler;
    BindGroup<WebgpuComputeTextureStorageBindGroup> storageBindGroup;
    BindGroup<WebgpuComputeTexturePlaneBindGroup> planeBindGroup;
    ComputeClass<WebgpuComputeTextureComputePass> computePass;
    RenderClass<WebgpuComputeTexturePlanePass> planePass;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates all fixed compute and plane resources from the generated DSL contract. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);

        storageTexture = device->createTexture(
            "WebgpuComputeTextureStorageRGBA8",
            WebgpuComputeTextureWidth,
            WebgpuComputeTextureHeight,
            1u);
        storageSampler = device->createSampler({
            .label = "WebgpuComputeTextureLinearSampler",
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

        storageBindGroup = device->createBindGroup<WebgpuComputeTextureStorageBindGroup>(
            storageTexture->createView());
        planeBindGroup = device->createBindGroup<WebgpuComputeTexturePlaneBindGroup>(
            storageTexture->createView(),
            storageSampler);
        computePass = device->createComputeClass<WebgpuComputeTextureComputePass>(storageBindGroup);
        planePass = device->createRenderClass<WebgpuComputeTexturePlanePass>(planeBindGroup);
    }

    /** Allocates the explicit 800 by 500 RGBA8 capture target requested by the host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebgpuComputeTextureOutputRGBA8",
            width,
            height,
            1u);
    }

    /** Computes all texels, draws the only plane, and presents the same offscreen result. */
    void render() override
    {
        WebgpuComputeTextureFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->computePass(
                "WebgpuComputeTextureCompute",
                computePass(WebgpuComputeTextureWidth, WebgpuComputeTextureHeight, 1u))
            ->renderPass(
                "WebgpuComputeTexturePlane",
                frameBuffer,
                planePass(6u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for deterministic test readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
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

    /** Releases the generated storage and output textures after capture completes. */
    void destroy() override
    {
        device->freeTexture(storageTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
