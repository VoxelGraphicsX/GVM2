#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS_HPP

#include "UGL.h"
#include "WebglCustomAttributesPointsVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglCustomAttributesPointsLogicalPointCount = 100000u;
static const uint WebglCustomAttributesPointsVertexCount =
    WebglCustomAttributesPointsLogicalPointCount * 4u;
static const uint WebglCustomAttributesPointsIndexCount =
    WebglCustomAttributesPointsLogicalPointCount * 6u;
static const uint WebglCustomAttributesPointsTextureExtent = 32u;
static const uint WebglCustomAttributesPointsTextureMipCount = 6u;
static const uint WebglCustomAttributesPointsOutputWidth = 800u;
static const uint WebglCustomAttributesPointsOutputHeight = 500u;

/** Stores Three's separate projection and model-view transforms. */
struct WebglCustomAttributesPointsUniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Binds transforms, dynamic logical-point sizes, and the raw spark texture. */
struct WebglCustomAttributesPointsBindGroup final : public IBindGroup
{
    /** Declares every existing DSL resource read by the private point shader. */
    constructor(
        UniformBuffer<WebglCustomAttributesPointsUniforms> uniforms [[Binding0]],
        StructuredBuffer<float> sizes [[Binding1]],
        Texture2D<float4> pointTexture [[Binding2]],
        Sampler pointSampler [[Binding3]])
    {
    }
};

/** Carries interpolated custom color and expanded local PointCoord into fragments. */
struct WebglCustomAttributesPointsVertexOutput
{
    float4 position [[Position]];
    float3 customColor [[Attribute0]];
    float2 pointCoord [[Attribute1]];
};

/** Defines the direct single-sample additive RGBA8 target. */
struct WebglCustomAttributesPointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws all 100,000 CPU-expanded sprites as one ordinary indexed object. */
class WebglCustomAttributesPointsMainPass final : public IRenderClass
{
public:
    /** Reproduces AdditiveBlending while disabling point depth testing and writes. */
    constructor(BindGroup<WebglCustomAttributesPointsBindGroup> bindGroup [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Applies r185 perspective point sizing and noninstanced billboard expansion. */
    WebglCustomAttributesPointsVertexOutput vertex(
        uint vertexID [[VertexID]],
        WebglCustomAttributesPointsVertex inputValue [[VertexInput0]])
    {
        const uint logicalPointIndex = vertexID / 4u;
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelViewMatrix,
            float4(inputValue.position, 1.0f));
        const float pointSize = max(
            bindGroup->sizes[logicalPointIndex] * (300.0f / -viewPosition.z),
            1.0f);
        float4 clipPosition = mul(
            bindGroup->uniforms->projectionMatrix,
            viewPosition);
        clipPosition.x +=
            inputValue.corner.x * pointSize /
            float(WebglCustomAttributesPointsOutputWidth) * clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * pointSize /
            float(WebglCustomAttributesPointsOutputHeight) * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglCustomAttributesPointsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.customColor = inputValue.customColor;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        return outputValue;
    }

    /** Multiplies raw spark RGBA by custom color without an output transfer function. */
    WebglCustomAttributesPointsFrameBuffer fragment(
        WebglCustomAttributesPointsVertexOutput inputValue)
    {
        const float4 textureColor = bindGroup->pointTexture->sample(
            bindGroup->pointSampler,
            inputValue.pointCoord);
        const float4 fragmentColor =
            float4(inputValue.customColor, 1.0f) * textureColor;

        WebglCustomAttributesPointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(fragmentColor);
        return frameBuffer;
    }
};

/** Owns all DSL-created resources and GPU work for webgl_custom_attributes_points. */
class WebglCustomAttributesPointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglCustomAttributesPointsVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<float, BufferUsage<Storage, CopyDst>> sizeBuffer;
    Buffer<WebglCustomAttributesPointsUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        pointTexture;
    Sampler pointSampler;
    BindGroup<WebglCustomAttributesPointsBindGroup> bindGroup;
    RenderClass<WebglCustomAttributesPointsMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    bool drawCurrentFrame = false;

public:
    /** Creates fixed ordinary buffers and the current trilinear ClampToEdge sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglCustomAttributesPointsVertices",
            WebglCustomAttributesPointsVertexCount);
        indexBuffer = device->createBuffer(
            "WebglCustomAttributesPointsIndices",
            WebglCustomAttributesPointsIndexCount);
        sizeBuffer = device->createBuffer(
            "WebglCustomAttributesPointsSizes",
            WebglCustomAttributesPointsLogicalPointCount);
        uniformBuffer = device->createBuffer("WebglCustomAttributesPointsUniforms", 1u);
        pointSampler = device->createSampler({
            .label = "WebglCustomAttributesPointsSparkSampler",
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

    /** Allocates the direct single-sample RGBA8 render target. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglCustomAttributesPointsOutputWidth ||
            height != WebglCustomAttributesPointsOutputHeight)
        {
            return;
        }
        outputTexture = device->createTexture(
            "WebglCustomAttributesPointsOutput", width, height, 1u);
    }

    /** Uploads immutable expanded geometry and the complete raw-UNORM spark mip chain. */
    void configureScene(
        const eastl::vector<WebglCustomAttributesPointsVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &textureMips,
        float4x4 projectionMatrix)
    {
        pointTexture = device->createTexture(
            "WebglCustomAttributesPointsSparkTexture",
            WebglCustomAttributesPointsTextureExtent,
            WebglCustomAttributesPointsTextureExtent,
            1u,
            uint(textureMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglCustomAttributesPointsVertexCount) *
                    sizeof(WebglCustomAttributesPointsVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglCustomAttributesPointsIndexCount) * sizeof(uint));
        for (uint mipLevel = 0u; mipLevel < uint(textureMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                pointTexture,
                textureMips[mipLevel].data(),
                uint64_t(textureMips[mipLevel].size()),
                mipLevel);
        }

        WebglCustomAttributesPointsUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = float4x4(1.0f);
        graphicsQueue
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();

        bindGroup = device->createBindGroup<WebglCustomAttributesPointsBindGroup>(
            uniformBuffer,
            sizeBuffer,
            pointTexture->createView(),
            pointSampler);
        mainPass = device->createRenderClass<WebglCustomAttributesPointsMainPass>(
            bindGroup);
    }

    /** Uploads one sequential size wave and the target-frame model-view transform. */
    void updateFrame(
        const eastl::vector<float> &sizes,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix,
        bool shouldDraw)
    {
        WebglCustomAttributesPointsUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = modelViewMatrix;
        graphicsQueue
            ->writeBuffer(
                BufferRange(sizeBuffer),
                sizes.data(),
                uint64_t(WebglCustomAttributesPointsLogicalPointCount) * sizeof(float))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        drawCurrentFrame = shouldDraw;
    }

    /** Draws one explicit indexed object only at the requested deterministic frame. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (drawCurrentFrame)
        {
            WebglCustomAttributesPointsFrameBuffer frameBuffer;
            frameBuffer.color = outputTexture->createView();
            frameBuffer.color.loadOp = LoadOp::Clear;
            frameBuffer.color.storeOp = StoreOp::Store;
            frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

            graphicsQueue
                ->renderPass(
                    "WebglCustomAttributesPointsScene",
                    frameBuffer,
                    mainPass->setVertexBuffer(vertexBuffer),
                    mainPass->setIndexBuffer(indexBuffer),
                    mainPass(
                        WebglCustomAttributesPointsIndexCount,
                        1u,
                        0u,
                        0,
                        0u))
                ->submit();
            drawCurrentFrame = false;
        }

        graphicsQueue
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

    /** Returns the locked output width. */
    uint getReadbackWidth() const
    {
        return WebglCustomAttributesPointsOutputWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return WebglCustomAttributesPointsOutputHeight;
    }

    /** Releases all standalone buffers, the spark texture, and the output texture. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(sizeBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(pointTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
