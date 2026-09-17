#ifndef GVM_THREE_WEBGL_GEOMETRY_COLORS_LOOKUPTABLE_HPP
#define GVM_THREE_WEBGL_GEOMETRY_COLORS_LOOKUPTABLE_HPP

#include "UGL.h"
#include "WebglGeometryColorsLookuptableData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the pressure mesh camera and point-light parameters. */
struct WebglGeometryColorsLookuptableSceneResources final : public IBindGroup
{
    /** Declares the one immutable scene uniform block. */
    constructor(
        UniformBuffer<WebglGeometryColorsLookuptableUniforms> uniforms [[Binding0]])
    {
    }
};

/** Binds the compute-generated 1-by-32 color legend. */
struct WebglGeometryColorsLookuptableLegendComputeResources final : public IBindGroup
{
    /** Declares the writable legend texture and selected map uniform. */
    constructor(
        RWTexture2D<TextureFormat::RGBA8Unorm> legend [[Binding0]],
        UniformBuffer<WebglGeometryColorsLookuptableUniforms> uniforms [[Binding1]])
    {
    }
};

/** Binds the generated legend to the ordinary screen-space sprite. */
struct WebglGeometryColorsLookuptableLegendResources final : public IBindGroup
{
    /** Declares one sampled legend texture and nearest sampler. */
    constructor(
        Texture2D<float4> legend [[Binding0]],
        Sampler legendSampler [[Binding1]])
    {
    }
};

/** Carries smooth view-space data into Lambert shading. */
struct WebglGeometryColorsLookuptableSceneVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float3 color [[Attribute2]];
};

/** Carries exact legend UV coordinates from an analytic sprite triangle. */
struct WebglGeometryColorsLookuptableLegendVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the shared single-sample RGBA8 and depth attachments. */
struct WebglGeometryColorsLookuptableSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the legend overlay color target without depth. */
struct WebglGeometryColorsLookuptableLegendFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear working-space channel to the r185 sRGB output transfer. */
float webglGeometryColorsLookuptableLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns one raw endpoint channel from the selected Canvas LUT color map. */
float3 webglGeometryColorsLookuptableMapColor(uint mapIndex, uint stopIndex)
{
    if (mapIndex == 1u)
    {
        if (stopIndex == 0u) return float3(60.0f, 78.0f, 194.0f) / 255.0f;
        if (stopIndex == 1u) return float3(155.0f, 188.0f, 255.0f) / 255.0f;
        if (stopIndex == 2u) return float3(220.0f, 220.0f, 220.0f) / 255.0f;
        if (stopIndex == 3u) return float3(246.0f, 163.0f, 133.0f) / 255.0f;
        return float3(180.0f, 4.0f, 38.0f) / 255.0f;
    }
    if (stopIndex == 0u) return float3(0.0f, 0.0f, 1.0f);
    if (stopIndex == 1u) return float3(0.0f, 1.0f, 1.0f);
    if (stopIndex == 2u) return float3(0.0f, 1.0f, 0.0f);
    if (stopIndex == 3u) return float3(1.0f, 1.0f, 0.0f);
    return float3(1.0f, 0.0f, 0.0f);
}

/** Interpolates the exact five-stop r185 Canvas LUT at one normalized value. */
float3 webglGeometryColorsLookuptableLegendColor(uint mapIndex, float value)
{
    if (value <= 0.2f)
    {
        return lerp(
            webglGeometryColorsLookuptableMapColor(mapIndex, 0u),
            webglGeometryColorsLookuptableMapColor(mapIndex, 1u),
            value / 0.2f);
    }
    if (value <= 0.5f)
    {
        return lerp(
            webglGeometryColorsLookuptableMapColor(mapIndex, 1u),
            webglGeometryColorsLookuptableMapColor(mapIndex, 2u),
            (value - 0.2f) / 0.3f);
    }
    if (value <= 0.8f)
    {
        return lerp(
            webglGeometryColorsLookuptableMapColor(mapIndex, 2u),
            webglGeometryColorsLookuptableMapColor(mapIndex, 3u),
            (value - 0.5f) / 0.3f);
    }
    return lerp(
        webglGeometryColorsLookuptableMapColor(mapIndex, 3u),
        webglGeometryColorsLookuptableMapColor(mapIndex, 4u),
        (value - 0.8f) / 0.2f);
}

/** Generates the same 32-row CanvasTexture legend used by Lut.updateCanvas. */
class [[LocalWorkGroupSize(1, 8, 1)]]
WebglGeometryColorsLookuptableLegendComputePass final : public IComputeClass
{
public:
    /** Binds the writable legend and immutable map selection. */
    constructor(
        BindGroup<WebglGeometryColorsLookuptableLegendComputeResources>
            resources [[Slot0]])
    {
    }

private:
    /** Writes one top-down canvas row with byte-quantized map color. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint row = dispatchThreadID.y;
        if (row >= 32u) return;
        const float value = 1.0f - float(row + 1u) / 32.0f;
        const uint mapIndex = uint(resources->uniforms->lightAndMap.w + 0.5f);
        const float3 color = webglGeometryColorsLookuptableLegendColor(
            mapIndex, value);
        resources->legend->write(uint2(0u, row), half4(half3(color), half(1.0f)));
    }
};

/** Renders the one non-indexed pressure object with vertex-color Lambert shading. */
class WebglGeometryColorsLookuptableMainPass final : public IRenderClass
{
public:
    /** Configures the r185 double-sided depth-tested material. */
    constructor(
        BindGroup<WebglGeometryColorsLookuptableSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the fixed perspective camera and forwards smooth attributes. */
    WebglGeometryColorsLookuptableSceneVertexOutput vertex(
        WebglGeometryColorsLookuptableVertex inputValue [[VertexInput0]])
    {
        WebglGeometryColorsLookuptableSceneVertexOutput outputValue;
        const float4 viewPosition = mul(
            resources->uniforms->modelView,
            float4(inputValue.position, 1.0f));
        float4 clipPosition = mul(
            resources->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = inputValue.normal;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    /** Evaluates the camera-attached intensity-three point light and sRGB output. */
    WebglGeometryColorsLookuptableSceneFrameBuffer fragment(
        WebglGeometryColorsLookuptableSceneVertexOutput inputValue)
    {
        float3 normal = normalize(inputValue.viewNormal);
        const float3 lightDirection = normalize(-inputValue.viewPosition);
        if (dot(normal, lightDirection) < 0.0f) normal = -normal;
        const float irradiance =
            saturate(dot(normal, lightDirection)) *
            resources->uniforms->lightAndMap.x * 0.3183098861837907f;
        const float3 linearColor = inputValue.color *
            resources->uniforms->lightAndMap.yyy * irradiance;
        WebglGeometryColorsLookuptableSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryColorsLookuptableLinearToSrgb(linearColor.x)),
            half(webglGeometryColorsLookuptableLinearToSrgb(linearColor.y)),
            half(webglGeometryColorsLookuptableLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the compute-generated legend after the main Scene without depth. */
class WebglGeometryColorsLookuptableLegendPass final : public IRenderClass
{
public:
    /** Binds the nearest-filtered legend and disables depth writes. */
    constructor(
        BindGroup<WebglGeometryColorsLookuptableLegendResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits a six-vertex sprite centered at NDC x=-0.5 with width 0.125. */
    WebglGeometryColorsLookuptableLegendVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 positions[6] = {
            float2(-0.5625f, -0.5f), float2(-0.4375f, -0.5f),
            float2(-0.4375f, 0.5f), float2(-0.5625f, -0.5f),
            float2(-0.4375f, 0.5f), float2(-0.5625f, 0.5f)};
        const float2 uvs[6] = {
            float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
            float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)};
        WebglGeometryColorsLookuptableLegendVertexOutput outputValue;
        outputValue.position = float4(positions[vertexID], 0.0f, 1.0f);
        outputValue.uv = uvs[vertexID];
        return outputValue;
    }

    /** Samples the CanvasTexture-equivalent legend with vertical flip semantics. */
    WebglGeometryColorsLookuptableLegendFrameBuffer fragment(
        WebglGeometryColorsLookuptableLegendVertexOutput inputValue)
    {
        const float4 color = resources->legend->sampleLevel(
            resources->legendSampler,
            inputValue.uv,
            0.0f);
        WebglGeometryColorsLookuptableLegendFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the dedicated mesh, procedural legend, passes, and single-sample output. */
class WebglGeometryColorsLookuptableRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglGeometryColorsLookuptableVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglGeometryColorsLookuptableUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> legendTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler legendSampler;
    BindGroup<WebglGeometryColorsLookuptableSceneResources> sceneResources;
    BindGroup<WebglGeometryColorsLookuptableLegendComputeResources> computeResources;
    BindGroup<WebglGeometryColorsLookuptableLegendResources> legendResources;
    ComputeClass<WebglGeometryColorsLookuptableLegendComputePass> computePass;
    RenderClass<WebglGeometryColorsLookuptableMainPass> mainPass;
    RenderClass<WebglGeometryColorsLookuptableLegendPass> legendPass;
    uint vertexCount = 0u;

public:
    /** Stores the generated device, swapchain, queue, and nearest legend sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        legendSampler = device->createSampler({
            .label = "WebglGeometryColorsLookuptableLegendSampler",
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

    /** Allocates canonical single-sample color, depth, and 1-by-32 legend targets. */
    void configureOutput(uint width, uint height)
    {
        outputTexture = device->createTexture(
            "WebglGeometryColorsLookuptableOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglGeometryColorsLookuptableDepth", width, height, 1u);
        legendTexture = device->createTexture(
            "WebglGeometryColorsLookuptableLegend", 1u, 32u, 1u);
    }

    /** Uploads the exact pressure geometry and creates all dedicated passes. */
    void configureScene(
        const eastl::vector<WebglGeometryColorsLookuptableVertex> &vertices,
        WebglGeometryColorsLookuptableUniforms uniforms)
    {
        vertexCount = uint(vertices.size());
        vertexBuffer = device->createBuffer(
            "WebglGeometryColorsLookuptableVertices", vertexCount);
        uniformBuffer = device->createBuffer(
            "WebglGeometryColorsLookuptableUniforms", 1u);
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        sceneResources =
            device->createBindGroup<WebglGeometryColorsLookuptableSceneResources>(
                uniformBuffer);
        computeResources =
            device->createBindGroup<WebglGeometryColorsLookuptableLegendComputeResources>(
                legendTexture->createView(), uniformBuffer);
        legendResources =
            device->createBindGroup<WebglGeometryColorsLookuptableLegendResources>(
                legendTexture->createView(), legendSampler);
        computePass =
            device->createComputeClass<WebglGeometryColorsLookuptableLegendComputePass>(
                computeResources);
        mainPass =
            device->createRenderClass<WebglGeometryColorsLookuptableMainPass>(
                sceneResources);
        legendPass =
            device->createRenderClass<WebglGeometryColorsLookuptableLegendPass>(
                legendResources);
    }

    /** Generates the legend, renders the mesh, overlays the sprite, and presents. */
    void render() override
    {
        WebglGeometryColorsLookuptableSceneFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputTexture->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {1.0, 1.0, 1.0, 1.0};
        sceneFrameBuffer.depth = depthTexture->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebglGeometryColorsLookuptableLegendFrameBuffer legendFrameBuffer;
        legendFrameBuffer.color = outputTexture->createView();
        legendFrameBuffer.color.loadOp = LoadOp::Load;
        legendFrameBuffer.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->computePass(
                "WebglGeometryColorsLookuptableLegendCompute",
                computePass(1u, 32u, 1u))
            ->renderPass(
                "WebglGeometryColorsLookuptableMain",
                sceneFrameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass(vertexCount, 1u, 0u, 0u))
            ->renderPass(
                "WebglGeometryColorsLookuptableLegend",
                legendFrameBuffer,
                legendPass(6u, 1u, 0u, 0u))
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

    /** Releases all dedicated geometry, legend, and attachment resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(legendTexture);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
