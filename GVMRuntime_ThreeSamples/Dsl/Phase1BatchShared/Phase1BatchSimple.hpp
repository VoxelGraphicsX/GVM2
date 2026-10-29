#ifndef GVM_THREE_PHASE1_BATCH_SIMPLE_HPP
#define GVM_THREE_PHASE1_BATCH_SIMPLE_HPP

#include "UGL.h"
#include "Phase1BatchData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores deterministic simple-scene and compute controls. */
struct Phase1BatchSimpleData
{
    float4 timeModeAndInput;
    float4 viewportAndColor;
};

/** Binds the simple-scene controls and DSL-generated texture. */
struct Phase1BatchSimpleResources final : public IBindGroup
{
    /** Declares the shared read resources for the ordinary RenderClass. */
    constructor(UniformBuffer<Phase1BatchSimpleData> data [[Binding0]],
                Texture2D<float4> generatedTexture [[Binding1]],
                Sampler generatedSampler [[Binding2]])
    {
    }
};

/** Binds the writable texture used by the existing compute path. */
struct Phase1BatchSimpleComputeResources final : public IBindGroup
{
    /** Declares one RGBA8 storage texture and deterministic controls. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> generatedTexture [[Binding0]],
                UniformBuffer<Phase1BatchSimpleData> data [[Binding1]])
    {
    }
};

/** Carries standalone geometry attributes into fragments. */
struct Phase1BatchSimpleVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Defines the direct simple-scene output attachment. */
struct Phase1BatchSimpleFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Generates compute texture, terrain, and procedural patterns through the current DSL. */
class [[LocalWorkGroupSize(8, 8, 1)]] Phase1BatchSimpleComputePass final : public IComputeClass
{
public:
    /** Binds the current public RWTexture2D and uniform-buffer capabilities. */
    constructor(BindGroup<Phase1BatchSimpleComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Writes one deterministic texel for compute geometry and texture examples. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint2 coordinate = dispatchThreadID.xy;
        if (coordinate.x >= 512u || coordinate.y >= 512u) return;
        const float2 uv = (float2(coordinate) + float2(0.5f)) / 512.0f;
        const float timeValue = resources->data->timeModeAndInput.x;
        const float mode = resources->data->timeModeAndInput.y;
        const float wave = sin((uv.x + timeValue * 0.03f) * 31.0f) *
                           cos((uv.y - timeValue * 0.02f) * 27.0f);
        const float checker = fmod(floor(uv.x * 16.0f) + floor(uv.y * 16.0f), 2.0f);
        const float3 procedural = mode > 2.5f
            ? lerp(float3(0.08f, 0.12f, 0.28f), float3(0.85f, 0.42f, 0.12f), checker)
            : float3(uv.x, uv.y, wave * 0.5f + 0.5f);
        resources->generatedTexture->write(coordinate, half4(float4(procedural, 1.0f)));
    }
};

/** Draws one ordinary, non-instanced simple Scene from standalone geometry. */
class Phase1BatchSimpleScenePass final : public IRenderClass
{
public:
    /** Binds ordinary resources without RenderSet or entity builtins. */
    constructor(BindGroup<Phase1BatchSimpleResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies deterministic compute-geometry displacement in the DSL vertex stage. */
    Phase1BatchSimpleVertexOutput vertex(Phase1BatchSimpleVertex inputValue [[VertexInput0]])
    {
        const float timeValue = resources->data->timeModeAndInput.x;
        const float mode = resources->data->timeModeAndInput.y;
        float4 position = inputValue.position;
        if (mode < 1.5f)
        {
            position.z += sin(position.x * 7.0f + timeValue) *
                          cos(position.y * 5.0f - timeValue * 0.7f) * 0.08f;
        }
        Phase1BatchSimpleVertexOutput outputValue;
        outputValue.position = position;
        outputValue.color = inputValue.color;
        outputValue.uv = inputValue.uvAndNormal.xy;
        return outputValue;
    }

    /** Samples the compute result and evaluates Sobel/equirectangular/MRT comparison modes. */
    Phase1BatchSimpleFrameBuffer fragment(Phase1BatchSimpleVertexOutput inputValue)
    {
        const float mode = resources->data->timeModeAndInput.y;
        const float2 texel = float2(1.0f / 512.0f);
        float3 color = resources->generatedTexture->sample(resources->generatedSampler, inputValue.uv).xyz;
        if (mode > 1.5f && mode < 2.5f)
        {
            const float3 left = resources->generatedTexture->sample(resources->generatedSampler, inputValue.uv - float2(texel.x, 0.0f)).xyz;
            const float3 right = resources->generatedTexture->sample(resources->generatedSampler, inputValue.uv + float2(texel.x, 0.0f)).xyz;
            const float3 up = resources->generatedTexture->sample(resources->generatedSampler, inputValue.uv - float2(0.0f, texel.y)).xyz;
            const float3 down = resources->generatedTexture->sample(resources->generatedSampler, inputValue.uv + float2(0.0f, texel.y)).xyz;
            color = abs(right - left) + abs(down - up);
        }
        else if (mode > 3.5f)
        {
            const float longitude = (inputValue.uv.x - 0.5f) * 6.28318530718f;
            const float latitude = (0.5f - inputValue.uv.y) * 3.14159265359f;
            const float3 direction = float3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
            color = float3(direction * 0.5f + float3(0.5f));
        }
        Phase1BatchSimpleFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color * inputValue.color.xyz), half(1.0f));
        return frameBuffer;
    }
};

/** Owns one ordinary Scene, one compute texture, and no RenderSet. */
class Phase1BatchSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<Phase1BatchSimpleVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<Phase1BatchSimpleData, BufferUsage<Uniform, CopyDst>> dataBuffer;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<StorageBinding, TextureBinding>, TextureDimension::e2D> generatedTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    Sampler generatedSampler;
    BindGroup<Phase1BatchSimpleResources> resources;
    BindGroup<Phase1BatchSimpleComputeResources> computeResources;
    ComputeClass<Phase1BatchSimpleComputePass> computePass;
    RenderClass<Phase1BatchSimpleScenePass> scenePass;
    Phase1BatchSimpleData data;
    uint indexCount = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates all ordinary geometry, compute, and sampling resources through the DSL. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer("Phase1BatchSimpleVertices", 65536u);
        indexBuffer = device->createBuffer("Phase1BatchSimpleIndices", 131072u);
        dataBuffer = device->createBuffer("Phase1BatchSimpleData", 1u);
        generatedTexture = device->createTexture("Phase1BatchGeneratedTexture", 512u, 512u, 1u);
        generatedSampler = device->createSampler({
            .label = "Phase1BatchGeneratedSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        resources = device->createBindGroup<Phase1BatchSimpleResources>(
            dataBuffer, generatedTexture->createView(), generatedSampler);
        computeResources = device->createBindGroup<Phase1BatchSimpleComputeResources>(
            generatedTexture->createView(), dataBuffer);
        computePass = device->createComputeClass<Phase1BatchSimpleComputePass>(computeResources);
        scenePass = device->createRenderClass<Phase1BatchSimpleScenePass>(resources);
    }

    /** Allocates the fixed final color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("Phase1BatchSimpleOutput", width, height, 1u);
        outputDepth = device->createTexture("Phase1BatchSimpleDepth", width, height, 1u);
    }

    /** Uploads one deterministic standalone geometry payload and case controls. */
    void configureScene(const eastl::vector<Phase1BatchSimpleVertex> &vertices,
                        const eastl::vector<uint> &indices,
                        float timeValue,
                        float mode,
                        float inputX,
                        float inputY)
    {
        indexCount = uint(indices.size());
        data.timeModeAndInput = float4(timeValue, mode, inputX, inputY);
        data.viewportAndColor = float4(float(readbackWidth), float(readbackHeight), 1.0f, 1.0f);
        graphicsQueue->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                                   uint64_t(vertices.size()) * sizeof(Phase1BatchSimpleVertex))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(dataBuffer), &data, sizeof(data))->submit();
    }

    /** Dispatches the compute stage and draws the one ordinary indexed Scene object. */
    void render() override
    {
        Phase1BatchSimpleFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->computePass("Phase1BatchSimpleCompute", computePass(512u, 512u, 1u))
            ->renderPass("Phase1BatchSimpleScene", frameBuffer,
                scenePass->setVertexBuffer(vertexBuffer), scenePass->setIndexBuffer(indexBuffer),
                scenePass(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final color target. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases every private simple-scene resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(dataBuffer);
        device->freeTexture(generatedTexture);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
