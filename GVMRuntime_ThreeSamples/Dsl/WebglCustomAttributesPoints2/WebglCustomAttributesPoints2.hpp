#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS2_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS2_HPP

#include "UGL.h"
#include "WebglCustomAttributesPoints2VertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglCustomAttributesPoints2LogicalPointCount = 3120u;
static const uint WebglCustomAttributesPoints2VertexCount =
    WebglCustomAttributesPoints2LogicalPointCount * 4u;
static const uint WebglCustomAttributesPoints2IndexCount =
    WebglCustomAttributesPoints2LogicalPointCount * 6u;
static const uint WebglCustomAttributesPoints2TextureExtent = 32u;
static const uint WebglCustomAttributesPoints2TextureMipCount = 6u;

/** Stores the current Three projection and model-view matrices. */
struct WebglCustomAttributesPoints2Uniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Binds transforms, dynamic sizes, and the raw Repeat-wrapped disc texture. */
struct WebglCustomAttributesPoints2BindGroup final : public IBindGroup
{
    /** Declares all resources read by the private ShaderMaterial equivalent. */
    constructor(
        UniformBuffer<WebglCustomAttributesPoints2Uniforms> uniforms [[Binding0]],
        StructuredBuffer<float> sizes [[Binding1]],
        Texture2D<float4> pointTexture [[Binding2]],
        Sampler pointSampler [[Binding3]])
    {
    }
};

/** Carries the immutable custom color and expanded PointCoord to fragments. */
struct WebglCustomAttributesPoints2VertexOutput
{
    float4 position [[Position]];
    float3 customColor [[Attribute0]];
    float2 pointCoord [[Attribute1]];
};

/** Defines the direct RGBA8 color target and default WebGL depth attachment. */
struct WebglCustomAttributesPoints2FrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the one stable-sorted Points object as ordinary indexed triangles. */
class WebglCustomAttributesPoints2MainPass final : public IRenderClass
{
public:
    /** Reproduces transparent NormalBlending and the material's default depth state. */
    constructor(BindGroup<WebglCustomAttributesPoints2BindGroup> bindGroup [[Slot0]])
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
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies depth attenuation, four-bit WebGL point quantization, and quad expansion. */
    WebglCustomAttributesPoints2VertexOutput vertex(
        uint vertexID [[VertexID]],
        WebglCustomAttributesPoints2Vertex inputValue [[VertexInput0]])
    {
        const uint logicalPointIndex = vertexID / 4u;
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelViewMatrix,
            float4(inputValue.position, 1.0f));
        float4 clipPosition = mul(
            bindGroup->uniforms->projectionMatrix,
            viewPosition);

        const float rawPointSize =
            bindGroup->sizes[logicalPointIndex] *
            (300.0f / -viewPosition.z);
        const float pointSize =
            floor(clamp(rawPointSize, 1.0f, 511.0f) * 16.0f + 0.5f) /
            16.0f;
        float2 pointCenter =
            (clipPosition.xy / clipPosition.w + float2(1.0f)) *
            float2(400.0f, 250.0f);
        pointCenter =
            floor(pointCenter * 16.0f + float2(0.5f)) / 16.0f;
        clipPosition.x =
            (pointCenter.x / 400.0f - 1.0f) * clipPosition.w;
        clipPosition.y =
            (pointCenter.y / 250.0f - 1.0f) * clipPosition.w;
        clipPosition.x +=
            inputValue.corner.x * pointSize / 800.0f * clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * pointSize / 500.0f * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglCustomAttributesPoints2VertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.customColor = inputValue.customColor;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        return outputValue;
    }

    /** Multiplies the raw disc texel by the upstream per-point HSL color. */
    WebglCustomAttributesPoints2FrameBuffer fragment(
        WebglCustomAttributesPoints2VertexOutput inputValue)
    {
        const float4 textureColor = bindGroup->pointTexture->sample(
            bindGroup->pointSampler,
            inputValue.pointCoord);
        const float4 fragmentColor =
            float4(inputValue.customColor, 1.0f) * textureColor;

        WebglCustomAttributesPoints2FrameBuffer frameBuffer;
        frameBuffer.color = half4(fragmentColor);
        return frameBuffer;
    }
};

/** Owns all DSL-created resources and GPU commands for the sorted point sample. */
class WebglCustomAttributesPoints2Renderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglCustomAttributesPoints2Vertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<float, BufferUsage<Storage, CopyDst>> sizeBuffer;
    Buffer<WebglCustomAttributesPoints2Uniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        pointTexture;
    Sampler pointSampler;
    BindGroup<WebglCustomAttributesPoints2BindGroup> bindGroup;
    RenderClass<WebglCustomAttributesPoints2MainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates ordinary standalone buffers and the existing Repeat trilinear sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglCustomAttributesPoints2Vertices",
            WebglCustomAttributesPoints2VertexCount);
        indexBuffer = device->createBuffer(
            "WebglCustomAttributesPoints2Indices",
            WebglCustomAttributesPoints2IndexCount);
        sizeBuffer = device->createBuffer(
            "WebglCustomAttributesPoints2Sizes",
            WebglCustomAttributesPoints2LogicalPointCount);
        uniformBuffer = device->createBuffer(
            "WebglCustomAttributesPoints2Uniforms",
            1u);
        pointSampler = device->createSampler({
            .label = "WebglCustomAttributesPoints2DiscSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 5.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the exact direct RGBA8 output and independent depth attachment. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglCustomAttributesPoints2Output",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglCustomAttributesPoints2Depth",
            width,
            height,
            1u);
    }

    /** Uploads immutable expanded vertices and the complete raw-UNORM disc mip chain. */
    void configureScene(
        const eastl::vector<WebglCustomAttributesPoints2Vertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &textureMips)
    {
        pointTexture = device->createTexture(
            "WebglCustomAttributesPoints2DiscTexture",
            WebglCustomAttributesPoints2TextureExtent,
            WebglCustomAttributesPoints2TextureExtent,
            1u,
            uint(textureMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglCustomAttributesPoints2VertexCount) *
                    sizeof(WebglCustomAttributesPoints2Vertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglCustomAttributesPoints2IndexCount) * sizeof(uint));
        for (uint mipLevel = 0u; mipLevel < uint(textureMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                pointTexture,
                textureMips[mipLevel].data(),
                uint64_t(textureMips[mipLevel].size()),
                mipLevel);
        }
        graphicsQueue->submit();

        bindGroup = device->createBindGroup<WebglCustomAttributesPoints2BindGroup>(
            uniformBuffer,
            sizeBuffer,
            pointTexture->createView(),
            pointSampler);
        mainPass =
            device->createRenderClass<WebglCustomAttributesPoints2MainPass>(
                bindGroup);
    }

    /** Uploads one callback's sizes, stable point ordering, and current render matrices. */
    void updateFrame(
        const eastl::vector<float> &sizes,
        const eastl::vector<uint> &indices,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix)
    {
        WebglCustomAttributesPoints2Uniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = modelViewMatrix;
        graphicsQueue
            ->writeBuffer(
                BufferRange(sizeBuffer),
                sizes.data(),
                uint64_t(WebglCustomAttributesPoints2LogicalPointCount) *
                    sizeof(float))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglCustomAttributesPoints2IndexCount) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
    }

    /** Draws exactly one ordinary indexed Scene object and presents its DSL output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglCustomAttributesPoints2FrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglCustomAttributesPoints2Scene",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(
                    WebglCustomAttributesPoints2IndexCount,
                    1u,
                    0u,
                    0,
                    0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 texture used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases all ordinary buffers, textures, and depth resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(sizeBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(pointTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
