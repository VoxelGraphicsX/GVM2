#ifndef GVM_THREE_WEBGPU_TEXTUREGRAD_HPP
#define GVM_THREE_WEBGPU_TEXTUREGRAD_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the exact r185 texture-gradient time and viewport state. */
struct WebgpuTexturegradUniforms
{
    float4 timeViewportAndReserved;
};

/** Binds the locked UV grid and explicit-gradient sampler. */
struct WebgpuTexturegradResources final : public IBindGroup
{
    /** Declares the complete resource set shared by both simple Scenes. */
    constructor(
        UniformBuffer<WebgpuTexturegradUniforms> uniforms [[Binding0]],
        Texture2D<float4> uvGrid [[Binding1]],
        Sampler uvGridSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into each panel fragment. */
struct WebgpuTexturegradVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the deterministic two-panel RGBA8 attachment. */
struct WebgpuTexturegradFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel with Three r185's output transfer. */
float webgpuTexturegradLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the exact TSL four-tap explicit-gradient material. */
float4 webgpuTexturegradShade(
    BindGroup<WebgpuTexturegradResources> resources,
    float2 planeUv)
{
    const float time =
        resources->uniforms->timeViewportAndReserved.x;
    const float blurBase =
        (0.0625f -
         cos(planeUv.x * 20.0f + time)) *
        0.0625f;
    const float blur = blurBase * blurBase;
    float2 gradient = float2(blur);
    if (planeUv.y > 0.5f)
    {
        gradient = float2(0.0f);
    }
    const float2 halfBlur = float2(blur * 0.5f);
    float4 linearColor =
        resources->uvGrid->sampleGrad(
            resources->uvGridSampler,
            planeUv + halfBlur,
            gradient,
            gradient) *
        0.25f;
    linearColor +=
        resources->uvGrid->sampleGrad(
            resources->uvGridSampler,
            planeUv + float2(halfBlur.x, -halfBlur.y),
            gradient,
            gradient) *
        0.25f;
    linearColor +=
        resources->uvGrid->sampleGrad(
            resources->uvGridSampler,
            planeUv + float2(-halfBlur.x, halfBlur.y),
            gradient,
            gradient) *
        0.25f;
    linearColor +=
        resources->uvGrid->sampleGrad(
            resources->uvGridSampler,
            planeUv - halfBlur,
            gradient,
            gradient) *
        0.25f;
    if (planeUv.y > 0.497f && planeUv.y < 0.503f)
    {
        linearColor = float4(1.0f);
    }
    return float4(
        webgpuTexturegradLinearToSrgb(linearColor.x),
        webgpuTexturegradLinearToSrgb(linearColor.y),
        webgpuTexturegradLinearToSrgb(linearColor.z),
        1.0f);
}

/** Draws the left WebGPU-backed independent Plane Scene. */
class WebgpuTexturegradWebgpuMainPass final : public IRenderClass
{
public:
    /** Configures one opaque ordinary RenderClass without RenderSet. */
    constructor(
        BindGroup<WebgpuTexturegradResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle with exact screen coordinates. */
    WebgpuTexturegradVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTexturegradVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = uv;
        return outputValue;
    }

    /** Applies the left canvas background and exact 250-square plane. */
    WebgpuTexturegradFrameBuffer fragment(
        WebgpuTexturegradVertexOutput inputValue)
    {
        const float2 pixel =
            inputValue.screenUv *
            resources->uniforms->timeViewportAndReserved.yz;
        if (pixel.x >= 400.0f)
        {
            discard_fragment();
        }
        WebgpuTexturegradFrameBuffer frameBuffer;
        if (pixel.x < 75.0f || pixel.x >= 325.0f ||
            pixel.y < 125.0f || pixel.y >= 375.0f)
        {
            frameBuffer.color = half4(
                half3(49.0f / 255.0f),
                half(1.0f));
            return frameBuffer;
        }
        const float2 planeUv =
            float2(
                (pixel.x - 75.0f) / 250.0f,
                1.0f - (pixel.y - 125.0f) / 250.0f);
        frameBuffer.color =
            half4(webgpuTexturegradShade(resources, planeUv));
        return frameBuffer;
    }
};

/** Draws the right WebGL-backed independent Plane Scene. */
class WebgpuTexturegradWebglMainPass final : public IRenderClass
{
public:
    /** Configures the second opaque ordinary RenderClass. */
    constructor(
        BindGroup<WebgpuTexturegradResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle with exact screen coordinates. */
    WebgpuTexturegradVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTexturegradVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = uv;
        return outputValue;
    }

    /** Applies the right canvas background and exact 250-square plane. */
    WebgpuTexturegradFrameBuffer fragment(
        WebgpuTexturegradVertexOutput inputValue)
    {
        const float2 pixel =
            inputValue.screenUv *
            resources->uniforms->timeViewportAndReserved.yz;
        if (pixel.x < 400.0f)
        {
            discard_fragment();
        }
        WebgpuTexturegradFrameBuffer frameBuffer;
        if (pixel.x < 475.0f || pixel.x >= 725.0f ||
            pixel.y < 125.0f || pixel.y >= 375.0f)
        {
            frameBuffer.color = half4(
                half3(33.0f / 255.0f),
                half(1.0f));
            return frameBuffer;
        }
        const float2 planeUv =
            float2(
                (pixel.x - 475.0f) / 250.0f,
                1.0f - (pixel.y - 125.0f) / 250.0f);
        frameBuffer.color =
            half4(webgpuTexturegradShade(resources, planeUv));
        return frameBuffer;
    }
};

/** Owns both r185 comparison Scenes and their shared immutable texture. */
class WebgpuTexturegradRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> uvGrid;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebgpuTexturegradUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler uvGridSampler;
    BindGroup<WebgpuTexturegradResources> resources;
    RenderClass<WebgpuTexturegradWebgpuMainPass> webgpuPass;
    RenderClass<WebgpuTexturegradWebglMainPass> webglPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the explicit-gradient sampler and deterministic uniform buffer. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer =
            device->createBuffer("WebgpuTexturegradUniforms", 1u);
        uvGridSampler = device->createSampler({
            .label = "WebgpuTexturegradSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 10.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the exact deterministic two-panel output extent. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuTexturegradOutput",
            width,
            height,
            1u);
    }

    /** Uploads the locked UV-grid mip chain and creates both Scene passes. */
    void configureScene(
        const eastl::vector<eastl::vector<uint8_t>> &mipLevels,
        uint textureWidth,
        uint textureHeight)
    {
        uvGrid = device->createTexture(
            "WebgpuTexturegradUvGrid",
            textureWidth,
            textureHeight,
            1u,
            uint(mipLevels.size()));
        for (uint mip = 0u; mip < uint(mipLevels.size()); mip += 1u)
        {
            graphicsQueue->writeTexture(
                uvGrid,
                mipLevels[mip].data(),
                uint64_t(mipLevels[mip].size()),
                mip);
        }
        resources =
            device->createBindGroup<WebgpuTexturegradResources>(
                uniformBuffer,
                uvGrid->createView(),
                uvGridSampler);
        webgpuPass =
            device->createRenderClass<WebgpuTexturegradWebgpuMainPass>(
                resources);
        webglPass =
            device->createRenderClass<WebgpuTexturegradWebglMainPass>(
                resources);
        graphicsQueue->submit();
    }

    /** Advances fixed 60 Hz time and renders both independent Scenes. */
    void render() override
    {
        const WebgpuTexturegradUniforms uniforms = {
            float4(
                float(frameIndex) / 60.0f,
                float(width),
                float(height),
                0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebgpuTexturegradFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            33.0 / 255.0,
            33.0 / 255.0,
            33.0 / 255.0,
            1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTexturegradScenes",
                frameBuffer,
                webgpuPass(3u, 1u, 0u, 0u),
                webglPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        frameIndex += 1u;
    }

    /** Returns the final DSL-owned RGBA8 texture. */
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

    /** Releases all texture and buffer storage owned by this renderer. */
    void destroy() override
    {
        device->freeTexture(uvGrid);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
