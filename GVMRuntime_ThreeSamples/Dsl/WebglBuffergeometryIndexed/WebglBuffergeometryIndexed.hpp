#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_INDEXED_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_INDEXED_HPP

#include "UGL.h"
#include "WebglBuffergeometryIndexedVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryIndexedVertexCount = 121u;
static const uint WebglBuffergeometryIndexedIndexCount = 600u;
static const uint WebglBuffergeometryIndexedWireVertexCount = 1200u;

/** Stores the target-frame transform for the ordinary single-sample draw. */
struct WebglBuffergeometryIndexedUniforms
{
    float4x4 modelViewProjection;
};

/** Binds the fixed transform and sample state used by both Scene materials. */
struct WebglBuffergeometryIndexedBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding shared by both UGLC pipelines. */
    constructor(UniformBuffer<WebglBuffergeometryIndexedUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the interpolated linear vertex color to the fragment stage. */
struct WebglBuffergeometryIndexedVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
};

/** Defines the single-sample color and depth target. */
struct WebglBuffergeometryIndexedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's exact output constants. */
inline float webglBuffergeometryIndexedLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Returns the hemisphere-only r185 MeshPhong vertex-color result. */
inline float4 webglBuffergeometryIndexedShade(
    float3 color)
{
    const float hemisphereIrradianceTimesInversePi =
        0.954929658551372f;
    const float3 linearColor = color * hemisphereIrradianceTimesInversePi;
    return float4(
        half(webglBuffergeometryIndexedLinearToSrgb(linearColor.x)),
        half(webglBuffergeometryIndexedLinearToSrgb(linearColor.y)),
        half(webglBuffergeometryIndexedLinearToSrgb(linearColor.z)),
        half(1.0f));
}

/** Reproduces the filled indexed DoubleSide MeshPhongMaterial draw. */
class WebglBuffergeometryIndexedFilledPass final : public IRenderClass
{
public:
    /** Configures ordinary indexed triangles with Three's opaque depth semantics. */
    constructor(BindGroup<WebglBuffergeometryIndexedBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one of the 121 shared vertices and forwards its linear color. */
    WebglBuffergeometryIndexedVertexOutput vertex(
        WebglBuffergeometryIndexedVertex inputValue [[VertexInput0]])
    {
        WebglBuffergeometryIndexedVertexOutput outputValue;
        outputValue.position = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        outputValue.color = inputValue.color;
        return outputValue;
    }

    /** Applies Three's constant white hemisphere indirect diffuse and output transfer. */
    WebglBuffergeometryIndexedFrameBuffer fragment(
        WebglBuffergeometryIndexedVertexOutput inputValue)
    {
        WebglBuffergeometryIndexedFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            webglBuffergeometryIndexedShade(inputValue.color));
        return frameBuffer;
    }
};

/** Reproduces the GUI wireframe mode with the existing line-list topology. */
class WebglBuffergeometryIndexedWireframePass final : public IRenderClass
{
public:
    /** Configures opaque edges through the backend-native line-list path. */
    constructor(BindGroup<WebglBuffergeometryIndexedBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Emits one source edge endpoint through the backend's existing line-list path. */
    WebglBuffergeometryIndexedVertexOutput vertex(
        WebglBuffergeometryIndexedWireVertex inputValue [[VertexInput0]])
    {
        const float4 startClip = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.startPosition, 1.0f));
        const float4 endClip = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.endPosition, 1.0f));
        const float endpoint = inputValue.lineCoordinate.x;
        const float4 clipPosition = lerp(startClip, endClip, endpoint);
        WebglBuffergeometryIndexedVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = lerp(
            inputValue.startColor,
            inputValue.endColor,
            endpoint);
        return outputValue;
    }

    /** Applies the same private MeshPhong lighting to each line fragment. */
    WebglBuffergeometryIndexedFrameBuffer fragment(
        WebglBuffergeometryIndexedVertexOutput inputValue)
    {
        WebglBuffergeometryIndexedFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            webglBuffergeometryIndexedShade(inputValue.color));
        return frameBuffer;
    }
};

/** Owns the standalone indexed grid, native wire edges, and all private DSL passes. */
class WebglBuffergeometryIndexedRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryIndexedVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglBuffergeometryIndexedWireVertex, BufferUsage<Vertex, CopyDst>> wireVertexBuffer;
    Buffer<WebglBuffergeometryIndexedUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglBuffergeometryIndexedBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometryIndexedFilledPass> filledPass0;
    RenderClass<WebglBuffergeometryIndexedWireframePass> wireframePass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    bool wireframeEnabled = false;
    bool sceneRendered = false;

public:
    /** Creates ordinary standalone buffers through the current DSL interfaces. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometryIndexedVertices",
            WebglBuffergeometryIndexedVertexCount);
        indexBuffer = device->createBuffer(
            "WebglBuffergeometryIndexedIndices",
            WebglBuffergeometryIndexedIndexCount);
        wireVertexBuffer = device->createBuffer(
            "WebglBuffergeometryIndexedWireVertices",
            WebglBuffergeometryIndexedWireVertexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglBuffergeometryIndexedUniforms0", 1u);
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryIndexedOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryIndexedDepth", width, height, 1u);
    }

    /** Uploads the 121/600 indexed source and immutable wire endpoint payload. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryIndexedVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<WebglBuffergeometryIndexedWireVertex> &wireVertices,
        float4x4 modelViewProjection,
        bool useWireframe)
    {
        wireframeEnabled = useWireframe;
        WebglBuffergeometryIndexedUniforms uniforms0;
        uniforms0.modelViewProjection = modelViewProjection;
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglBuffergeometryIndexedVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(wireVertexBuffer),
                wireVertices.data(),
                uint64_t(wireVertices.size()) *
                    sizeof(WebglBuffergeometryIndexedWireVertex))
            ->writeBuffer(BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->submit();

        bindGroup0 = device->createBindGroup<WebglBuffergeometryIndexedBindGroup>(
            uniformBuffer0);
        filledPass0 = device->createRenderClass<WebglBuffergeometryIndexedFilledPass>(
            bindGroup0);
        wireframePass0 =
            device->createRenderClass<WebglBuffergeometryIndexedWireframePass>(
                bindGroup0);
    }

    /** Rasterizes the selected material mode into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (!sceneRendered)
        {
            WebglBuffergeometryIndexedFrameBuffer frameBuffer0;
            frameBuffer0.color = outputTexture->createView();
            frameBuffer0.color.loadOp = LoadOp::Clear;
            frameBuffer0.color.storeOp = StoreOp::Store;
            frameBuffer0.color.clearValue = {
                5.0 / 255.0, 5.0 / 255.0, 5.0 / 255.0, 1.0};
            frameBuffer0.depth = depthTexture->createView();
            frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
            frameBuffer0.depth.depthStoreOp = StoreOp::Store;
            frameBuffer0.depth.depthClearValue = 1.0f;

            if (wireframeEnabled)
            {
                graphicsQueue
                    ->renderPass(
                        "WebglBuffergeometryIndexedWireSample0",
                        frameBuffer0,
                        wireframePass0->setVertexBuffer(wireVertexBuffer),
                        wireframePass0(
                            WebglBuffergeometryIndexedWireVertexCount,
                            1u,
                            0u,
                            0u))
                    ->submit();
            }
            else
            {
                graphicsQueue
                    ->renderPass(
                        "WebglBuffergeometryIndexedFilledSample0",
                        frameBuffer0,
                        filledPass0->setVertexBuffer(vertexBuffer),
                        filledPass0->setIndexBuffer(indexBuffer),
                        filledPass0(
                            WebglBuffergeometryIndexedIndexCount,
                            1u,
                            0u,
                            0,
                            0u))
                    ->submit();
            }
            sceneRendered = true;
        }

        graphicsQueue
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 output for deterministic readback. */
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
        return readbackWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases all standalone buffers and single-sample targets. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(wireVertexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
