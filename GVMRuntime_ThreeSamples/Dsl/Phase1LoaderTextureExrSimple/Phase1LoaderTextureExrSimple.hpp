#ifndef GVM_THREE_PHASE1_LOADER_TEXTURE_EXR_SIMPLE_HPP
#define GVM_THREE_PHASE1_LOADER_TEXTURE_EXR_SIMPLE_HPP

#include "Phase1LoaderTextureExrSimpleData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the linear EXR texture and per-scenario exposure. */
struct WebglLoaderTextureExrResources final : public IBindGroup
{
    /** Declares the complete private Reinhard material resource set. */
    constructor(
        UniformBuffer<WebglLoaderTextureExrUniforms> uniforms [[Binding0]],
        Texture2D<float4> image [[Binding1]],
        Sampler imageSampler [[Binding2]])
    {
    }
};

/** Carries one projected plane coordinate and linear texture coordinate. */
struct WebglLoaderTextureExrVertexOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebglLoaderTextureExrFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one non-negative linear channel through Three's output transfer. */
float webglLoaderTextureExrLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the canonical EXR plane with Reinhard exposure and output conversion. */
class WebglLoaderTextureExrQuadPass final : public IRenderClass
{
public:
    /** Enables a deterministic uncullled opaque indexed plane. */
    constructor(
        BindGroup<WebglLoaderTextureExrResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one already-projected plane vertex. */
    WebglLoaderTextureExrVertexOutput vertex(
        WebglLoaderTextureExrVertex inputValue [[VertexInput0]])
    {
        WebglLoaderTextureExrVertexOutput outputValue;
        outputValue.position = float4(inputValue.position, 0.0f, 1.0f);
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        return outputValue;
    }

    /** Samples linear half-float RGB and applies exact Reinhard tone mapping. */
    WebglLoaderTextureExrFrameBuffer fragment(
        WebglLoaderTextureExrVertexOutput inputValue)
    {
        float3 mapped = float3(0.0f);
        const float2 quadMinimum = float2(275.0f, 62.5f);
        const float2 quadMaximum = float2(525.0f, 437.5f);
        if (inputValue.position.x >= quadMinimum.x &&
            inputValue.position.x < quadMaximum.x &&
            inputValue.position.y > quadMinimum.y &&
            inputValue.position.y <= quadMaximum.y)
        {
            const float2 uv = float2(
                (inputValue.position.x - quadMinimum.x) / 250.0f,
                (inputValue.position.y - quadMinimum.y) / 375.0f);
            const float3 source = resources->image->sample(
                resources->imageSampler,
                uv).xyz;
            const float3 exposed =
                source * resources->uniforms->exposureAndPadding.x;
            mapped = exposed / (float3(1.0f) + exposed);
        }
        WebglLoaderTextureExrFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglLoaderTextureExrLinearToSrgb(mapped.x)),
            half(webglLoaderTextureExrLinearToSrgb(mapped.y)),
            half(webglLoaderTextureExrLinearToSrgb(mapped.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated ordinary RenderClass for webgl_loader_texture_exr. */
class Phase1LoaderTextureExrSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglLoaderTextureExrVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglLoaderTextureExrUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> imageTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler imageSampler;
    BindGroup<WebglLoaderTextureExrResources> resources;
    RenderClass<WebglLoaderTextureExrQuadPass> quadPass;

public:
    /** Stores the generated device and creates the authored linear sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        imageSampler = device->createSampler({
            .label = "WebglLoaderTextureExrSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the canonical single-sample output target. */
    void configureOutput(uint width, uint height)
    {
        outputTexture = device->createTexture(
            "WebglLoaderTextureExrOutput", width, height, 1u);
    }

    /** Uploads the exact display plane, linear RGBA16F texels, and exposure. */
    void configureScene(
        const eastl::vector<WebglLoaderTextureExrVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint16_t> &pixels,
        uint imageWidth,
        uint imageHeight,
        WebglLoaderTextureExrUniforms uniforms)
    {
        vertexBuffer = device->createBuffer(
            "WebglLoaderTextureExrVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebglLoaderTextureExrIndices", uint(indices.size()));
        uniformBuffer = device->createBuffer(
            "WebglLoaderTextureExrUniforms", 1u);
        imageTexture = device->createTexture(
            "WebglLoaderTextureExrImage", imageWidth, imageHeight, 1u);
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                          uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->writeTexture(imageTexture, pixels.data(),
                           uint64_t(pixels.size()) * sizeof(uint16_t), 0u)
            ->submit();
        resources = device->createBindGroup<WebglLoaderTextureExrResources>(
            uniformBuffer, imageTexture->createView(), imageSampler);
        quadPass = device->createRenderClass<WebglLoaderTextureExrQuadPass>(resources);
    }

    /** Draws and presents the one EXR plane. */
    void render() override
    {
        WebglLoaderTextureExrFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglLoaderTextureExrScene",
                frameBuffer,
                quadPass->setVertexBuffer(vertexBuffer),
                quadPass->setIndexBuffer(indexBuffer),
                quadPass(6u, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the canonical output width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the canonical output height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases all dedicated EXR scene resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(imageTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
