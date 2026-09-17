#ifndef GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_PROCEDURAL_HPP
#define GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_PROCEDURAL_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the replay-selected one-, two-, or three-channel procedural mode. */
struct WebglPostprocessingProceduralUniforms
{
    uint4 noiseDimensionAndReserved;
};

/** Binds the immutable replay-selected procedural mode to the fullscreen pass. */
struct WebglPostprocessingProceduralBindGroup final : public IBindGroup
{
    /** Declares the existing uniform-buffer binding used by both UGLC pipelines. */
    constructor(UniformBuffer<WebglPostprocessingProceduralUniforms> uniforms [[Binding0]])
    {
    }
};

/** Binds GPU-generated coordinate rows to the procedural fullscreen pass. */
struct WebglPostprocessingProceduralCoordinateBindGroup final : public IBindGroup
{
    /** Declares the two exact coordinate lookup textures. */
    constructor(Texture2D<TextureFormat::R32Float> xCoordinates [[Binding0]],
                Texture2D<TextureFormat::R32Float> yCoordinates [[Binding1]])
    {
    }
};

/** Carries one interpolated coordinate into a coordinate-row fragment stage. */
struct WebglPostprocessingProceduralCoordinateVertexOutput
{
    float4 position [[Position]];
    float coordinate [[Attribute0]];
};

/** Defines one R32Float coordinate row generated entirely by the DSL. */
struct WebglPostprocessingProceduralCoordinateFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::R32Float> coordinate;
};

/** Carries the final fullscreen position used to address the coordinate rows. */
struct WebglPostprocessingProceduralVertexOutput
{
    float4 position [[Position]];
};

/** Defines the single RGBA8 output written by the fullscreen procedural pass. */
struct WebglPostprocessingProceduralFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Evaluates the exact Three r185 common shader rand function for one UV coordinate. */
inline float webglPostprocessingProceduralRand(float2 uv)
{
    const float Pi = 3.141592653589793f;
    const float dotValue = dot(uv, float2(12.9898f, 78.233f));
    const float sineArgument = dotValue - Pi * floor(dotValue / Pi);
    return frac(sin(sineArgument) * 43758.5453f);
}

/** Generates PlaneGeometry interpolation into an R32Float target. */
class WebglPostprocessingProceduralCoordinatePass final : public IRenderClass
{
public:
    /** Configures the two-triangle coordinate plane without external resources. */
    constructor()
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Emits the upstream indexed PlaneGeometry order as six non-indexed vertices. */
    WebglPostprocessingProceduralCoordinateVertexOutput vertex(uint vertexID [[VertexID]])
    {
        WebglPostprocessingProceduralCoordinateVertexOutput outputValue;
        if (vertexID == 0u)
        {
            outputValue.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            outputValue.coordinate = 0.0f;
        }
        else if (vertexID == 1u || vertexID == 3u)
        {
            outputValue.position = float4(-1.0f, 1.0f, 0.0f, 1.0f);
            outputValue.coordinate = 0.0f;
        }
        else if (vertexID == 2u || vertexID == 5u)
        {
            outputValue.position = float4(1.0f, -1.0f, 0.0f, 1.0f);
            outputValue.coordinate = 1.0f;
        }
        else
        {
            outputValue.position = float4(1.0f, 1.0f, 0.0f, 1.0f);
            outputValue.coordinate = 1.0f;
        }
        return outputValue;
    }

    /** Stores one hardware-interpolated coordinate without normalization loss. */
    WebglPostprocessingProceduralCoordinateFrameBuffer fragment(
        WebglPostprocessingProceduralCoordinateVertexOutput inputValue)
    {
        WebglPostprocessingProceduralCoordinateFrameBuffer frameBuffer;
        frameBuffer.coordinate = inputValue.coordinate;
        return frameBuffer;
    }
};

/** Reproduces all three r185 procedural ShaderMaterial variants in one private DSL pass. */
class WebglPostprocessingProceduralPass final : public IRenderClass
{
public:
    /** Binds the replay mode and exact coordinate rows for the fullscreen pass. */
    constructor(BindGroup<WebglPostprocessingProceduralBindGroup> bindGroup [[Slot0]],
                BindGroup<WebglPostprocessingProceduralCoordinateBindGroup> coordinateBindGroup [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates one fullscreen triangle because coordinates come from GPU lookup rows. */
    WebglPostprocessingProceduralVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2(
            float((vertexID << 1u) & 2u),
            float(vertexID & 2u));
        WebglPostprocessingProceduralVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Evaluates the exact one-, two-, or three-channel upstream fragment expression. */
    WebglPostprocessingProceduralFrameBuffer fragment(
        WebglPostprocessingProceduralVertexOutput inputValue)
    {
        const uint column = uint(inputValue.position.x);
        const uint row = uint(inputValue.position.y);
        uint yCoordinateWidth;
        uint yCoordinateHeight;
        coordinateBindGroup->yCoordinates->getDimensions(
            yCoordinateWidth,
            yCoordinateHeight);
        const float2 texCoord = float2(
            coordinateBindGroup->xCoordinates->read(uint2(column, 0u), 0u).x,
            coordinateBindGroup->yCoordinates->read(
                uint2(yCoordinateWidth - 1u - row, 0u),
                0u).x);
        const float random0 = webglPostprocessingProceduralRand(texCoord);
        float3 color = float3(random0);
        const uint noiseDimension =
            bindGroup->uniforms->noiseDimensionAndReserved.x;
        if (noiseDimension >= 2u)
        {
            const float random1 = webglPostprocessingProceduralRand(
                texCoord + float2(0.4f, 0.6f));
            color = lerp(
                lerp(float3(1.0f), float3(0.0f, 0.0f, 1.0f), random0),
                float3(0.0f),
                random1);
            if (noiseDimension >= 3u)
            {
                const float random2 = webglPostprocessingProceduralRand(
                    texCoord + float2(0.6f, 0.4f));
                color = float3(random0, random1, random2);
            }
        }

        WebglPostprocessingProceduralFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the DSL-only fullscreen resources and deterministic RGBA8 readback target. */
class WebglPostprocessingProceduralRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglPostprocessingProceduralUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer;
    BindGroup<WebglPostprocessingProceduralBindGroup> bindGroup;
    BindGroup<WebglPostprocessingProceduralCoordinateBindGroup> coordinateBindGroup;
    RenderClass<WebglPostprocessingProceduralCoordinatePass> coordinatePass;
    RenderClass<WebglPostprocessingProceduralPass> proceduralPass;
    Texture<TextureFormat::R32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        xCoordinateTexture;
    Texture<TextureFormat::R32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        yCoordinateTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the uniform buffer and private coordinate RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer(
            "WebglPostprocessingProceduralUniforms",
            1u);
        bindGroup = device->createBindGroup<WebglPostprocessingProceduralBindGroup>(
            uniformBuffer);
        coordinatePass =
            device->createRenderClass<WebglPostprocessingProceduralCoordinatePass>();
    }

    /** Allocates exact coordinate rows and the host-requested RGBA8 output texture. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        xCoordinateTexture = device->createTexture(
            "WebglPostprocessingProceduralXCoordinates",
            width,
            height,
            1u);
        yCoordinateTexture = device->createTexture(
            "WebglPostprocessingProceduralYCoordinates",
            height,
            width,
            1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingProceduralRGBA8",
            width,
            height,
            1u);
        coordinateBindGroup =
            device->createBindGroup<WebglPostprocessingProceduralCoordinateBindGroup>(
                xCoordinateTexture->createView(),
                yCoordinateTexture->createView());
        proceduralPass = device->createRenderClass<WebglPostprocessingProceduralPass>(
            bindGroup,
            coordinateBindGroup);
    }

    /** Uploads the replay-derived procedural dimension through the DSL queue interface. */
    void configureNoiseMode(uint noiseDimension)
    {
        WebglPostprocessingProceduralUniforms uniforms;
        uniforms.noiseDimensionAndReserved = uint4(noiseDimension, 0u, 0u, 0u);
        graphicsQueue
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
    }

    /** Generates coordinate rows, evaluates the noise pass, and presents its output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglPostprocessingProceduralCoordinateFrameBuffer xCoordinateFrameBuffer;
        xCoordinateFrameBuffer.coordinate = xCoordinateTexture->createView();
        xCoordinateFrameBuffer.coordinate.loadOp = LoadOp::Clear;
        xCoordinateFrameBuffer.coordinate.storeOp = StoreOp::Store;
        xCoordinateFrameBuffer.coordinate.clearValue = {0.0, 0.0, 0.0, 0.0};

        WebglPostprocessingProceduralCoordinateFrameBuffer yCoordinateFrameBuffer;
        yCoordinateFrameBuffer.coordinate = yCoordinateTexture->createView();
        yCoordinateFrameBuffer.coordinate.loadOp = LoadOp::Clear;
        yCoordinateFrameBuffer.coordinate.storeOp = StoreOp::Store;
        yCoordinateFrameBuffer.coordinate.clearValue = {0.0, 0.0, 0.0, 0.0};

        WebglPostprocessingProceduralFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        graphicsQueue
            ->renderPass(
                "procedural-x-coordinates",
                xCoordinateFrameBuffer,
                coordinatePass(6u, 1u, 0u, 0u))
            ->renderPass(
                "procedural-y-coordinates",
                yCoordinateFrameBuffer,
                coordinatePass(6u, 1u, 0u, 0u))
            ->renderPass(
                "procedural-noise",
                frameBuffer,
                proceduralPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the mode uniform, coordinate rows, and RGBA8 output resources. */
    void destroy() override
    {
        device->freeBuffer(uniformBuffer);
        device->freeTexture(xCoordinateTexture);
        device->freeTexture(yCoordinateTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
