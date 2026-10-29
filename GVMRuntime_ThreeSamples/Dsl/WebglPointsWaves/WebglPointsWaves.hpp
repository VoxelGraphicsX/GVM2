#ifndef GVM_THREE_WEBGL_POINTS_WAVES_HPP
#define GVM_THREE_WEBGL_POINTS_WAVES_HPP

#include "UGL.h"
#include "WebglPointsWavesData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglPointsWavesAmountX = 50u;
static const uint WebglPointsWavesAmountY = 50u;
static const uint WebglPointsWavesLogicalPointCount =
    WebglPointsWavesAmountX * WebglPointsWavesAmountY;
static const uint WebglPointsWavesVertexCount =
    WebglPointsWavesLogicalPointCount * 3u;
static const uint WebglPointsWavesIndexCount =
    WebglPointsWavesLogicalPointCount * 3u;

/** Binds the only dynamic state consumed by the private wave-point shader. */
struct WebglPointsWavesSceneResources final : public IBindGroup
{
    /** Declares one transform and phase uniform buffer without standalone GPU logic. */
    constructor(UniformBuffer<WebglPointsWavesUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries one expanded PointCoord and quantized size to the sample coverage test. */
struct WebglPointsWavesVertexOutput
{
    float4 position [[Position]];
    float2 pointCoord [[Attribute0]];
    float pointSize [[Attribute1]];
};

/** Defines the requested-size Scene color and depth attachments. */
struct WebglPointsWavesSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the one logical Points object as noninstanced expanded triangle corners. */
class WebglPointsWavesMainPass final : public IRenderClass
{
public:
    /** Reproduces the opaque ShaderMaterial without native point topology. */
    constructor(BindGroup<WebglPointsWavesSceneResources> sceneResources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Evaluates the r185 CPU wave equations and exact perspective point-size equation. */
    WebglPointsWavesVertexOutput vertex(
        WebglPointsWavesVertex inputValue [[VertexInput0]])
    {
        const float gridX = inputValue.positionAndGridX.w;
        const float gridY = inputValue.cornerAndGridY.z;
        const float phase = sceneResources->uniforms->phaseAndViewport.x;
        const float waveX = sin((gridX + phase) * 0.3f);
        const float waveY = sin((gridY + phase) * 0.5f);
        const float3 worldPosition = float3(
            inputValue.positionAndGridX.x,
            -(waveX + waveY) * 50.0f,
            inputValue.positionAndGridX.z);
        const float scale =
            (waveX + 1.0f) * 20.0f +
            (waveY + 1.0f) * 20.0f;
        const float4 viewPosition = mul(
            sceneResources->uniforms->viewMatrix,
            float4(worldPosition, 1.0f));
        float pointSize = 0.0f;
        if (viewPosition.z <= -1.0f)
        {
            const float rawPointSize =
                scale * (300.0f / -viewPosition.z);
            pointSize =
                floor(clamp(rawPointSize, 1.0f, 511.0f) * 16.0f + 0.5f) /
                16.0f;
        }
        float4 clipPosition = mul(
            sceneResources->uniforms->projectionMatrix,
            viewPosition);
        const float2 viewport =
            sceneResources->uniforms->phaseAndViewport.yz;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        const float2 sampleOffset =
            sceneResources->uniforms->sampleOffsetAndPadding.xy;
        clipPosition.x -=
            sampleOffset.x * 2.0f /
            viewport.x * clipPosition.w;
        clipPosition.y +=
            sampleOffset.y * 2.0f /
            viewport.y * clipPosition.w;
        clipPosition.x +=
            inputValue.cornerAndGridY.x * pointSize /
            viewport.x * clipPosition.w;
        clipPosition.y -=
            inputValue.cornerAndGridY.y * pointSize /
            viewport.y * clipPosition.w;
        if (viewPosition.z > -1.0f)
        {
            clipPosition = float4(2.0f, 2.0f, 2.0f, 1.0f);
        }
        WebglPointsWavesVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.pointCoord =
            inputValue.cornerAndGridY.xy * 0.5f + 0.5f;
        outputValue.pointSize = pointSize;
        return outputValue;
    }

    /** Applies native square coverage and pixel-center disc clipping. */
    WebglPointsWavesSceneFrameBuffer fragment(
        WebglPointsWavesVertexOutput inputValue)
    {
        if (inputValue.pointCoord.x < 0.0f ||
            inputValue.pointCoord.x > 1.0f ||
            inputValue.pointCoord.y < 0.0f ||
            inputValue.pointCoord.y > 1.0f)
        {
            discard_fragment();
        }
        const float2 sampleOffset =
            sceneResources->uniforms->sampleOffsetAndPadding.xy;
        const float2 pixelCenterPointCoord =
            inputValue.pointCoord - sampleOffset / inputValue.pointSize;
        clip(0.475f - length(
            pixelCenterPointCoord - float2(0.5f)));
        WebglPointsWavesSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(1.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Owns all DSL-created GPU resources for the webgl_points_waves compatibility sample. */
class WebglPointsWavesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglPointsWavesVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglPointsWavesUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglPointsWavesSceneResources> sceneResources0;
    RenderClass<WebglPointsWavesMainPass> mainPass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates fixed ordinary geometry and uniform buffers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglPointsWavesVertices",
            WebglPointsWavesVertexCount);
        indexBuffer = device->createBuffer(
            "WebglPointsWavesIndices",
            WebglPointsWavesIndexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglPointsWavesUniforms0",
            1u);
    }

    /** Allocates matching requested-size Scene and final readback targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture(
            "WebglPointsWavesOutputRGBA8",
            width,
            height,
            1u);
        sceneDepth = device->createTexture(
            "WebglPointsWavesSceneDepth32",
            width,
            height,
            1u);
    }

    /** Uploads immutable expanded point corners and creates the one ordinary Scene pass. */
    void configureScene(
        const eastl::vector<WebglPointsWavesVertex> &vertices,
        const eastl::vector<uint> &indices)
    {
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(WebglPointsWavesVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->submit();
        sceneResources0 =
            device->createBindGroup<WebglPointsWavesSceneResources>(
                uniformBuffer0);
        mainPass0 =
            device->createRenderClass<WebglPointsWavesMainPass>(
                sceneResources0);
    }

    /** Uploads one deterministic camera and phase to the Scene pass. */
    void updateFrame(WebglPointsWavesUniforms uniforms)
    {
        WebglPointsWavesUniforms uniforms0 = uniforms;
        uniforms0.sampleOffsetAndPadding =
            float4(0.0f, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer0),
                &uniforms0,
                sizeof(uniforms0))
            ->submit();
    }

    /** Draws the Scene directly into the single-sample output and presents. */
    void render() override
    {
        WebglPointsWavesSceneFrameBuffer sceneFrameBuffer0;
        sceneFrameBuffer0.color = outputColor->createView();
        sceneFrameBuffer0.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer0.color.storeOp = StoreOp::Store;
        sceneFrameBuffer0.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrameBuffer0.depth = sceneDepth->createView();
        sceneFrameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer0.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer0.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglPointsWavesSceneSample0",
                sceneFrameBuffer0,
                mainPass0->setVertexBuffer(vertexBuffer),
                mainPass0->setIndexBuffer(indexBuffer),
                mainPass0(WebglPointsWavesIndexCount, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final requested-size DSL output for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the final readback width configured by the shared sample host. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the final readback height configured by the shared sample host. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases every resource owned by this private DSL renderer. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
