#ifndef GVM_THREE_WEBGL_SHADER_HPP
#define GVM_THREE_WEBGL_SHADER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the deterministic Monjori time used by the r185 webgl_shader port. */
struct WebglShaderUniforms
{
    float4 timeAndReserved;
};

/** Binds the single uniform buffer consumed by the Monjori fragment shader. */
struct WebglShaderBindGroup final : public IBindGroup
{
    /** Declares the existing DSL uniform-buffer binding used by both UGLC pipelines. */
    constructor(UniformBuffer<WebglShaderUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the r185 plane UV coordinates from the procedural vertex stage. */
struct WebglShaderVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the deterministic RGBA8 output used by the webgl_shader scenarios. */
struct WebglShaderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Reproduces the r185 Monjori ShaderMaterial as one RenderSet-free fullscreen pass. */
class WebglShaderMonjoriPass final : public IRenderClass
{
public:
    /** Binds the Monjori time without adding any standalone scene geometry resources. */
    constructor(BindGroup<WebglShaderBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates a fullscreen triangle whose interpolated UVs match PlaneGeometry(2, 2). */
    WebglShaderVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglShaderVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.texCoord = float2(texCoord.x, 1.0f - texCoord.y);
        return outputValue;
    }

    /** Evaluates the original r185 Monjori fragment algorithm without approximation. */
    WebglShaderFrameBuffer fragment(WebglShaderVertexOutput inputValue)
    {
        const float2 p = -1.0f + 2.0f * inputValue.texCoord;
        const float a = bindGroup->uniforms->timeAndReserved.x * 40.0f;
        const float g = 1.0f / 40.0f;

        float e = 400.0f * (p.x * 0.5f + 0.5f);
        float f = 400.0f * (p.y * 0.5f + 0.5f);
        float i = 200.0f + sin(e * g + a / 150.0f) * 20.0f;
        float d = 200.0f + cos(f * g / 2.0f) * 18.0f + cos(e * g) * 7.0f;
        float r = sqrt(pow(abs(i - e), 2.0f) + pow(abs(d - f), 2.0f));
        float q = f / r;
        e = (r * cos(q)) - a / 2.0f;
        f = (r * sin(q)) - a / 2.0f;
        d = sin(e * g) * 176.0f + sin(e * g) * 164.0f + r;
        float h = ((f + d) + a / 2.0f) * g;
        i = cos(h + r * p.x / 1.3f) * (e + e + a) +
            cos(q * g * 6.0f) * (r + h / 3.0f);
        h = sin(f * g) * 144.0f - sin(e * g) * 212.0f * p.x;
        h = (h + (f - e) * q + sin(r - (a + h) / 7.0f) * 10.0f + i / 4.0f) * g;
        i += cos(h * 2.3f * sin(a / 350.0f - q)) * 184.0f *
                 sin(q - (r * 4.3f + a / 12.0f) * g) +
             tan(r * g + h) * 184.0f * cos(r * g + h);
        i = fmod(i / 5.6f, 256.0f) / 64.0f;
        if (i < 0.0f)
        {
            i += 4.0f;
        }
        if (i >= 2.0f)
        {
            i = 4.0f - i;
        }
        d = r / 350.0f;
        d += sin(d * d * 8.0f) * 0.52f;
        f = (sin(a * g) + 1.0f) / 2.0f;

        const float3 color =
            float3(f * i / 1.6f, i / 2.0f + d / 13.0f, i) * d * p.x +
            float3(i / 1.3f + d / 8.0f, i / 2.0f + d / 18.0f, i) *
                d * (1.0f - p.x);
        WebglShaderFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns and submits the deterministic r185 webgl_shader fullscreen sample. */
class WebglShaderRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglShaderUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglShaderBindGroup> bindGroup;
    RenderClass<WebglShaderMonjoriPass> monjoriPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    uint frameIndex = 0u;

public:
    /** Creates the existing DSL resources shared by Legacy and Experimental generation. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer("WebglShaderUniforms", 1u);
        bindGroup = device->createBindGroup<WebglShaderBindGroup>(uniformBuffer);
        monjoriPass = device->createRenderClass<WebglShaderMonjoriPass>(bindGroup);
    }

    /** Allocates the fixed-size RGBA8 target requested by the deterministic host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglShaderRGBA8", width, height, 1u);
    }

    /** Advances time at 60 Hz, draws one fullscreen triangle, and presents the output. */
    void render() override
    {
        WebglShaderUniforms uniforms;
        uniforms.timeAndReserved = float4(float(frameIndex) / 60.0f, 0.0f, 0.0f, 0.0f);

        auto nextTexture = swapchain->queryNextTexture();
        WebglShaderFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        graphicsQueue
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->renderPass("WebglShaderMonjori", frameBuffer, monjoriPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        ++frameIndex;
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

    /** Releases the uniform and output resources after capture completes. */
    void destroy() override
    {
        device->freeBuffer(uniformBuffer);
        device->freeTexture(outputTexture);
    }
};

#endif
