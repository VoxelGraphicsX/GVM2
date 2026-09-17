#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_RAWSHADER_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_RAWSHADER_HPP

#include "UGL.h"
#include "WebglBuffergeometryRawshaderVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryRawshaderVertexCount = 600u;

/** Stores the fixed Three-compatible transform and private shader time. */
struct WebglBuffergeometryRawshaderUniforms
{
    float4x4 modelViewProjection;
    float4 timeAndReserved;
};

/** Binds the frame state consumed by the private raw-shader equivalent. */
struct WebglBuffergeometryRawshaderBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding shared by both UGLC pipelines. */
    constructor(UniformBuffer<WebglBuffergeometryRawshaderUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries raw object position and normalized color to the fragment stage. */
struct WebglBuffergeometryRawshaderVertexOutput
{
    float4 position [[Position]];
    float3 objectPosition [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Defines the RGBA8 color and depth targets used by the single Scene pass. */
struct WebglBuffergeometryRawshaderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Re-expresses the r185 RawShaderMaterial as one ordinary DSL RenderClass. */
class WebglBuffergeometryRawshaderMainPass final : public IRenderClass
{
public:
    /** Configures DoubleSide, normal alpha blending, and default Three depth semantics. */
    constructor(BindGroup<WebglBuffergeometryRawshaderBindGroup> bindGroup [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Unpacks the Uint8 color and applies the deterministic model-view-projection matrix. */
    WebglBuffergeometryRawshaderVertexOutput vertex(
        WebglBuffergeometryRawshaderVertex inputValue [[VertexInput0]])
    {
        const float inverseByteMaximum = 1.0f / 255.0f;
        const uint packedColor = inputValue.colorRgba8;

        WebglBuffergeometryRawshaderVertexOutput outputValue;
        outputValue.position = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        outputValue.objectPosition = inputValue.position;
        outputValue.color = float4(
                                float(packedColor & 0xffu),
                                float((packedColor >> 8u) & 0xffu),
                                float((packedColor >> 16u) & 0xffu),
                                float((packedColor >> 24u) & 0xffu)) *
                            inverseByteMaximum;
        return outputValue;
    }

    /** Applies the original position/time red modulation and writes transparent RGBA. */
    WebglBuffergeometryRawshaderFrameBuffer fragment(
        WebglBuffergeometryRawshaderVertexOutput inputValue)
    {
        float4 color = inputValue.color;
        color.x += sin(
                       inputValue.objectPosition.x * 10.0f +
                       bindGroup->uniforms->timeAndReserved.x) *
                   0.5f;

        WebglBuffergeometryRawshaderFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the standalone geometry, private DSL material, and deterministic output. */
class WebglBuffergeometryRawshaderRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryRawshaderVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglBuffergeometryRawshaderUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglBuffergeometryRawshaderBindGroup> bindGroup;
    RenderClass<WebglBuffergeometryRawshaderMainPass> mainPass;
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
            "WebglBuffergeometryRawshaderVertices",
            WebglBuffergeometryRawshaderVertexCount);
        uniformBuffer = device->createBuffer("WebglBuffergeometryRawshaderUniforms", 1u);
        bindGroup = device->createBindGroup<WebglBuffergeometryRawshaderBindGroup>(
            uniformBuffer);
        mainPass = device->createRenderClass<WebglBuffergeometryRawshaderMainPass>(
            bindGroup);
    }

    /** Allocates the fixed host-requested RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryRawshaderRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryRawshaderDepth32", width, height, 1u);
    }

    /** Uploads the C++-prepared vertices and fixed capture-frame state through DSL queue work. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryRawshaderVertex> &vertexData,
        float4x4 modelViewProjection,
        float shaderTime)
    {
        WebglBuffergeometryRawshaderUniforms uniforms;
        uniforms.modelViewProjection = modelViewProjection;
        uniforms.timeAndReserved = float4(shaderTime, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertexData.data(),
                uint64_t(WebglBuffergeometryRawshaderVertexCount) *
                    sizeof(WebglBuffergeometryRawshaderVertex))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
    }

    /** Draws the single non-indexed object through the locked main-private-shader pass. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryRawshaderFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            16.0 / 255.0,
            16.0 / 255.0,
            16.0 / 255.0,
            1.0,
        };
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "main-private-shader",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass(WebglBuffergeometryRawshaderVertexCount, 1u, 0u, 0u))
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
