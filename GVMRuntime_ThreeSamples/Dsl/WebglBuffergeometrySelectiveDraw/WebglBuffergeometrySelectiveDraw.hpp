#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_SELECTIVE_DRAW_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_SELECTIVE_DRAW_HPP

#include "UGL.h"
#include "WebglBuffergeometrySelectiveDrawVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometrySelectiveDrawSourceLineCount = 20000u;
static const uint WebglBuffergeometrySelectiveDrawSourceVertexCount = 40000u;
static const uint WebglBuffergeometrySelectiveDrawExpandedVertexCount = 40000u;

/** Stores the target-frame transforms for the ordinary single-sample line pass. */
struct WebglBuffergeometrySelectiveDrawUniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Binds the fixed transform and sample state used by the selective line pass. */
struct WebglBuffergeometrySelectiveDrawBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding shared by both UGLC pipelines. */
    constructor(UniformBuffer<WebglBuffergeometrySelectiveDrawUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the interpolated custom color and visibility attributes to the fragment stage. */
struct WebglBuffergeometrySelectiveDrawVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float visible [[Attribute1]];
};

/** Defines the single-sample color and depth target. */
struct WebglBuffergeometrySelectiveDrawFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces one r185 LineSegments draw with the existing line-list topology and discard. */
class WebglBuffergeometrySelectiveDrawMainPass final : public IRenderClass
{
public:
    /** Configures opaque line segments with Three's default depth semantics. */
    constructor(BindGroup<WebglBuffergeometrySelectiveDrawBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one source line endpoint through the existing line-list path. */
    WebglBuffergeometrySelectiveDrawVertexOutput vertex(
        WebglBuffergeometrySelectiveDrawVertex inputValue [[VertexInput0]])
    {
        float4 startClip = mul(
            bindGroup->uniforms->projectionMatrix,
            mul(
                bindGroup->uniforms->modelViewMatrix,
                float4(inputValue.startPosition, 1.0f)));
        float4 endClip = mul(
            bindGroup->uniforms->projectionMatrix,
            mul(
                bindGroup->uniforms->modelViewMatrix,
                float4(inputValue.endPosition, 1.0f)));
        startClip.y = -startClip.y;
        endClip.y = -endClip.y;
        startClip.z = (startClip.z + startClip.w) * 0.5f;
        endClip.z = (endClip.z + endClip.w) * 0.5f;
        const float endpoint = inputValue.lineCoordinate.x;
        const float4 clipPosition = lerp(startClip, endClip, endpoint);
        WebglBuffergeometrySelectiveDrawVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = lerp(
            inputValue.startColor,
            inputValue.endColor,
            endpoint);
        outputValue.visible = inputValue.visible;
        return outputValue;
    }

    /** Discards hidden segments and writes the upstream custom vertex color unchanged. */
    WebglBuffergeometrySelectiveDrawFrameBuffer fragment(
        WebglBuffergeometrySelectiveDrawVertexOutput inputValue)
    {
        if (inputValue.visible <= 0.0f)
        {
            discard_fragment();
        }
        WebglBuffergeometrySelectiveDrawFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the selective line endpoint payload and all private DSL rendering passes. */
class WebglBuffergeometrySelectiveDrawRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometrySelectiveDrawVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<WebglBuffergeometrySelectiveDrawUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer0;
    BindGroup<WebglBuffergeometrySelectiveDrawBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometrySelectiveDrawMainPass> mainPass0;
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
    bool sceneRendered = false;

public:
    /** Creates the ordinary standalone buffers through current DSL interfaces. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometrySelectiveDrawVertices",
            WebglBuffergeometrySelectiveDrawExpandedVertexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglBuffergeometrySelectiveDrawUniforms0", 1u);
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometrySelectiveDrawOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometrySelectiveDrawDepth", width, height, 1u);
    }

    /** Uploads all line endpoints and the target-frame matrix before the first draw. */
    void configureScene(
        const eastl::vector<WebglBuffergeometrySelectiveDrawVertex> &vertices,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix)
    {
        WebglBuffergeometrySelectiveDrawUniforms uniforms0;
        uniforms0.projectionMatrix = projectionMatrix;
        uniforms0.modelViewMatrix = modelViewMatrix;
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglBuffergeometrySelectiveDrawVertex))
            ->writeBuffer(
                BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->submit();

        bindGroup0 =
            device->createBindGroup<WebglBuffergeometrySelectiveDrawBindGroup>(
                uniformBuffer0);
        mainPass0 =
            device->createRenderClass<WebglBuffergeometrySelectiveDrawMainPass>(
                bindGroup0);
    }

    /** Uploads the replay-updated visibility payload through the generated interface. */
    void updateVisibilityVertices(
        const eastl::vector<WebglBuffergeometrySelectiveDrawVertex> &vertices)
    {
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglBuffergeometrySelectiveDrawVertex))
            ->submit();
    }

    /** Rasterizes line segments directly into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (!sceneRendered)
        {
            WebglBuffergeometrySelectiveDrawFrameBuffer frameBuffer0;
            frameBuffer0.color = outputTexture->createView();
            frameBuffer0.color.loadOp = LoadOp::Clear;
            frameBuffer0.color.storeOp = StoreOp::Store;
            frameBuffer0.color.clearValue = {0.0, 0.0, 0.0, 1.0};
            frameBuffer0.depth = depthTexture->createView();
            frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
            frameBuffer0.depth.depthStoreOp = StoreOp::Store;
            frameBuffer0.depth.depthClearValue = 1.0f;

            graphicsQueue
                ->renderPass(
                    "WebglBuffergeometrySelectiveDrawSample0",
                    frameBuffer0,
                    mainPass0->setVertexBuffer(vertexBuffer),
                    mainPass0(
                        WebglBuffergeometrySelectiveDrawExpandedVertexCount,
                        1u,
                        0u,
                        0u))
                ->submit();
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
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
