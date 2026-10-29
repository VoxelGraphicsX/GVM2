#ifndef GVM_THREE_PHASE1_LINES_POINTS_SIMPLE_HPP
#define GVM_THREE_PHASE1_LINES_POINTS_SIMPLE_HPP

#include "UGL.h"
#include "Phase1LinesPointsSimpleData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglPointsBillboardsPointCount = 10000u;
static const uint WebglPointsBillboardsVertexCount = 40000u;
static const uint WebglPointsBillboardsIndexCount = 60000u;
static const uint WebglPointsBillboardsTextureExtent = 32u;

/** Binds camera, material, sprite texture, and sampler resources. */
struct WebglPointsBillboardsResources final : public IBindGroup
{
    /** Declares every resource used by the private PointsMaterial equivalent. */
    constructor(UniformBuffer<WebglPointsBillboardsUniforms> uniforms [[Binding0]],
                Texture2D<float4> pointTexture [[Binding1]],
                Sampler pointSampler [[Binding2]])
    {
    }
};

/** Carries local PointCoord, fog depth, and material color into fragments. */
struct WebglPointsBillboardsVertexOutput
{
    float4 position [[Position]];
    float2 pointCoord [[Attribute0]];
    float fogDepth [[Attribute1]];
    float pointSize [[Attribute2]];
};

/** Defines the canonical RGBA8 and depth output attachments. */
struct WebglPointsBillboardsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Decodes one standard sRGB channel into Three's linear working space. */
float webglPointsBillboardsSrgbToLinear(float value)
{
    if (value <= 0.04045f)
    {
        return value * 0.0773993808f;
    }
    return pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
}

/** Encodes one linear working-space channel through Three's output transfer. */
float webglPointsBillboardsLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces one native Points renderable with triangle-expanded sprites. */
class WebglPointsBillboardsMainPass final : public IRenderClass
{
public:
    /** Reproduces transparent NormalBlending and default point depth behavior. */
    constructor(BindGroup<WebglPointsBillboardsResources> resources [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies native point-size quantization and quad expansion. */
    WebglPointsBillboardsVertexOutput vertex(WebglPointsBillboardsVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition = resources->uniforms->modelViewColumn0 * inputValue.center.x +
                                    resources->uniforms->modelViewColumn1 * inputValue.center.y +
                                    resources->uniforms->modelViewColumn2 * inputValue.center.z +
                                    resources->uniforms->modelViewColumn3 * inputValue.center.w;
        float4 clipPosition = resources->uniforms->projectionColumn0 * viewPosition.x +
                              resources->uniforms->projectionColumn1 * viewPosition.y +
                              resources->uniforms->projectionColumn2 * viewPosition.z +
                              resources->uniforms->projectionColumn3 * viewPosition.w;
        const float attenuationScale = resources->uniforms->attenuationAndFog.x;
        const float rawSize = resources->uniforms->materialColorAndSize.w *
                              (attenuationScale > 0.5f ? 250.0f / -viewPosition.z : 1.0f);
        const float pointSize = floor(clamp(rawSize, 1.0f, 511.0f) * 16.0f + 0.5f) / 16.0f;
        const float expansionScale = attenuationScale > 0.5f ? 1.02f : 1.0f;
        clipPosition.x += inputValue.corner.x * pointSize * expansionScale / 800.0f * clipPosition.w;
        clipPosition.y += inputValue.corner.y * pointSize * expansionScale / 500.0f * clipPosition.w;
        WebglPointsBillboardsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.pointCoord = float2(inputValue.corner.x * 0.5f + 0.5f,
                                        0.5f - inputValue.corner.y * 0.5f);
        outputValue.fogDepth = -viewPosition.z;
        outputValue.pointSize = pointSize;
        return outputValue;
    }

    /** Samples the sRGB disc, applies alpha test, fog, output transfer, and blending. */
    WebglPointsBillboardsFrameBuffer fragment(WebglPointsBillboardsVertexOutput inputValue)
    {
        const float pointLod =
            clamp(
                log2(32.0f / inputValue.pointSize),
                0.0f,
                5.0f);
        const float4 sampled = resources->pointTexture->sampleLevel(resources->pointSampler, inputValue.pointCoord, pointLod);
        if (sampled.w < 0.5f)
        {
            discard_fragment();
        }
        const float3 textureLinear = float3(webglPointsBillboardsSrgbToLinear(sampled.x),
                                            webglPointsBillboardsSrgbToLinear(sampled.y),
                                            webglPointsBillboardsSrgbToLinear(sampled.z));
        const float3 linearColor = textureLinear * resources->uniforms->materialColorAndSize.xyz;
        const float fogDensity = resources->uniforms->attenuationAndFog.y;
        const float fogFactor = 1.0f - exp(-fogDensity * fogDensity * inputValue.fogDepth * inputValue.fogDepth);
        const float3 outputColor = float3(webglPointsBillboardsLinearToSrgb(linearColor.x),
                                          webglPointsBillboardsLinearToSrgb(linearColor.y),
                                          webglPointsBillboardsLinearToSrgb(linearColor.z));
        WebglPointsBillboardsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(lerp(outputColor, float3(0.0f), fogFactor)), half(sampled.w));
        return frameBuffer;
    }
};

/** Owns the one simple point Scene and its DSL-created resources. */
class Phase1LinesPointsSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglPointsBillboardsVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglPointsBillboardsUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> pointTexture;
    Sampler pointSampler;
    BindGroup<WebglPointsBillboardsResources> resources;
    RenderClass<WebglPointsBillboardsMainPass> mainPass;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;

public:
    /** Creates standalone geometry, material, and sprite resources through the DSL. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer("WebglPointsBillboardsVertices", WebglPointsBillboardsVertexCount);
        indexBuffer = device->createBuffer("WebglPointsBillboardsIndices", WebglPointsBillboardsIndexCount);
        uniformBuffer = device->createBuffer("WebglPointsBillboardsUniforms", 1u);
        pointSampler = device->createSampler({
            .label = "WebglPointsBillboardsSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 5.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        sceneDepth = device->createTexture("WebglPointsBillboardsDepth", width, height, 1u);
        outputColor = device->createTexture("WebglPointsBillboardsOutput", width, height, 1u);
    }

    /** Uploads immutable expanded points, CPU-authored mips, and canonical frame state. */
    void configureScene(const eastl::vector<WebglPointsBillboardsVertex> &vertices,
                        const eastl::vector<uint> &indices,
                        const eastl::vector<eastl::vector<uint8_t>> &textureMips,
                        float mv00, float mv01, float mv02, float mv03,
                        float mv10, float mv11, float mv12, float mv13,
                        float mv20, float mv21, float mv22, float mv23,
                        float mv30, float mv31, float mv32, float mv33,
                        float p00, float p01, float p02, float p03,
                        float p10, float p11, float p12, float p13,
                        float p20, float p21, float p22, float p23,
                        float p30, float p31, float p32, float p33,
                        float red, float green, float blue,
                        float sizeAttenuation)
    {
        pointTexture = device->createTexture("WebglPointsBillboardsDisc", WebglPointsBillboardsTextureExtent,
                                             WebglPointsBillboardsTextureExtent, 1u, uint(textureMips.size()));
        for (uint mipLevel = 0u; mipLevel < uint(textureMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(pointTexture, textureMips[mipLevel].data(),
                                        uint64_t(textureMips[mipLevel].size()), mipLevel);
        }
        WebglPointsBillboardsUniforms uniforms;
        uniforms.modelViewColumn0 = float4(mv00, mv01, mv02, mv03);
        uniforms.modelViewColumn1 = float4(mv10, mv11, mv12, mv13);
        uniforms.modelViewColumn2 = float4(mv20, mv21, mv22, mv23);
        uniforms.modelViewColumn3 = float4(mv30, mv31, mv32, mv33);
        uniforms.projectionColumn0 = float4(p00, p01, p02, p03);
        uniforms.projectionColumn1 = float4(p10, p11, p12, p13);
        uniforms.projectionColumn2 = float4(p20, p21, p22, p23);
        uniforms.projectionColumn3 = float4(p30, p31, p32, p33);
        uniforms.materialColorAndSize = float4(red, green, blue, 35.0f);
        uniforms.attenuationAndFog = float4(sizeAttenuation, 0.001f, 0.0f, 0.0f);
        uniforms.reserved = float4(0.0f);
        graphicsQueue->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                                   uint64_t(vertices.size()) * sizeof(WebglPointsBillboardsVertex))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        resources = device->createBindGroup<WebglPointsBillboardsResources>(
            uniformBuffer, pointTexture->createView(), pointSampler);
        mainPass = device->createRenderClass<WebglPointsBillboardsMainPass>(resources);
    }

    /** Draws the one logical Points object through one ordinary indexed RenderClass. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglPointsBillboardsFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = sceneDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue->renderPass(
                "WebglPointsBillboardsScene",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(
                    WebglPointsBillboardsIndexCount,
                    1u,
                    0u,
                    0,
                    0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created color target for readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the canonical output width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the canonical output height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases the private single-sample point resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(pointTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
