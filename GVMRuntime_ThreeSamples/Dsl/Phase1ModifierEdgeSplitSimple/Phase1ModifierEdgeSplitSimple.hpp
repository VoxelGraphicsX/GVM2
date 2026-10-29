#ifndef GVM_THREE_PHASE1_MODIFIER_EDGE_SPLIT_SIMPLE_HPP
#define GVM_THREE_PHASE1_MODIFIER_EDGE_SPLIT_SIMPLE_HPP

#include "Phase1ModifierEdgeSplitSimpleData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the Cerberus albedo texture and immutable per-scenario state. */
struct WebglModifierEdgeSplitResources final : public IBindGroup
{
    /** Declares the only resources used by the private Standard material. */
    constructor(
        UniformBuffer<WebglModifierEdgeSplitUniforms> uniforms [[Binding0]],
        Texture2D<float4> albedo [[Binding1]],
        Sampler albedoSampler [[Binding2]])
    {
    }
};

/** Carries projected position, world-space shading data, and authored UV. */
struct WebglModifierEdgeSplitVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Defines the single-sample color and depth attachments for the simple Scene. */
struct WebglModifierEdgeSplitFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one sRGB texture or output channel into linear working space. */
float webglModifierEdgeSplitSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value * 0.0773993808f
        : pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
}

/** Converts one non-negative linear channel through Three's output transfer. */
float webglModifierEdgeSplitLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces the one-object Standard material under the authored hemisphere light. */
class WebglModifierEdgeSplitMainPass final : public IRenderClass
{
public:
    /** Enables opaque indexed triangles with generated-backend front culling. */
    constructor(
        BindGroup<WebglModifierEdgeSplitResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one CPU world-transformed Cerberus vertex. */
    WebglModifierEdgeSplitVertexOutput vertex(
        WebglModifierEdgeSplitVertex inputValue [[VertexInput0]])
    {
        WebglModifierEdgeSplitVertexOutput outputValue;
        outputValue.position =
            resources->uniforms->viewProjectionColumn0 * inputValue.position.x +
            resources->uniforms->viewProjectionColumn1 * inputValue.position.y +
            resources->uniforms->viewProjectionColumn2 * inputValue.position.z +
            resources->uniforms->viewProjectionColumn3;
        outputValue.worldPosition = inputValue.position;
        outputValue.worldNormal = inputValue.normal;
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        return outputValue;
    }

    /** Applies smooth or derivative-flat normal shading, optional map, and sRGB output. */
    WebglModifierEdgeSplitFrameBuffer fragment(
        WebglModifierEdgeSplitVertexOutput inputValue)
    {
        float3 normal = normalize(inputValue.worldNormal);
        if (resources->uniforms->materialFlagsAndPadding.y > 0.5f)
        {
            normal = normalize(cross(
                ddy(inputValue.worldPosition),
                ddx(inputValue.worldPosition)));
        }
        float3 baseColor = float3(1.0f);
        if (resources->uniforms->materialFlagsAndPadding.x > 0.5f)
        {
            const float3 encoded = resources->albedo->sample(
                resources->albedoSampler,
                inputValue.textureCoordinate).xyz;
            baseColor = float3(
                webglModifierEdgeSplitSrgbToLinear(encoded.x),
                webglModifierEdgeSplitSrgbToLinear(encoded.y),
                webglModifierEdgeSplitSrgbToLinear(encoded.z));
        }
        const float hemisphereWeight = normal.y * 0.5f + 0.5f;
        const float3 skyColor = float3(1.0f);
        const float3 groundColor = float3(0.0578054302f);
        const float3 linearColor =
            baseColor * lerp(groundColor, skyColor, hemisphereWeight) *
            (3.0f * 0.3183098861837907f);
        WebglModifierEdgeSplitFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglModifierEdgeSplitLinearToSrgb(linearColor.x)),
            half(webglModifierEdgeSplitLinearToSrgb(linearColor.y)),
            half(webglModifierEdgeSplitLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated ordinary RenderClass for webgl_modifier_edgesplit. */
class Phase1ModifierEdgeSplitSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglModifierEdgeSplitVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglModifierEdgeSplitUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> albedoTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler albedoSampler;
    BindGroup<WebglModifierEdgeSplitResources> resources;
    RenderClass<WebglModifierEdgeSplitMainPass> mainPass;
    uint indexCount = 0u;

public:
    /** Stores the generated device and creates immutable sampling state. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        albedoSampler = device->createSampler({
            .label = "WebglModifierEdgeSplitAlbedoSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
    }

    /** Allocates canonical single-sample output attachments. */
    void configureOutput(uint width, uint height)
    {
        depthTexture = device->createTexture(
            "WebglModifierEdgeSplitDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglModifierEdgeSplitOutput", width, height, 1u);
    }

    /** Uploads selected EdgeSplit geometry, all explicit image mips, and camera state. */
    void configureScene(
        const eastl::vector<WebglModifierEdgeSplitVertex> &vertices,
        const eastl::vector<uint> &indices,
        WebglModifierEdgeSplitUniforms uniforms,
        const eastl::vector<eastl::vector<uint8_t>> &albedoMips,
        uint albedoWidth,
        uint albedoHeight)
    {
        vertexBuffer = device->createBuffer(
            "WebglModifierEdgeSplitVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebglModifierEdgeSplitIndices", uint(indices.size()));
        uniformBuffer = device->createBuffer(
            "WebglModifierEdgeSplitUniforms", 1u);
        albedoTexture = device->createTexture(
            "WebglModifierEdgeSplitAlbedo",
            albedoWidth,
            albedoHeight,
            1u,
            uint(albedoMips.size()));
        indexCount = uint(indices.size());
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(WebglModifierEdgeSplitVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        for (uint mipLevel = 0u;
             mipLevel < uint(albedoMips.size());
             ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(
                    albedoTexture,
                    albedoMips[mipLevel].data(),
                    uint64_t(albedoMips[mipLevel].size()),
                    mipLevel)
                ->submit();
        }
        resources = device->createBindGroup<WebglModifierEdgeSplitResources>(
            uniformBuffer,
            albedoTexture->createView(),
            albedoSampler);
        mainPass = device->createRenderClass<WebglModifierEdgeSplitMainPass>(
            resources);
    }

    /** Draws one indexed Cerberus object and presents its final target. */
    void render() override
    {
        WebglModifierEdgeSplitFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglModifierEdgeSplitScene",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 texture for strict readback. */
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

    /** Releases every dedicated geometry, material, and output resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(albedoTexture);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
