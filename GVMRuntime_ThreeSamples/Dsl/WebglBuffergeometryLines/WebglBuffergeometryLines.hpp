#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_HPP

#include "UGL.h"
#include "WebglBuffergeometryLinesVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryLinesVertexCount = 10000u;

/** Stores the fixed-frame transform and absolute morph-target influence. */
struct WebglBuffergeometryLinesUniforms
{
    float4x4 modelViewProjection;
    float4 morphWeightAndReserved;
};

/** Binds the transform and morph state used by the line-strip material. */
struct WebglBuffergeometryLinesBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding shared by both UGLC pipelines. */
    constructor(UniformBuffer<WebglBuffergeometryLinesUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the transformed position and vertex color into the fragment stage. */
struct WebglBuffergeometryLinesVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
};

/** Defines the RGBA8 color and depth targets used by the line Scene pass. */
struct WebglBuffergeometryLinesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's output transfer constants. */
inline float webglBuffergeometryLinesLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces the single r185 Line object with the existing LineStrip topology. */
class WebglBuffergeometryLinesMainPass final : public IRenderClass
{
public:
    /** Configures opaque vertex-color line rendering with default Three depth semantics. */
    constructor(BindGroup<WebglBuffergeometryLinesBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineStrip);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Blends the absolute morph target and applies the fixed-frame object transform. */
    WebglBuffergeometryLinesVertexOutput vertex(
        WebglBuffergeometryLinesVertex inputValue [[VertexInput0]])
    {
        const float morphWeight = bindGroup->uniforms->morphWeightAndReserved.x;
        const float3 position = lerp(
            inputValue.position,
            inputValue.morphPosition,
            morphWeight);

        WebglBuffergeometryLinesVertexOutput outputValue;
        outputValue.position = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(position, 1.0f));
        outputValue.color = inputValue.color;
        return outputValue;
    }

    /** Writes the interpolated LineBasicMaterial vertex color with opaque alpha. */
    WebglBuffergeometryLinesFrameBuffer fragment(
        WebglBuffergeometryLinesVertexOutput inputValue)
    {
        WebglBuffergeometryLinesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglBuffergeometryLinesLinearToSrgb(inputValue.color.x)),
            half(webglBuffergeometryLinesLinearToSrgb(inputValue.color.y)),
            half(webglBuffergeometryLinesLinearToSrgb(inputValue.color.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the standalone line buffer, fixed-frame state, and deterministic output. */
class WebglBuffergeometryLinesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryLinesVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglBuffergeometryLinesUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglBuffergeometryLinesBindGroup> bindGroup;
    RenderClass<WebglBuffergeometryLinesMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates all GPU resources through the existing DSL resource interfaces. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometryLinesVertices",
            WebglBuffergeometryLinesVertexCount);
        uniformBuffer = device->createBuffer("WebglBuffergeometryLinesUniforms", 1u);
        bindGroup = device->createBindGroup<WebglBuffergeometryLinesBindGroup>(
            uniformBuffer);
        mainPass = device->createRenderClass<WebglBuffergeometryLinesMainPass>(
            bindGroup);
    }

    /** Allocates the fixed host-requested RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryLinesRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryLinesDepth32", width, height, 1u);
    }

    /** Uploads C++-prepared line vertices and the locked capture-frame state. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryLinesVertex> &vertexData,
        float4x4 modelViewProjection,
        float morphWeight)
    {
        WebglBuffergeometryLinesUniforms uniforms;
        uniforms.modelViewProjection = modelViewProjection;
        uniforms.morphWeightAndReserved = float4(morphWeight, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertexData.data(),
                uint64_t(WebglBuffergeometryLinesVertexCount) *
                    sizeof(WebglBuffergeometryLinesVertex))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
    }

    /** Draws the one non-instanced line object through one explicit Scene pass. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryLinesFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "main-line-strip",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass(WebglBuffergeometryLinesVertexCount, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output for deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the standalone geometry, uniform, color, and depth resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
