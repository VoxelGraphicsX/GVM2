#ifndef GVM_THREE_WEBGL_MATERIALS_MATCAP_HPP
#define GVM_THREE_WEBGL_MATERIALS_MATCAP_HPP

#include "WebglMaterialsMatcapData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the two canonical matcaps, tangent normal map, and immutable scene state. */
struct WebglMaterialsMatcapResources final : public IBindGroup
{
    /** Declares the complete private matcap resource layout. */
    constructor(
        UniformBuffer<WebglMaterialsMatcapUniforms> uniforms [[Binding0]],
        Texture2D<float4> exrMatcap [[Binding1]],
        Texture2D<float4> droppedMatcap [[Binding2]],
        Texture2D<float4> normalMap [[Binding3]],
        Sampler matcapSampler [[Binding4]],
        Sampler normalSampler [[Binding5]])
    {
    }
};

/** Carries view-space geometry and UV data into the matcap fragment stage. */
struct WebglMaterialsMatcapVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float3 viewDerivativeU [[Attribute3]];
    float3 viewDerivativeV [[Attribute4]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglMaterialsMatcapFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one non-negative linear component to the r185 sRGB output transfer. */
float webglMaterialsMatcapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the exact r185 ACES fitted operator before display encoding. */
float3 webglMaterialsMatcapAces(float3 color, float exposure)
{
    color *= exposure / 0.6f;
    const float3 acesInput = float3(
        0.59719f * color.x + 0.35458f * color.y + 0.04823f * color.z,
        0.07600f * color.x + 0.90834f * color.y + 0.01566f * color.z,
        0.02840f * color.x + 0.13383f * color.y + 0.83777f * color.z);
    const float3 fitA =
        acesInput * (acesInput + float3(0.0245786f)) -
        float3(0.000090537f);
    const float3 fitB =
        acesInput * (0.983729f * acesInput + float3(0.4329510f)) +
        float3(0.238081f);
    const float3 fitted = fitA / fitB;
    const float3 acesOutput = float3(
        1.60475f * fitted.x - 0.53108f * fitted.y - 0.07367f * fitted.z,
        -0.10208f * fitted.x + 1.10813f * fitted.y - 0.00605f * fitted.z,
        -0.00327f * fitted.x - 0.07276f * fitted.y + 1.07602f * fitted.z);
    return clamp(acesOutput, float3(0.0f), float3(1.0f));
}

/** Renders the dedicated front-sided Lee Perry Smith matcap material. */
class WebglMaterialsMatcapMainPass final : public IRenderClass
{
public:
    /** Configures the opaque front-sided depth-tested matcap draw. */
    constructor(BindGroup<WebglMaterialsMatcapResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the fixed camera and passes view-space attributes to the fragment stage. */
    WebglMaterialsMatcapVertexOutput vertex(
        WebglMaterialsMatcapVertex inputValue [[VertexInput0]])
    {
        WebglMaterialsMatcapVertexOutput outputValue;
        const float4 localPosition = float4(inputValue.position, 1.0f);
        const float4 viewPosition =
            mul(resources->uniforms->modelView, localPosition);
        outputValue.position =
            mul(resources->uniforms->modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        const float4 transformedNormal =
            mul(resources->uniforms->normalTransform, float4(inputValue.normal, 0.0f));
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(float3(
            transformedNormal.x,
            transformedNormal.y,
            transformedNormal.z));
        const float4 transformedDerivativeU =
            mul(resources->uniforms->modelView,
                float4(inputValue.positionDerivativeU, 0.0f));
        const float4 transformedDerivativeV =
            mul(resources->uniforms->modelView,
                float4(inputValue.positionDerivativeV, 0.0f));
        outputValue.viewDerivativeU = transformedDerivativeU.xyz;
        outputValue.viewDerivativeV = transformedDerivativeV.xyz;
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        return outputValue;
    }

    /** Samples the tangent normal and selected matcap before ACES display conversion. */
    WebglMaterialsMatcapFrameBuffer fragment(
        WebglMaterialsMatcapVertexOutput inputValue)
    {
        WebglMaterialsMatcapFrameBuffer frameBuffer;
        const float2 normalUv = float2(
            inputValue.textureCoordinate.x,
            1.0f - inputValue.textureCoordinate.y);
        const float3 sampledNormal =
            (float3(resources->normalMap->sample(
                resources->normalSampler,
                normalUv).xyz) * 2.0f - float3(1.0f)) *
            float3(1.0f, 1.0f, 1.0f);
        const float3 tangent = cross(
            inputValue.viewDerivativeV,
            inputValue.viewNormal);
        const float3 bitangent = cross(
            inputValue.viewNormal,
            inputValue.viewDerivativeU);
        const float determinant = max(
            dot(tangent, tangent),
            dot(bitangent, bitangent));
        const float tangentScale =
            determinant == 0.0f ? 0.0f : 1.0f / sqrt(determinant);
        const float3 normal = normalize(
            tangent * (sampledNormal.x * tangentScale) +
            bitangent * (sampledNormal.y * tangentScale) +
            inputValue.viewNormal * sampledNormal.z);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 matcapX = normalize(float3(
            viewDirection.z, 0.0f, -viewDirection.x));
        const float3 matcapY = cross(viewDirection, matcapX);
        const float2 matcapUv =
            float2(dot(matcapX, normal), dot(matcapY, normal)) * 0.495f +
            float2(0.5f);
        const float3 exrColor = resources->exrMatcap->sampleLevel(
            resources->matcapSampler,
            float2(matcapUv.x, 1.0f - matcapUv.y),
            0.0f).xyz;
        const float3 droppedColor = resources->droppedMatcap->sample(
            resources->matcapSampler,
            float2(matcapUv.x, 1.0f - matcapUv.y)).xyz;
        const float custom = resources->uniforms->colorExposureCustom.w;
        const float3 matcapColor = lerp(exrColor, droppedColor, custom);
        const float3 linearColor =
            resources->uniforms->colorExposureCustom.xyz * matcapColor;
        const float3 displayLinear = webglMaterialsMatcapAces(
            linearColor,
            resources->uniforms->colorExposureCustom.w > 0.5f
                ? 1.35f
                : 1.0f);
        frameBuffer.color = half4(
            webglMaterialsMatcapLinearToSrgb(displayLinear.x),
            webglMaterialsMatcapLinearToSrgb(displayLinear.y),
            webglMaterialsMatcapLinearToSrgb(displayLinear.z),
            1.0f);
        return frameBuffer;
    }
};

/** Owns the dedicated single-mesh matcap sample and all DSL-created GPU resources. */
class WebglMaterialsMatcapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsMatcapVertex,
           BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsMatcapUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> exrMatcapTexture;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> droppedMatcapTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> normalTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler matcapSampler;
    Sampler normalSampler;
    BindGroup<WebglMaterialsMatcapResources> resources;
    RenderClass<WebglMaterialsMatcapMainPass> mainPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the immutable material samplers and uniform allocation. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer("WebglMaterialsMatcapUniforms", 1u);
        matcapSampler = device->createSampler({
            .label = "WebglMaterialsMatcapSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
        normalSampler = device->createSampler({
            .label = "WebglMaterialsMatcapNormalSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
    }

    /** Allocates ordinary single-sample output and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglMaterialsMatcapOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglMaterialsMatcapDepth", width, height, 1u);
    }

    /** Uploads the decoded mesh, three authored textures, and scenario uniforms. */
    void configureScene(
        const eastl::vector<WebglMaterialsMatcapVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint16_t> &exrMatcapPixels,
        uint exrWidth,
        uint exrHeight,
        const eastl::vector<eastl::vector<uint8_t>> &droppedMatcapMips,
        uint droppedWidth,
        uint droppedHeight,
        const eastl::vector<eastl::vector<uint8_t>> &normalMips,
        uint normalWidth,
        uint normalHeight,
        WebglMaterialsMatcapUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer(
            "WebglMaterialsMatcapVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebglMaterialsMatcapIndices", indexCount);
        exrMatcapTexture = device->createTexture(
            "WebglMaterialsMatcapExr", exrWidth, exrHeight, 1u);
        droppedMatcapTexture = device->createTexture(
            "WebglMaterialsMatcapDropped", droppedWidth, droppedHeight, 1u,
            uint(droppedMatcapMips.size()));
        normalTexture = device->createTexture(
            "WebglMaterialsMatcapNormal",
            normalWidth, normalHeight, 1u, uint(normalMips.size()));
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                          uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->writeTexture(exrMatcapTexture, exrMatcapPixels.data(),
                           uint64_t(exrMatcapPixels.size()) * sizeof(uint16_t))
            ->submit();
        for (uint mipLevel = 0u;
             mipLevel < uint(droppedMatcapMips.size()); ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(droppedMatcapTexture,
                               droppedMatcapMips[mipLevel].data(),
                               uint64_t(droppedMatcapMips[mipLevel].size()),
                               mipLevel)
                ->submit();
        }
        for (uint mipLevel = 0u; mipLevel < uint(normalMips.size()); ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(normalTexture,
                               normalMips[mipLevel].data(),
                               uint64_t(normalMips[mipLevel].size()),
                               mipLevel)
                ->submit();
        }
        resources = device->createBindGroup<WebglMaterialsMatcapResources>(
            uniformBuffer,
            exrMatcapTexture->createView(),
            droppedMatcapTexture->createView(),
            normalTexture->createView(),
            matcapSampler,
            normalSampler);
        mainPass = device->createRenderClass<WebglMaterialsMatcapMainPass>(resources);
    }

    /** Draws the one canonical mesh and presents the tone-mapped RGBA8 result. */
    void render() override
    {
        WebglMaterialsMatcapFrameBuffer frameBuffer;
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
                "WebglMaterialsMatcapMain",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
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

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases every dedicated standalone matcap resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(exrMatcapTexture);
        device->freeTexture(droppedMatcapTexture);
        device->freeTexture(normalTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
