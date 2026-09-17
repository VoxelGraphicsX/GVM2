#ifndef GVM_THREE_WEBGL_MATERIALS_NORMALMAP_OBJECT_SPACE_HPP
#define GVM_THREE_WEBGL_MATERIALS_NORMALMAP_OBJECT_SPACE_HPP

#include "WebglMaterialsNormalmapObjectSpaceData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the two embedded Nefertiti textures and immutable camera state. */
struct WebglMaterialsNormalmapObjectSpaceResources final : public IBindGroup
{
    /** Declares the complete private object-space normal material resource set. */
    constructor(
        UniformBuffer<WebglMaterialsNormalmapObjectSpaceUniforms> uniforms [[Binding0]],
        Texture2D<float4> baseColor [[Binding1]],
        Texture2D<float4> objectNormal [[Binding2]],
        Sampler materialSampler [[Binding3]])
    {
    }
};

/** Carries perspective-correct UV and view-space position to Standard shading. */
struct WebglMaterialsNormalmapObjectSpaceVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float2 textureCoordinate [[Attribute1]];
};

/** Defines the single-sample r185 color and depth attachments. */
struct WebglMaterialsNormalmapObjectSpaceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear-light channel through Three's default output transfer. */
float webglMaterialsNormalmapObjectSpaceLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglMaterialsNormalmapObjectSpaceFresnel(
    float3 f0,
    float dotViewHalf)
{
    const float factor = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates Three r185's direct GGX BRDF at the authored roughness. */
float3 webglMaterialsNormalmapObjectSpaceGgx(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal,
    float roughness)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float normalDotHalf = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float viewDotHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    const float distribution =
        alphaSquared /
        max(3.141592653589793f * denominator * denominator, 0.000001f);
    const float visibility =
        0.5f /
        max(
            normalDotLight *
                    sqrt(alphaSquared +
                         (1.0f - alphaSquared) *
                             normalDotView * normalDotView) +
                normalDotView *
                    sqrt(alphaSquared +
                         (1.0f - alphaSquared) *
                             normalDotLight * normalDotLight),
            0.000001f);
    return webglMaterialsNormalmapObjectSpaceFresnel(
               float3(0.04f), viewDotHalf) *
           (visibility * distribution);
}

/** Applies the recentered model and selected Orbit camera. */
WebglMaterialsNormalmapObjectSpaceVertexOutput
webglMaterialsNormalmapObjectSpaceTransform(
    WebglMaterialsNormalmapObjectSpaceVertex inputValue,
    float4x4 modelViewProjection,
    float4x4 modelView,
    float2 sampleOffset)
{
    const float4 localPosition = float4(inputValue.position, 1.0f);
    const float4 viewPosition =
        mul(modelView, localPosition);
    WebglMaterialsNormalmapObjectSpaceVertexOutput outputValue;
    outputValue.position =
        mul(modelViewProjection, localPosition);
    outputValue.position.y = -outputValue.position.y;
    outputValue.position.z =
        (outputValue.position.z + outputValue.position.w) * 0.5f;
    outputValue.position.x -=
        sampleOffset.x * 2.0f / 800.0f *
        outputValue.position.w;
    outputValue.position.y -=
        sampleOffset.y * 2.0f / 500.0f *
        outputValue.position.w;
    outputValue.viewPosition = viewPosition.xyz;
    outputValue.textureCoordinate = inputValue.textureCoordinate;
    return outputValue;
}

/** Shades one face using the embedded base color and object-space normal map. */
float4 webglMaterialsNormalmapObjectSpaceShade(
    float3 baseColor,
    float3 objectNormal,
    float3 viewPosition,
    float4x4 modelView,
    float3 ambientPointRoughness,
    float faceSign)
{
    objectNormal *= faceSign;
    const float4 transformedNormal4 = mul(
        modelView,
        float4(objectNormal, 0.0f));
    const float3 transformedNormal = float3(
        transformedNormal4.x,
        transformedNormal4.y,
        transformedNormal4.z);
    const float3 viewNormal = normalize(transformedNormal);
    const float3 viewDirection = normalize(-viewPosition);
    const float3 lightDirection = viewDirection;
    const float normalDotLight =
        clamp(dot(viewNormal, lightDirection), 0.0f, 1.0f);
    const float pointIntensity =
        ambientPointRoughness.y;
    const float ambientIntensity =
        ambientPointRoughness.x;
    const float roughness =
        ambientPointRoughness.z;
    const float inversePi = 0.3183098861837907f;
    const float3 diffuse =
        baseColor *
        (ambientIntensity + pointIntensity * normalDotLight) *
        inversePi;
    const float3 specular =
        pointIntensity * normalDotLight *
        webglMaterialsNormalmapObjectSpaceGgx(
            lightDirection,
            viewDirection,
            viewNormal,
            roughness);
    const float3 linearColor = diffuse + specular;
    return float4(
        webglMaterialsNormalmapObjectSpaceLinearToSrgb(linearColor.x),
        webglMaterialsNormalmapObjectSpaceLinearToSrgb(linearColor.y),
        webglMaterialsNormalmapObjectSpaceLinearToSrgb(linearColor.z),
        1.0f);
}

/** Renders the first back-facing phase of Three's DoubleSide material. */
class WebglMaterialsNormalmapObjectSpaceBackFacesPass final
    : public IRenderClass
{
public:
    /** Configures opaque back-face shading against the shared depth target. */
    constructor(
        BindGroup<WebglMaterialsNormalmapObjectSpaceResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the immutable model-view-projection transform. */
    WebglMaterialsNormalmapObjectSpaceVertexOutput vertex(
        WebglMaterialsNormalmapObjectSpaceVertex inputValue [[VertexInput0]])
    {
        return webglMaterialsNormalmapObjectSpaceTransform(
            inputValue,
            resources->uniforms->modelViewProjection,
            resources->uniforms->modelView,
            resources->uniforms->sampleOffsetAndPadding.xy);
    }

    /** Reverses the decoded object normal for the back-facing material phase. */
    WebglMaterialsNormalmapObjectSpaceFrameBuffer fragment(
        WebglMaterialsNormalmapObjectSpaceVertexOutput inputValue)
    {
        WebglMaterialsNormalmapObjectSpaceFrameBuffer frameBuffer;
        const float2 uv = float2(
            inputValue.textureCoordinate.x,
            1.0f - inputValue.textureCoordinate.y);
        const float3 baseColor = float3(
            resources->baseColor->sampleLevel(
                resources->materialSampler,
                uv,
                0.0f).xyz);
        const float3 objectNormal =
            float3(resources->objectNormal->sampleLevel(
                resources->materialSampler,
                uv,
                0.0f).xyz) * 2.0f - float3(1.0f);
        const float4 shaded = webglMaterialsNormalmapObjectSpaceShade(
            baseColor,
            objectNormal,
            inputValue.viewPosition,
            resources->uniforms->modelView,
            resources->uniforms->ambientPointRoughness.xyz,
            -1.0f);
        frameBuffer.color = half4(shaded);
        return frameBuffer;
    }
};

/** Renders the second front-facing phase of Three's DoubleSide material. */
class WebglMaterialsNormalmapObjectSpaceFrontFacesPass final
    : public IRenderClass
{
public:
    /** Configures opaque front-face shading against the preserved depth target. */
    constructor(
        BindGroup<WebglMaterialsNormalmapObjectSpaceResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the immutable model-view-projection transform. */
    WebglMaterialsNormalmapObjectSpaceVertexOutput vertex(
        WebglMaterialsNormalmapObjectSpaceVertex inputValue [[VertexInput0]])
    {
        return webglMaterialsNormalmapObjectSpaceTransform(
            inputValue,
            resources->uniforms->modelViewProjection,
            resources->uniforms->modelView,
            resources->uniforms->sampleOffsetAndPadding.xy);
    }

    /** Preserves the decoded object normal for the front-facing material phase. */
    WebglMaterialsNormalmapObjectSpaceFrameBuffer fragment(
        WebglMaterialsNormalmapObjectSpaceVertexOutput inputValue)
    {
        WebglMaterialsNormalmapObjectSpaceFrameBuffer frameBuffer;
        const float2 uv = float2(
            inputValue.textureCoordinate.x,
            1.0f - inputValue.textureCoordinate.y);
        const float3 baseColor = float3(
            resources->baseColor->sampleLevel(
                resources->materialSampler,
                uv,
                0.0f).xyz);
        const float3 objectNormal =
            float3(resources->objectNormal->sampleLevel(
                resources->materialSampler,
                uv,
                0.0f).xyz) * 2.0f - float3(1.0f);
        const float4 shaded = webglMaterialsNormalmapObjectSpaceShade(
            baseColor,
            objectNormal,
            inputValue.viewPosition,
            resources->uniforms->modelView,
            resources->uniforms->ambientPointRoughness.xyz,
            1.0f);
        frameBuffer.color = half4(shaded);
        return frameBuffer;
    }
};

/** Owns the dedicated single-mesh, dual-pass object-space normal scene. */
class WebglMaterialsNormalmapObjectSpaceRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsNormalmapObjectSpaceVertex,
           BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsNormalmapObjectSpaceUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> baseColorTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> objectNormalTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler materialSampler;
    BindGroup<WebglMaterialsNormalmapObjectSpaceResources> resources;
    RenderClass<WebglMaterialsNormalmapObjectSpaceBackFacesPass> backFacesPass;
    RenderClass<WebglMaterialsNormalmapObjectSpaceFrontFacesPass> frontFacesPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the uniform and authored trilinear material sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer(
            "WebglMaterialsNormalmapObjectSpaceUniforms", 1u);
        materialSampler = device->createSampler({
            .label = "WebglMaterialsNormalmapObjectSpaceSampler",
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

    /** Allocates the ordinary single-sample output and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglMaterialsNormalmapObjectSpaceOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglMaterialsNormalmapObjectSpaceDepth", width, height, 1u);
    }

    /** Uploads the decoded mesh, explicit texture mips, and selected camera. */
    void configureScene(
        const eastl::vector<WebglMaterialsNormalmapObjectSpaceVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &baseColorMips,
        const eastl::vector<eastl::vector<uint8_t>> &objectNormalMips,
        uint textureWidth,
        uint textureHeight,
        WebglMaterialsNormalmapObjectSpaceUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer(
            "WebglMaterialsNormalmapObjectSpaceVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebglMaterialsNormalmapObjectSpaceIndices", indexCount);
        baseColorTexture = device->createTexture(
            "WebglMaterialsNormalmapObjectSpaceBaseColor",
            textureWidth, textureHeight, 1u, uint(baseColorMips.size()));
        objectNormalTexture = device->createTexture(
            "WebglMaterialsNormalmapObjectSpaceNormal",
            textureWidth, textureHeight, 1u, uint(objectNormalMips.size()));
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                          uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        for (uint mipLevel = 0u; mipLevel < uint(baseColorMips.size()); ++mipLevel)
        {
            graphicsQueue
                ->writeTexture(baseColorTexture,
                               baseColorMips[mipLevel].data(),
                               uint64_t(baseColorMips[mipLevel].size()),
                               mipLevel)
                ->writeTexture(objectNormalTexture,
                               objectNormalMips[mipLevel].data(),
                               uint64_t(objectNormalMips[mipLevel].size()),
                               mipLevel)
                ->submit();
        }
        resources = device->createBindGroup<
            WebglMaterialsNormalmapObjectSpaceResources>(
                uniformBuffer,
                baseColorTexture->createView(),
                objectNormalTexture->createView(),
                materialSampler);
        backFacesPass = device->createRenderClass<
            WebglMaterialsNormalmapObjectSpaceBackFacesPass>(resources);
        frontFacesPass = device->createRenderClass<
            WebglMaterialsNormalmapObjectSpaceFrontFacesPass>(resources);
    }

    /** Draws the two DoubleSide phases and presents the RGBA8 result. */
    void render() override
    {
        WebglMaterialsNormalmapObjectSpaceFrameBuffer clearFrameBuffer;
        clearFrameBuffer.color = outputTexture->createView();
        clearFrameBuffer.color.loadOp = LoadOp::Clear;
        clearFrameBuffer.color.storeOp = StoreOp::Store;
        clearFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        clearFrameBuffer.depth = depthTexture->createView();
        clearFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        clearFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        clearFrameBuffer.depth.depthClearValue = 1.0f;
        WebglMaterialsNormalmapObjectSpaceFrameBuffer loadFrameBuffer;
        loadFrameBuffer.color = outputTexture->createView();
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.color.storeOp = StoreOp::Store;
        loadFrameBuffer.depth = depthTexture->createView();
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglMaterialsNormalmapObjectSpaceBackFaces",
                clearFrameBuffer,
                backFacesPass->setVertexBuffer(vertexBuffer),
                backFacesPass->setIndexBuffer(indexBuffer),
                backFacesPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebglMaterialsNormalmapObjectSpaceFrontFaces",
                loadFrameBuffer,
                frontFacesPass->setVertexBuffer(vertexBuffer),
                frontFacesPass->setIndexBuffer(indexBuffer),
                frontFacesPass(indexCount, 1u, 0u, 0, 0u))
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

    /** Releases every dedicated standalone scene resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(baseColorTexture);
        device->freeTexture(objectNormalTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
