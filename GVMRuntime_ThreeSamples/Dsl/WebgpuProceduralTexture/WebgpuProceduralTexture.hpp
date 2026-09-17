#ifndef GVM_THREE_WEBGPU_PROCEDURAL_TEXTURE_HPP
#define GVM_THREE_WEBGPU_PROCEDURAL_TEXTURE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the exact r185 checker scale and Gaussian blur direction radius. */
struct WebgpuProceduralTextureData
{
    float4 scaleBlurAndFlags;
};

/** Binds the procedural checker destination and scenario controls. */
struct WebgpuProceduralTextureCheckerResources final : public IBindGroup
{
    /** Declares the writable checker texture and immutable frame parameters. */
    constructor(
        RWTexture2D<TextureFormat::RGBA16Float> checker [[Binding0]],
        UniformBuffer<WebgpuProceduralTextureData> data [[Binding1]])
    {
    }
};

/** Binds one separable blur source, destination, sampler, and direction. */
struct WebgpuProceduralTextureBlurResources final : public IBindGroup
{
    /** Declares resources shared by the horizontal and vertical blur classes. */
    constructor(
        Texture2D<float4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        RWTexture2D<TextureFormat::RGBA16Float> destination [[Binding2]],
        UniformBuffer<WebgpuProceduralTextureData> data [[Binding3]])
    {
    }
};

/** Binds the completed procedural texture to the ordinary plane material. */
struct WebgpuProceduralTexturePresentResources final : public IBindGroup
{
    /** Declares the vertically blurred texture and its linear sampler. */
    constructor(
        Texture2D<float4> texture [[Binding0]],
        Sampler textureSampler [[Binding1]])
    {
    }
};

/** Carries analytic fullscreen coordinates into the plane fragment. */
struct WebgpuProceduralTextureVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic RGBA8 attachment. */
struct WebgpuProceduralTextureFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Generates the exact 512-square TSL checker converted to a texture. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuProceduralTextureCheckerPass final : public IComputeClass
{
public:
    /** Binds only the DSL-owned checker target and scenario uniform. */
    constructor(
        BindGroup<WebgpuProceduralTextureCheckerResources>
            resources [[Slot0]])
    {
    }

private:
    /** Writes one texel using Three r185 checker coordinate semantics. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint2 coordinate = dispatchThreadID.xy;
        if (coordinate.x >= 512u || coordinate.y >= 512u)
        {
            return;
        }
        const float2 uv =
            (float2(coordinate) + float2(0.5f)) / 512.0f;
        const float uvScale =
            resources->data->scaleBlurAndFlags.x;
        const float2 checkerUv = uv * uvScale * 2.0f;
        const float parity = fmod(
            floor(checkerUv.x) + floor(checkerUv.y),
            2.0f);
        const float value = sign(parity);
        resources->checker->write(
            coordinate,
            half4(half(value), half(value), half(value), half(1.0f)));
    }
};

/** Applies Three r185's horizontal radius-20 Gaussian filter. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuProceduralTextureHorizontalBlurPass final : public IComputeClass
{
public:
    /** Binds the checker source and horizontal intermediate destination. */
    constructor(
        BindGroup<WebgpuProceduralTextureBlurResources>
            resources [[Slot0]])
    {
    }

private:
    /** Evaluates all 85 symmetric kernel taps for one output texel. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint2 coordinate = dispatchThreadID.xy;
        if (coordinate.x >= 512u || coordinate.y >= 512u)
        {
            return;
        }
        const float2 uv =
            (float2(coordinate) + float2(0.5f)) / 512.0f;
        const float kernelRadius = 43.0f;
        const float sigma = kernelRadius / 3.0f;
        const float blurAmount =
            resources->data->scaleBlurAndFlags.y;
        const float firstWeight = 0.39894f / sigma;
        float4 sum = resources->source->sampleLevel(
            resources->sourceSampler, uv, 0.0f) * firstWeight;
        for (uint index = 1u; index < 43u; index += 1u)
        {
            const float tap = float(index);
            const float weight =
                0.39894f *
                exp(-0.5f * tap * tap / (sigma * sigma)) /
                sigma;
            const float2 offset =
                float2(blurAmount * tap / 512.0f, 0.0f);
            sum +=
                (resources->source->sampleLevel(
                     resources->sourceSampler, uv + offset, 0.0f) +
                 resources->source->sampleLevel(
                     resources->sourceSampler, uv - offset, 0.0f)) *
                weight;
        }
        resources->destination->write(
            coordinate, half4(sum));
    }
};

/** Applies Three r185's vertical radius-20 Gaussian filter. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebgpuProceduralTextureVerticalBlurPass final : public IComputeClass
{
public:
    /** Binds the horizontal source and completed vertical destination. */
    constructor(
        BindGroup<WebgpuProceduralTextureBlurResources>
            resources [[Slot0]])
    {
    }

private:
    /** Evaluates all 85 symmetric kernel taps for one output texel. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint2 coordinate = dispatchThreadID.xy;
        if (coordinate.x >= 512u || coordinate.y >= 512u)
        {
            return;
        }
        const float2 uv =
            (float2(coordinate) + float2(0.5f)) / 512.0f;
        const float kernelRadius = 43.0f;
        const float sigma = kernelRadius / 3.0f;
        const float blurAmount =
            resources->data->scaleBlurAndFlags.y;
        const float firstWeight = 0.39894f / sigma;
        float4 sum = resources->source->sampleLevel(
            resources->sourceSampler, uv, 0.0f) * firstWeight;
        for (uint index = 1u; index < 43u; index += 1u)
        {
            const float tap = float(index);
            const float weight =
                0.39894f *
                exp(-0.5f * tap * tap / (sigma * sigma)) /
                sigma;
            const float2 offset =
                float2(0.0f, blurAmount * tap / 512.0f);
            sum +=
                (resources->source->sampleLevel(
                     resources->sourceSampler, uv + offset, 0.0f) +
                 resources->source->sampleLevel(
                     resources->sourceSampler, uv - offset, 0.0f)) *
                weight;
        }
        resources->destination->write(
            coordinate, half4(sum));
    }
};

/** Converts one linear channel using Three r185's output color transfer. */
float webgpuProceduralTextureLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the one ordinary PlaneGeometry without a RenderSet. */
class WebgpuProceduralTextureMainPass final : public IRenderClass
{
public:
    /** Binds the completed procedural texture and disables irrelevant depth. */
    constructor(
        BindGroup<WebgpuProceduralTexturePresentResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for analytic orthographic plane coverage. */
    WebgpuProceduralTextureVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuProceduralTextureVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples the 1x1 plane and applies the renderer output transfer. */
    WebgpuProceduralTextureFrameBuffer fragment(
        WebgpuProceduralTextureVertexOutput inputValue)
    {
        const float2 ndc =
            inputValue.screenUv * 2.0f - 1.0f;
        const float2 halfExtent = float2(0.3125f, 0.5f);
        const bool inside =
            abs(ndc.x) <= halfExtent.x &&
            abs(ndc.y) <= halfExtent.y;
        float3 color = float3(0.0f);
        if (inside)
        {
            float2 uv =
                ndc / (halfExtent * 2.0f) + 0.5f;
            uv.y = 1.0f - uv.y;
            color = resources->texture->sampleLevel(
                resources->textureSampler, uv, 0.0f).xyz;
            color = float3(
                webgpuProceduralTextureLinearToSrgb(color.x),
                webgpuProceduralTextureLinearToSrgb(color.y),
                webgpuProceduralTextureLinearToSrgb(color.z));
        }
        WebgpuProceduralTextureFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated checker, separable blur, plane, and readback chain. */
class WebgpuProceduralTextureRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuProceduralTextureData,
           BufferUsage<Uniform, CopyDst>> dataBuffer;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> checkerTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> horizontalTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> verticalTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler linearSampler;
    BindGroup<WebgpuProceduralTextureCheckerResources>
        checkerResources;
    BindGroup<WebgpuProceduralTextureBlurResources>
        horizontalResources;
    BindGroup<WebgpuProceduralTextureBlurResources>
        verticalResources;
    BindGroup<WebgpuProceduralTexturePresentResources>
        presentResources;
    ComputeClass<WebgpuProceduralTextureCheckerPass> checkerPass;
    ComputeClass<WebgpuProceduralTextureHorizontalBlurPass>
        horizontalPass;
    ComputeClass<WebgpuProceduralTextureVerticalBlurPass>
        verticalPass;
    RenderClass<WebgpuProceduralTextureMainPass> mainPass;
    WebgpuProceduralTextureData data = {};
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;
    bool autoUpdate = true;

public:
    /** Creates the current public DSL resources used by the r185 algorithm. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        dataBuffer =
            device->createBuffer("ProceduralTextureData", 1u);
        checkerTexture =
            device->createTexture("ProceduralChecker", 512u, 512u, 1u);
        horizontalTexture =
            device->createTexture(
                "ProceduralHorizontal", 512u, 512u, 1u);
        verticalTexture =
            device->createTexture(
                "ProceduralVertical", 512u, 512u, 1u);
        linearSampler = device->createSampler({
            .label = "ProceduralTextureLinearSampler",
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
        checkerResources =
            device->createBindGroup<
                WebgpuProceduralTextureCheckerResources>(
                    checkerTexture->createView(),
                    dataBuffer);
        horizontalResources =
            device->createBindGroup<
                WebgpuProceduralTextureBlurResources>(
                    checkerTexture->createView(),
                    linearSampler,
                    horizontalTexture->createView(),
                    dataBuffer);
        verticalResources =
            device->createBindGroup<
                WebgpuProceduralTextureBlurResources>(
                    horizontalTexture->createView(),
                    linearSampler,
                    verticalTexture->createView(),
                    dataBuffer);
        presentResources =
            device->createBindGroup<
                WebgpuProceduralTexturePresentResources>(
                    verticalTexture->createView(),
                    linearSampler);
        checkerPass =
            device->createComputeClass<
                WebgpuProceduralTextureCheckerPass>(
                    checkerResources);
        horizontalPass =
            device->createComputeClass<
                WebgpuProceduralTextureHorizontalBlurPass>(
                    horizontalResources);
        verticalPass =
            device->createComputeClass<
                WebgpuProceduralTextureVerticalBlurPass>(
                    verticalResources);
        mainPass =
            device->createRenderClass<
                WebgpuProceduralTextureMainPass>(
                    presentResources);
    }

    /** Allocates the final deterministic output texture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor =
            device->createTexture(
                "ProceduralTextureOutput",
                width,
                height,
                1u);
    }

    /** Selects one locked Manifest scenario without pipeline branching. */
    void configureScenario(
        float uvScale,
        float blurAmount,
        bool inAutoUpdate)
    {
        data.scaleBlurAndFlags =
            float4(
                uvScale,
                blurAmount,
                inAutoUpdate ? 1.0f : 0.0f,
                0.0f);
        autoUpdate = inAutoUpdate;
    }

    /** Generates the procedural texture and draws the ordinary plane. */
    void render() override
    {
        if (autoUpdate || frameIndex == 0u)
        {
            graphicsQueue
                ->writeBuffer(
                    BufferRange(dataBuffer),
                    &data,
                    sizeof(data))
                ->submit();
            graphicsQueue->computePass(
                "WebgpuProceduralTextureChecker",
                checkerPass(512u, 512u, 1u));
            graphicsQueue->computePass(
                "WebgpuProceduralTextureHorizontalBlur",
                horizontalPass(512u, 512u, 1u));
            graphicsQueue->computePass(
                "WebgpuProceduralTextureVerticalBlur",
                verticalPass(512u, 512u, 1u));
        }
        WebgpuProceduralTextureFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture =
            swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuProceduralTextureMain",
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

    /** Returns the final DSL-owned RGBA8 readback texture. */
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

    /** Releases every dedicated procedural-texture resource. */
    void destroy() override
    {
        device->freeTexture(checkerTexture);
        device->freeTexture(horizontalTexture);
        device->freeTexture(verticalTexture);
        device->freeTexture(outputColor);
        device->freeBuffer(dataBuffer);
    }
};

#endif
