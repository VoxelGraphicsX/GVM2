#ifndef GVM_THREE_WEBGPU_COMPUTE_TEXTURE_PINGPONG_HPP
#define GVM_THREE_WEBGPU_COMPUTE_TEXTURE_PINGPONG_HPP

#include "UGL.h"
#include "WebgpuComputeTexturePingpongData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the deterministic r185 seed used by the initialization dispatch. */
struct PingpongSeedData
{
    float4 seedAndReserved;
};

/** Stores the texture selected for the current presentation frame. */
struct PingpongPresentData
{
    float4 phaseAndViewport;
};

/** Binds the writable ping texture and deterministic random seed. */
struct PingpongInitResources final : public IBindGroup
{
    /** Declares the exact RGBA16Float initialization resources. */
    constructor(
        RWTexture2D<TextureFormat::RGBA16Float> ping [[Binding0]],
        UniformBuffer<PingpongSeedData> seed [[Binding1]])
    {
    }
};

/** Binds ping as the source and pong as the destination. */
struct PingpongToPongResources final : public IBindGroup
{
    /** Declares one ordered ping-to-pong compute step. */
    constructor(
        RWTexture2D<TextureFormat::RGBA16Float> ping [[Binding0]],
        RWTexture2D<TextureFormat::RGBA16Float> pong [[Binding1]])
    {
    }
};

/** Binds pong as the source and ping as the destination. */
struct PingpongToPingResources final : public IBindGroup
{
    /** Declares one ordered pong-to-ping compute step. */
    constructor(
        RWTexture2D<TextureFormat::RGBA16Float> pong [[Binding0]],
        RWTexture2D<TextureFormat::RGBA16Float> ping [[Binding1]])
    {
    }
};

/** Binds one completed storage result and the explicit 256-square mip. */
struct PingpongMipResources final : public IBindGroup
{
    /** Declares one source base level and one writable mip equivalent. */
    constructor(
        RWTexture2D<TextureFormat::RGBA16Float> source [[Binding0]],
        RWTexture2D<TextureFormat::RGBA16Float> mip [[Binding1]])
    {
    }
};

/** Initializes ping with the exact r185 TSL random function. */
class [[LocalWorkGroupSize(64, 1, 1)]]
WebgpuComputeTexturePingpongInitPass final : public IComputeClass
{
public:
    /** Binds the initialization seed and ping storage texture. */
    constructor(BindGroup<PingpongInitResources> resources [[Slot0]])
    {
    }

private:
    /** Writes all 512 squared seeded RGBA16Float texels. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint linearIndex = dispatchThreadID.x;
        if (linearIndex >= 512u * 512u) return;
        const uint2 coordinate =
            uint2(linearIndex % 512u, linearIndex / 512u);
        const float2 uv = float2(coordinate) / 512.0f;
        const float2 seedValue = resources->seed->seedAndReserved.xy;
        const float r =
            frac(sin(dot(
                     uv + seedValue * 100.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f) -
            frac(sin(dot(
                     uv + seedValue * 300.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f);
        const float g =
            frac(sin(dot(
                     uv + seedValue * 200.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f) -
            frac(sin(dot(
                     uv + seedValue * 300.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f);
        const float b =
            frac(sin(dot(
                     uv + seedValue * 200.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f) -
            frac(sin(dot(
                     uv + seedValue * 100.0f,
                     float2(12.9898f, 4.1414f))) * 43758.5453f);
        resources->ping->write(
            coordinate, half4(float4(r, g, b, 1.0f)));
    }
};

/** Executes the upstream diagonal five-texel ping-to-pong blur. */
class [[LocalWorkGroupSize(64, 1, 1)]]
WebgpuComputeTexturePingpongToPongPass final : public IComputeClass
{
public:
    /** Binds ping for reads and pong for writes. */
    constructor(BindGroup<PingpongToPongResources> resources [[Slot0]])
    {
    }

private:
    /** Writes one ordered ping-to-pong blur result. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint linearIndex = dispatchThreadID.x;
        if (linearIndex >= 512u * 512u) return;
        const uint2 coordinate =
            uint2(linearIndex % 512u, linearIndex / 512u);
        const uint left = (coordinate.x + 511u) % 512u;
        const uint right = min(coordinate.x + 1u, 511u);
        const uint upper = min(coordinate.y + 1u, 511u);
        const uint lower = (coordinate.y + 511u) % 512u;
        const float4 color =
            (float4(resources->ping->read(uint2(left, upper))) +
             float4(resources->ping->read(uint2(left, lower))) +
             float4(resources->ping->read(coordinate)) +
             float4(resources->ping->read(uint2(right, lower))) +
             float4(resources->ping->read(uint2(right, upper)))) / 5.0f;
        resources->pong->write(
            coordinate, half4(float4(color.xyz * 1.05f, 1.0f)));
    }
};

/** Executes the upstream diagonal five-texel pong-to-ping blur. */
class [[LocalWorkGroupSize(64, 1, 1)]]
WebgpuComputeTexturePingpongToPingPass final : public IComputeClass
{
public:
    /** Binds pong for reads and ping for writes. */
    constructor(BindGroup<PingpongToPingResources> resources [[Slot0]])
    {
    }

private:
    /** Writes one ordered pong-to-ping blur result. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint linearIndex = dispatchThreadID.x;
        if (linearIndex >= 512u * 512u) return;
        const uint2 coordinate =
            uint2(linearIndex % 512u, linearIndex / 512u);
        const uint left = (coordinate.x + 511u) % 512u;
        const uint right = min(coordinate.x + 1u, 511u);
        const uint upper = min(coordinate.y + 1u, 511u);
        const uint lower = (coordinate.y + 511u) % 512u;
        const float4 color =
            (float4(resources->pong->read(uint2(left, upper))) +
             float4(resources->pong->read(uint2(left, lower))) +
             float4(resources->pong->read(coordinate)) +
             float4(resources->pong->read(uint2(right, lower))) +
             float4(resources->pong->read(uint2(right, upper)))) / 5.0f;
        resources->ping->write(
            coordinate, half4(float4(color.xyz * 1.05f, 1.0f)));
    }
};

/** Builds the exact first mip generated by the r185 texture backend. */
class [[LocalWorkGroupSize(64, 1, 1)]]
WebgpuComputeTexturePingpongMipPass final : public IComputeClass
{
public:
    /** Binds the completed base level and writable half-size texture. */
    constructor(BindGroup<PingpongMipResources> resources [[Slot0]])
    {
    }

private:
    /** Averages one aligned two-by-two texel footprint into RGBA16Float. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint linearIndex = dispatchThreadID.x;
        if (linearIndex >= 256u * 256u) return;
        const uint2 target =
            uint2(linearIndex % 256u, linearIndex / 256u);
        const uint2 source = target * 2u;
        const float4 color =
            (float4(resources->source->read(source)) +
             float4(resources->source->read(source + uint2(1u, 0u))) +
             float4(resources->source->read(source + uint2(0u, 1u))) +
             float4(resources->source->read(source + uint2(1u, 1u)))) /
            4.0f;
        resources->mip->write(target, half4(color));
    }
};

/** Binds the generated first mip for ordinary plane presentation. */
struct PingpongPresentResources final : public IBindGroup
{
    /** Declares the sampled mip, sampler, and deterministic viewport state. */
    constructor(
        Texture2D<float4> mip [[Binding0]],
        Sampler textureSampler [[Binding1]],
        UniformBuffer<PingpongPresentData> data [[Binding2]])
    {
    }
};

/** Carries clip position and plane UV into the material fragment. */
struct PingpongPlaneVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the final ordinary Scene output attachments. */
struct PingpongFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with the r185 output transfer. */
float pingpongLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Presents the currently completed storage texture on one ordinary plane. */
class WebgpuComputeTexturePingpongMainPass final : public IRenderClass
{
public:
    /** Binds only private texture and phase resources. */
    constructor(BindGroup<PingpongPresentResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Passes the orthographic plane attributes directly to the fragment. */
    PingpongPlaneVertexOutput vertex(
        PingpongPlaneVertex inputValue [[VertexInput0]])
    {
        PingpongPlaneVertexOutput outputValue;
        outputValue.position = inputValue.position;
        outputValue.position.y = -outputValue.position.y;
        outputValue.uv = inputValue.uvAndReserved.xy;
        return outputValue;
    }

    /** Samples the r185-equivalent first mip and applies output conversion. */
    PingpongFrameBuffer fragment(PingpongPlaneVertexOutput inputValue)
    {
        const float3 linearColor = max(
            float3(resources->mip->sample(
                resources->textureSampler, inputValue.uv).xyz),
            float3(0.0f));
        PingpongFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                pingpongLinearToSrgb(linearColor.x),
                pingpongLinearToSrgb(linearColor.y),
                pingpongLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the exact two-texture compute and ordinary plane presentation chain. */
class WebgpuComputeTexturePingpongRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> ping;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> pong;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> presentMip;
    Buffer<PingpongSeedData, BufferUsage<Uniform, CopyDst>> seedBuffer;
    Buffer<PingpongPresentData, BufferUsage<Uniform, CopyDst>> presentBuffer;
    Buffer<PingpongPlaneVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    BindGroup<PingpongInitResources> initResources;
    BindGroup<PingpongToPongResources> toPongResources;
    BindGroup<PingpongToPingResources> toPingResources;
    BindGroup<PingpongMipResources> pingMipResources;
    BindGroup<PingpongMipResources> pongMipResources;
    BindGroup<PingpongPresentResources> presentResources;
    ComputeClass<WebgpuComputeTexturePingpongInitPass> initPass;
    ComputeClass<WebgpuComputeTexturePingpongToPongPass> toPongPass;
    ComputeClass<WebgpuComputeTexturePingpongToPingPass> toPingPass;
    ComputeClass<WebgpuComputeTexturePingpongMipPass> pingMipPass;
    ComputeClass<WebgpuComputeTexturePingpongMipPass> pongMipPass;
    RenderClass<WebgpuComputeTexturePingpongMainPass> presentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    Sampler textureSampler;
    uint frameIndex = 0u;
    uint randomState = 161639577u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both RGBA16Float textures and all private DSL passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        ping = device->createTexture("PingpongPing", 512u, 512u, 1u);
        pong = device->createTexture("PingpongPong", 512u, 512u, 1u);
        presentMip =
            device->createTexture("PingpongPresentMip", 256u, 256u, 1u);
        seedBuffer = device->createBuffer("PingpongSeed", 1u);
        presentBuffer = device->createBuffer("PingpongPresent", 1u);
        vertexBuffer = device->createBuffer("PingpongVertices", 4u);
        indexBuffer = device->createBuffer("PingpongIndices", 6u);
        textureSampler = device->createSampler({
            .label = "PingpongSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .compare = CompareFunction::Undefined,
            .maxAnisotropy = 1u,
        });
        initResources = device->createBindGroup<PingpongInitResources>(
            ping->createView(), seedBuffer);
        toPongResources =
            device->createBindGroup<PingpongToPongResources>(
                ping->createView(), pong->createView());
        toPingResources =
            device->createBindGroup<PingpongToPingResources>(
                pong->createView(), ping->createView());
        pingMipResources =
            device->createBindGroup<PingpongMipResources>(
                ping->createView(), presentMip->createView());
        pongMipResources =
            device->createBindGroup<PingpongMipResources>(
                pong->createView(), presentMip->createView());
        presentResources =
            device->createBindGroup<PingpongPresentResources>(
                presentMip->createView(), textureSampler, presentBuffer);
        initPass =
            device->createComputeClass<
                WebgpuComputeTexturePingpongInitPass>(initResources);
        toPongPass =
            device->createComputeClass<
                WebgpuComputeTexturePingpongToPongPass>(toPongResources);
        toPingPass =
            device->createComputeClass<
                WebgpuComputeTexturePingpongToPingPass>(toPingResources);
        pingMipPass =
            device->createComputeClass<
                WebgpuComputeTexturePingpongMipPass>(pingMipResources);
        pongMipPass =
            device->createComputeClass<
                WebgpuComputeTexturePingpongMipPass>(pongMipResources);
        presentPass =
            device->createRenderClass<
                WebgpuComputeTexturePingpongMainPass>(presentResources);
    }

    /** Allocates deterministic final color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor =
            device->createTexture("PingpongOutput", width, height, 1u);
        outputDepth =
            device->createTexture("PingpongDepth", width, height, 1u);
    }

    /** Uploads the ordinary plane used by every scenario. */
    void configurePlane(
        const eastl::vector<PingpongPlaneVertex> &vertices,
        const eastl::vector<uint> &indices)
    {
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer), vertices.data(),
                uint64_t(vertices.size()) * sizeof(PingpongPlaneVertex))
            ->writeBuffer(
                BufferRange(indexBuffer), indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->submit();
    }

    /** Runs the optional reset, one ordered blur, and one plane draw. */
    void render() override
    {
        const bool phaseToPong = frameIndex % 2u == 0u;
        float2 seedValue =
            float2(0.40081608295440674f, 0.25348329544067383f);
        const bool resetAtSecondBoundary =
            frameIndex > 1u && frameIndex % 60u == 1u;
        if (resetAtSecondBoundary)
        {
            randomState ^= randomState << 13u;
            randomState ^= randomState >> 17u;
            randomState ^= randomState << 5u;
            seedValue.x =
                float(randomState >> 8u) / 16777216.0f;
            randomState ^= randomState << 13u;
            randomState ^= randomState >> 17u;
            randomState ^= randomState << 5u;
            seedValue.y =
                float(randomState >> 8u) / 16777216.0f;
        }
        const PingpongSeedData seedData = {
            float4(seedValue, 0.0f, 0.0f)};
        const PingpongPresentData presentData =
            {float4(phaseToPong ? 1.0f : 0.0f,
                    float(width), float(height), 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(seedBuffer), &seedData, sizeof(seedData))
            ->writeBuffer(
                BufferRange(presentBuffer),
                &presentData, sizeof(presentData))
            ->submit();
        PingpongFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto swapchainTexture = swapchain->queryNextTexture();
        if (frameIndex == 0u || resetAtSecondBoundary)
        {
            graphicsQueue->computePass(
                "PingpongInit", initPass(512u * 512u, 1u, 1u));
        }
        if (phaseToPong)
        {
            graphicsQueue->computePass(
                "PingpongToPong",
                toPongPass(512u * 512u, 1u, 1u));
            graphicsQueue->computePass(
                "PingpongPongMip",
                pongMipPass(256u * 256u, 1u, 1u));
        }
        else
        {
            graphicsQueue->computePass(
                "PingpongToPing",
                toPingPass(512u * 512u, 1u, 1u));
            graphicsQueue->computePass(
                "PingpongPingMip",
                pingMipPass(256u * 256u, 1u, 1u));
        }
        graphicsQueue
            ->renderPass(
                "PingpongPresent", frameBuffer,
                presentPass->setVertexBuffer(vertexBuffer),
                presentPass->setIndexBuffer(indexBuffer),
                presentPass(6u, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                swapchainTexture, outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        frameIndex += 1u;
    }

    /** Returns the DSL-owned final color target. */
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

    /** Releases every private ping-pong resource. */
    void destroy() override
    {
        device->freeTexture(ping);
        device->freeTexture(pong);
        device->freeTexture(presentMip);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
        device->freeBuffer(seedBuffer);
        device->freeBuffer(presentBuffer);
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
    }
};

#endif
