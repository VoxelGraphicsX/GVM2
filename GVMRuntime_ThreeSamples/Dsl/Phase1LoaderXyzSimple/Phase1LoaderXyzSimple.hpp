#ifndef GVM_THREE_PHASE1_LOADER_XYZ_SIMPLE_HPP
#define GVM_THREE_PHASE1_LOADER_XYZ_SIMPLE_HPP

#include "UGL.h"
#include "WebglLoaderXyzVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglLoaderXyzPointCount = 201u;
static const uint WebglLoaderXyzVertexCount = WebglLoaderXyzPointCount * 4u;
static const uint WebglLoaderXyzIndexCount = WebglLoaderXyzPointCount * 6u;
static const uint WebglLoaderXyzOutputWidth = 800u;
static const uint WebglLoaderXyzOutputHeight = 500u;

/** Stores Three's camera/object transforms and one deterministic coverage-sample location. */
struct WebglLoaderXyzUniforms
{
    float4x4 modelView;
    float4x4 projection;
    float4 samplePositionAndReserved;
};

/** Binds the fixed-frame transform used by one single-sample Scene invocation. */
struct WebglLoaderXyzBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding shared by both UGLC pipelines. */
    constructor(UniformBuffer<WebglLoaderXyzUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries one expanded point corner into the fragment stage. */
struct WebglLoaderXyzVertexOutput
{
    float4 position [[Position]];
};

/** Defines the deterministic single-sample color and depth target. */
struct WebglLoaderXyzFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces the example's single uncolored Points object with triangle-list billboards. */
class WebglLoaderXyzPointPass final : public IRenderClass
{
public:
    /** Preserves opaque PointsMaterial depth semantics while disabling triangle culling. */
    constructor(BindGroup<WebglLoaderXyzBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies object/camera transforms, perspective point sizing, and sample jitter. */
    WebglLoaderXyzVertexOutput vertex(WebglLoaderXyzVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.position, 1.0f));
        float4 clipPosition = mul(bindGroup->uniforms->projection, viewPosition);

        const float pointSizePixels = 25.0f / -viewPosition.z;
        clipPosition.x +=
            inputValue.corner.x * pointSizePixels /
            float(WebglLoaderXyzOutputWidth) * clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * pointSizePixels /
            float(WebglLoaderXyzOutputHeight) * clipPosition.w;

        const float2 samplePosition =
            bindGroup->uniforms->samplePositionAndReserved.xy;
        clipPosition.x +=
            (1.0f - 2.0f * samplePosition.x) /
            float(WebglLoaderXyzOutputWidth) * clipPosition.w;
        clipPosition.y +=
            (2.0f * samplePosition.y - 1.0f) /
            float(WebglLoaderXyzOutputHeight) * clipPosition.w;

        WebglLoaderXyzVertexOutput outputValue;
        outputValue.position = clipPosition;
        return outputValue;
    }

    /** Writes the default white PointsMaterial color with opaque alpha. */
    WebglLoaderXyzFrameBuffer fragment(WebglLoaderXyzVertexOutput inputValue)
    {
        WebglLoaderXyzFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(1.0f), half(1.0f), half(1.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the standalone point expansion and deterministic single-sample output. */
class Phase1LoaderXyzSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglLoaderXyzVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglLoaderXyzUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglLoaderXyzBindGroup> bindGroup0;
    RenderClass<WebglLoaderXyzPointPass> pointPass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;

public:
    /** Creates all fixed-size standalone buffers used by this one-object sample. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglLoaderXyzVertices", WebglLoaderXyzVertexCount);
        indexBuffer = device->createBuffer(
            "WebglLoaderXyzIndices", WebglLoaderXyzIndexCount);
        uniformBuffer0 = device->createBuffer("WebglLoaderXyzUniforms0", 1u);
    }

    /** Allocates the locked single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglLoaderXyzOutputWidth ||
            height != WebglLoaderXyzOutputHeight)
        {
            return;
        }
        outputTexture = device->createTexture("WebglLoaderXyzOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglLoaderXyzDepth", width, height, 1u);
    }

    /** Uploads parsed XYZ geometry and creates the single-sample state binding. */
    void configureScene(
        const eastl::vector<WebglLoaderXyzVertex> &vertices,
        const eastl::vector<uint> &indices,
        float4x4 modelView,
        float4x4 projection)
    {
        WebglLoaderXyzUniforms uniforms0;
        uniforms0.modelView = modelView;
        uniforms0.projection = projection;
        uniforms0.samplePositionAndReserved = float4(0.5f, 0.5f, 0.0f, 0.0f);

        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglLoaderXyzVertexCount) * sizeof(WebglLoaderXyzVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglLoaderXyzIndexCount) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->submit();

        bindGroup0 = device->createBindGroup<WebglLoaderXyzBindGroup>(uniformBuffer0);
        pointPass0 = device->createRenderClass<WebglLoaderXyzPointPass>(bindGroup0);
    }

    /** Rasterizes the expanded points directly into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglLoaderXyzFrameBuffer frameBuffer0;
        frameBuffer0.color = outputTexture->createView();
        frameBuffer0.depth = depthTexture->createView();
        frameBuffer0.color.loadOp = LoadOp::Clear;
        frameBuffer0.color.storeOp = StoreOp::Store;
        frameBuffer0.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer0.depth.depthStoreOp = StoreOp::Store;
        frameBuffer0.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglLoaderXyzMain",
                frameBuffer0,
                pointPass0->setVertexBuffer(vertexBuffer),
                pointPass0->setIndexBuffer(indexBuffer),
                pointPass0(WebglLoaderXyzIndexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 texture used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the locked output width. */
    uint getReadbackWidth() const
    {
        return WebglLoaderXyzOutputWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return WebglLoaderXyzOutputHeight;
    }

    /** Releases every standalone buffer and private texture owned by this sample. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
