#ifndef GVM_THREE_WEBGL_SHADER_LAVA_HPP
#define GVM_THREE_WEBGL_SHADER_LAVA_HPP

#include "UGL.h"
#include "WebglShaderLavaData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the authored cloud/lava textures and fixed-frame material state. */
struct WebglShaderLavaSceneResources final : public IBindGroup
{
    /** Declares all resources read by the dedicated lava Scene pass. */
    constructor(
        UniformBuffer<WebglShaderLavaUniforms> uniforms [[Binding0]],
        Texture2D<float4> cloud [[Binding1]],
        Texture2D<float4> lava [[Binding2]],
        Sampler repeatSampler [[Binding3]])
    {
    }
};

/** Binds one source texture and directional 25-tap convolution state. */
struct WebglShaderLavaBlurResources final : public IBindGroup
{
    /** Declares the current postprocess source and shared linear sampler. */
    constructor(
        Texture2D<float4> source [[Binding0]],
        Sampler linearSampler [[Binding1]])
    {
    }
};

/** Binds the completed blur for additive composition over the loaded Scene target. */
struct WebglShaderLavaCombineResources final : public IBindGroup
{
    /** Declares the completed HDR blur and its linear sampler. */
    constructor(
        Texture2D<float4> bloom [[Binding0]],
        Sampler linearSampler [[Binding1]])
    {
    }
};

/** Binds the combined HDR image for r185 output transfer. */
struct WebglShaderLavaOutputResources final : public IBindGroup
{
    /** Declares the one combined source and linear sampler. */
    constructor(
        Texture2D<float4> source [[Binding0]],
        Sampler linearSampler [[Binding1]])
    {
    }
};

/** Carries lava UV and fragment-position fog inputs. */
struct WebglShaderLavaSceneVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float fogDepth [[Attribute1]];
};

/** Carries one analytic fullscreen UV coordinate. */
struct WebglShaderLavaScreenVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines one HDR scene or postprocess color target with depth. */
struct WebglShaderLavaSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one depth-free HDR postprocess target. */
struct WebglShaderLavaHdrFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final single-sample RGBA8 output target. */
struct WebglShaderLavaOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one sampled sRGB lava channel into linear shader working space. */
float webglShaderLavaSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Converts one non-negative linear output channel to the r185 sRGB transfer. */
float webglShaderLavaLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Emits the canonical fullscreen oversized triangle and top-down UVs. */
WebglShaderLavaScreenVertexOutput webglShaderLavaFullscreenVertex(uint vertexID)
{
    WebglShaderLavaScreenVertexOutput outputValue;
    if (vertexID == 0u)
    {
        outputValue.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
        outputValue.uv = float2(0.0f, 0.0f);
    }
    else if (vertexID == 1u)
    {
        outputValue.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
        outputValue.uv = float2(2.0f, 0.0f);
    }
    else
    {
        outputValue.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
        outputValue.uv = float2(0.0f, 2.0f);
    }
    return outputValue;
}

/** Evaluates one 25-tap sigma-four convolution along the requested axis. */
float4 webglShaderLavaBlur25(
    Texture2D<float4> source,
    Sampler sourceSampler,
    float2 uv,
    float2 increment)
{
    float4 sum = float4(0.0f);
    for (int tap = -12; tap <= 12; ++tap)
    {
        const float offset = float(tap);
        const float weight = exp(-(offset * offset) / 32.0f) /
            10.009172595445069f;
        sum += source->sampleLevel(
            sourceSampler, uv + increment * offset, 0.0f) * weight;
    }
    return sum;
}

/** Ports the complete authored lava fragment shader into the existing DSL. */
class WebglShaderLavaScenePass final : public IRenderClass
{
public:
    /** Configures the opaque front-sided depth-tested ShaderMaterial. */
    constructor(BindGroup<WebglShaderLavaSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies Torus animation and the exact authored UV scale. */
    WebglShaderLavaSceneVertexOutput vertex(
        WebglShaderLavaVertex inputValue [[VertexInput0]])
    {
        WebglShaderLavaSceneVertexOutput outputValue;
        float4 clipPosition = mul(
            resources->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = clipPosition;
        outputValue.uv = inputValue.uv * float2(3.0f, 1.0f);
        outputValue.fogDepth = clipPosition.z;
        return outputValue;
    }

    /** Evaluates both repeat textures, overflow color, and exp-squared fog. */
    WebglShaderLavaSceneFrameBuffer fragment(
        WebglShaderLavaSceneVertexOutput inputValue)
    {
        const float time = resources->uniforms->timeFogAndStrength.x;
        const float4 noise = resources->cloud->sample(
            resources->repeatSampler, inputValue.uv);
        float2 t1 = inputValue.uv + float2(1.5f, -1.5f) * time * 0.02f;
        float2 t2 = inputValue.uv + float2(-0.5f, 2.0f) * time * 0.01f;
        t1 += noise.xy * 2.0f;
        t2.x -= noise.y * 0.2f;
        t2.y += noise.z * 0.2f;
        const float p = resources->cloud->sample(
            resources->repeatSampler, t1 * 2.0f).a;
        float4 color = resources->lava->sample(
            resources->repeatSampler, t2 * 2.0f);
        color.rgb = float3(
            webglShaderLavaSrgbToLinear(color.r),
            webglShaderLavaSrgbToLinear(color.g),
            webglShaderLavaSrgbToLinear(color.b));
        float4 temporary = color * (p * 2.0f) + (color * color - 0.1f);
        if (temporary.r > 1.0f)
            temporary.bg += clamp(temporary.r - 2.0f, 0.0f, 100.0f);
        if (temporary.g > 1.0f) temporary.rb += temporary.g - 1.0f;
        if (temporary.b > 1.0f) temporary.rg += temporary.b - 1.0f;
        const float depth = inputValue.fogDepth;
        const float density = resources->uniforms->timeFogAndStrength.y;
        const float fogFactor = 1.0f - saturate(
            exp2(-density * density * depth * depth * 1.442695f));
        WebglShaderLavaSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(lerp(temporary, float4(0.0f, 0.0f, 0.0f, temporary.a), fogFactor));
        return frameBuffer;
    }
};

/** Applies BloomPass's horizontal 25-tap convolution. */
class WebglShaderLavaBloomHorizontalPass final : public IRenderClass
{
public:
    /** Binds the HDR Scene result without depth or blending. */
    constructor(BindGroup<WebglShaderLavaBlurResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }
private:
    /** Emits one fullscreen triangle. */
    WebglShaderLavaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    { return webglShaderLavaFullscreenVertex(vertexID); }
    /** Samples 25 horizontal taps at BloomPass.blurX spacing. */
    WebglShaderLavaHdrFrameBuffer fragment(WebglShaderLavaScreenVertexOutput inputValue)
    {
        WebglShaderLavaHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(webglShaderLavaBlur25(
            resources->source, resources->linearSampler, inputValue.uv,
            float2(0.001953125f, 0.0f)));
        return frameBuffer;
    }
};

/** Applies BloomPass's vertical 25-tap convolution. */
class WebglShaderLavaBloomVerticalPass final : public IRenderClass
{
public:
    /** Binds the horizontal intermediate without depth or blending. */
    constructor(BindGroup<WebglShaderLavaBlurResources> resources [[Slot0]])
    { setCullMode(CullMode::None); setDepthWriteEnabled(false); }
private:
    /** Emits one fullscreen triangle. */
    WebglShaderLavaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    { return webglShaderLavaFullscreenVertex(vertexID); }
    /** Samples 25 vertical taps at BloomPass.blurY spacing. */
    WebglShaderLavaHdrFrameBuffer fragment(WebglShaderLavaScreenVertexOutput inputValue)
    {
        WebglShaderLavaHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(webglShaderLavaBlur25(
            resources->source, resources->linearSampler, inputValue.uv,
            float2(0.0f, 0.001953125f)));
        return frameBuffer;
    }
};

/** Adds BloomPass's strength-1.25 blur through the declared fixed blend state. */
class WebglShaderLavaBloomCombinePass final : public IRenderClass
{
public:
    /** Reproduces Three's non-premultiplied AdditiveBlending factors. */
    constructor(BindGroup<WebglShaderLavaCombineResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        BlendState blendState;
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
    }
private:
    /** Emits one fullscreen triangle. */
    WebglShaderLavaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    { return webglShaderLavaFullscreenVertex(vertexID); }
    /** Returns the strength-scaled blur consumed by fixed-function blending. */
    WebglShaderLavaHdrFrameBuffer fragment(WebglShaderLavaScreenVertexOutput inputValue)
    {
        const float4 bloomColor = resources->bloom->sampleLevel(
            resources->linearSampler, inputValue.uv, 0.0f);
        const float4 sourceColor = bloomColor * 1.25f;
        WebglShaderLavaHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(sourceColor);
        return frameBuffer;
    }
};

/** Performs OutputPass's default no-tone-map sRGB transfer. */
class WebglShaderLavaOutputPass final : public IRenderClass
{
public:
    /** Binds the combined HDR result without depth. */
    constructor(BindGroup<WebglShaderLavaOutputResources> resources [[Slot0]])
    { setCullMode(CullMode::None); setDepthWriteEnabled(false); }
private:
    /** Emits one fullscreen triangle. */
    WebglShaderLavaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    { return webglShaderLavaFullscreenVertex(vertexID); }
    /** Applies OutputPass's sRGB OETF to the complete combined color. */
    WebglShaderLavaOutputFrameBuffer fragment(WebglShaderLavaScreenVertexOutput inputValue)
    {
        const float4 color = resources->source->sampleLevel(
            resources->linearSampler, inputValue.uv, 0.0f);
        WebglShaderLavaOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglShaderLavaLinearToSrgb(color.r)),
            half(webglShaderLavaLinearToSrgb(color.g)),
            half(webglShaderLavaLinearToSrgb(color.b)),
            half(webglShaderLavaLinearToSrgb(color.a)));
        return frameBuffer;
    }
};

/** Owns the dedicated Torus, authored textures, HDR bloom chain, and output. */
class WebglShaderLavaRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglShaderLavaVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglShaderLavaUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> cloudTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> lavaTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> horizontalTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> verticalTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    Sampler repeatSampler;
    Sampler linearSampler;
    BindGroup<WebglShaderLavaSceneResources> sceneResources;
    BindGroup<WebglShaderLavaBlurResources> horizontalResources;
    BindGroup<WebglShaderLavaBlurResources> verticalResources;
    BindGroup<WebglShaderLavaCombineResources> combineResources;
    BindGroup<WebglShaderLavaOutputResources> outputResources;
    RenderClass<WebglShaderLavaScenePass> scenePass;
    RenderClass<WebglShaderLavaBloomHorizontalPass> horizontalPass;
    RenderClass<WebglShaderLavaBloomVerticalPass> verticalPass;
    RenderClass<WebglShaderLavaBloomCombinePass> combinePass;
    RenderClass<WebglShaderLavaOutputPass> outputPass;
    uint indexCount = 0u;
    uint width = 0u;
    uint height = 0u;

public:
    /** Stores the generated GPU handles and creates repeat/linear samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        repeatSampler = device->createSampler({
            .label = "WebglShaderLavaRepeatSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
        linearSampler = device->createSampler({
            .label = "WebglShaderLavaLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
    }

    /** Allocates all ordinary single-sample HDR, depth, and RGBA8 targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture("WebglShaderLavaScene", width, height, 1u);
        horizontalTexture = device->createTexture("WebglShaderLavaBloomX", width, height, 1u);
        verticalTexture = device->createTexture("WebglShaderLavaBloomY", width, height, 1u);
        outputTexture = device->createTexture("WebglShaderLavaOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglShaderLavaDepth", width, height, 1u);
    }

    /** Uploads Torus geometry, explicit texture mips, uniforms, and all five passes. */
    void configureScene(
        const eastl::vector<WebglShaderLavaVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &cloudMips,
        uint cloudWidth,
        uint cloudHeight,
        const eastl::vector<eastl::vector<uint8_t>> &lavaMips,
        uint lavaWidth,
        uint lavaHeight,
        WebglShaderLavaUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer("WebglShaderLavaVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer("WebglShaderLavaIndices", indexCount);
        uniformBuffer = device->createBuffer("WebglShaderLavaUniforms", 1u);
        cloudTexture = device->createTexture("WebglShaderLavaCloud", cloudWidth, cloudHeight, 1u, uint(cloudMips.size()));
        lavaTexture = device->createTexture("WebglShaderLavaTile", lavaWidth, lavaHeight, 1u, uint(lavaMips.size()));
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(), uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        for (uint mip = 0u; mip < uint(cloudMips.size()); ++mip)
            graphicsQueue->writeTexture(cloudTexture, cloudMips[mip].data(), uint64_t(cloudMips[mip].size()), mip)->submit();
        for (uint mip = 0u; mip < uint(lavaMips.size()); ++mip)
            graphicsQueue->writeTexture(lavaTexture, lavaMips[mip].data(), uint64_t(lavaMips[mip].size()), mip)->submit();
        sceneResources = device->createBindGroup<WebglShaderLavaSceneResources>(
            uniformBuffer, cloudTexture->createView(), lavaTexture->createView(), repeatSampler);
        horizontalResources = device->createBindGroup<WebglShaderLavaBlurResources>(sceneTexture->createView(), linearSampler);
        verticalResources = device->createBindGroup<WebglShaderLavaBlurResources>(horizontalTexture->createView(), linearSampler);
        combineResources = device->createBindGroup<WebglShaderLavaCombineResources>(verticalTexture->createView(), linearSampler);
        outputResources = device->createBindGroup<WebglShaderLavaOutputResources>(sceneTexture->createView(), linearSampler);
        scenePass = device->createRenderClass<WebglShaderLavaScenePass>(sceneResources);
        horizontalPass = device->createRenderClass<WebglShaderLavaBloomHorizontalPass>(horizontalResources);
        verticalPass = device->createRenderClass<WebglShaderLavaBloomVerticalPass>(verticalResources);
        combinePass = device->createRenderClass<WebglShaderLavaBloomCombinePass>(combineResources);
        outputPass = device->createRenderClass<WebglShaderLavaOutputPass>(outputResources);
    }

    /** Executes the Scene, two convolutions, additive combine, output, and present. */
    void render() override
    {
        WebglShaderLavaSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrame.depth = depthTexture->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglShaderLavaHdrFrameBuffer horizontalFrame;
        horizontalFrame.color = horizontalTexture->createView();
        horizontalFrame.color.loadOp = LoadOp::Clear;
        horizontalFrame.color.storeOp = StoreOp::Store;
        horizontalFrame.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglShaderLavaHdrFrameBuffer verticalFrame;
        verticalFrame.color = verticalTexture->createView();
        verticalFrame.color.loadOp = LoadOp::Clear;
        verticalFrame.color.storeOp = StoreOp::Store;
        verticalFrame.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglShaderLavaHdrFrameBuffer combinedFrame;
        combinedFrame.color = sceneTexture->createView();
        combinedFrame.color.loadOp = LoadOp::Load;
        combinedFrame.color.storeOp = StoreOp::Store;
        WebglShaderLavaOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglShaderLavaScene", sceneFrame,
                         scenePass->setVertexBuffer(vertexBuffer),
                         scenePass->setIndexBuffer(indexBuffer),
                         scenePass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebglShaderLavaBloomHorizontal", horizontalFrame, horizontalPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglShaderLavaBloomVertical", verticalFrame, verticalPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglShaderLavaBloomCombine", combinedFrame,
                         combinePass(3u, 1u, 0u, 0u))
            ->renderPass("WebglShaderLavaOutput", outputFrame, outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for strict readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases every dedicated lava geometry, texture, and attachment resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(cloudTexture);
        device->freeTexture(lavaTexture);
        device->freeTexture(sceneTexture);
        device->freeTexture(horizontalTexture);
        device->freeTexture(verticalTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
