#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS3_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS3_HPP

#include "UGL.h"
#include "WebglCustomAttributesPoints3VertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglCustomAttributesPoints3LogicalPointCount = 91859u;
static const uint WebglCustomAttributesPoints3VertexCount =
    WebglCustomAttributesPoints3LogicalPointCount * 4u;
static const uint WebglCustomAttributesPoints3IndexCount =
    WebglCustomAttributesPoints3LogicalPointCount * 6u;
static const uint WebglCustomAttributesPoints3TextureExtent = 64u;
static const uint WebglCustomAttributesPoints3TextureMipCount = 7u;
static const uint WebglCustomAttributesPoints3OutputWidth = 800u;
static const uint WebglCustomAttributesPoints3OutputHeight = 500u;

/** Stores the current Three projection and model-view transforms. */
struct WebglCustomAttributesPoints3Uniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
};

/** Binds transforms, dynamic logical sizes, and the raw Repeat-wrapped ball texture. */
struct WebglCustomAttributesPoints3BindGroup final : public IBindGroup
{
    /** Declares every existing DSL resource read by the private point shader. */
    constructor(
        UniformBuffer<WebglCustomAttributesPoints3Uniforms> uniforms [[Binding0]],
        StructuredBuffer<float> sizes [[Binding1]],
        Texture2D<float4> pointTexture [[Binding2]],
        Sampler pointSampler [[Binding3]])
    {
    }
};

/** Carries clip position, padded custom color, and local PointCoord into fragments. */
struct WebglCustomAttributesPoints3VertexOutput
{
    float4 position [[Position]];
    float4 customColor [[Attribute0]];
    float2 pointCoord [[Attribute1]];
};

/** Defines the direct single-sample RGBA8 target and default WebGL depth attachment. */
struct WebglCustomAttributesPoints3FrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the one alpha-tested and fogged Points object as ordinary indexed triangles. */
class WebglCustomAttributesPoints3MainPass final : public IRenderClass
{
public:
    /** Reproduces opaque ShaderMaterial depth behavior without triangle-face culling. */
    constructor(BindGroup<WebglCustomAttributesPoints3BindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies native point quantization, depth attenuation, and noninstanced expansion. */
    WebglCustomAttributesPoints3VertexOutput vertex(
        uint vertexID [[VertexID]],
        WebglCustomAttributesPoints3Vertex inputValue [[VertexInput0]])
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
            (150.0f / -viewPosition.z);
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
            inputValue.corner.x * pointSize /
            float(WebglCustomAttributesPoints3OutputWidth) * clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * pointSize /
            float(WebglCustomAttributesPoints3OutputHeight) * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglCustomAttributesPoints3VertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.customColor = inputValue.customColor;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        return outputValue;
    }

    /** Applies ball alpha discard and the authored fragment-depth black fog. */
    WebglCustomAttributesPoints3FrameBuffer fragment(
        WebglCustomAttributesPoints3VertexOutput inputValue)
    {
        const float4 textureColor = bindGroup->pointTexture->sample(
            bindGroup->pointSampler,
            inputValue.pointCoord);
        if (textureColor.a < 0.5f)
        {
            discard_fragment();
        }
        const float4 pointColor =
            textureColor * inputValue.customColor;
        const float fragmentDepth =
            inputValue.position.z / inputValue.position.w;
        const float fogFactor = smoothstep(200.0f, 600.0f, fragmentDepth);

        WebglCustomAttributesPoints3FrameBuffer frameBuffer;
        const float4 foggedColor = lerp(
            pointColor,
            float4(0.0f, 0.0f, 0.0f, pointColor.a),
            fogFactor);
        frameBuffer.color = half4(
            half3(foggedColor.rgb),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns all DSL-created resources and GPU work for webgl_custom_attributes_points3. */
class WebglCustomAttributesPoints3Renderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglCustomAttributesPoints3Vertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<float, BufferUsage<Storage, CopyDst>> sizeBuffer;
    Buffer<WebglCustomAttributesPoints3Uniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        pointTexture;
    Sampler pointSampler;
    BindGroup<WebglCustomAttributesPoints3BindGroup> bindGroup;
    RenderClass<WebglCustomAttributesPoints3MainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    bool drawCurrentFrame = false;

public:
    /** Creates ordinary buffers and the existing Repeat trilinear sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglCustomAttributesPoints3Vertices",
            WebglCustomAttributesPoints3VertexCount);
        indexBuffer = device->createBuffer(
            "WebglCustomAttributesPoints3Indices",
            WebglCustomAttributesPoints3IndexCount);
        sizeBuffer = device->createBuffer(
            "WebglCustomAttributesPoints3Sizes",
            WebglCustomAttributesPoints3LogicalPointCount);
        uniformBuffer = device->createBuffer(
            "WebglCustomAttributesPoints3Uniforms",
            1u);
        pointSampler = device->createSampler({
            .label = "WebglCustomAttributesPoints3BallSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 6.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the direct 800x500 single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglCustomAttributesPoints3OutputWidth ||
            height != WebglCustomAttributesPoints3OutputHeight)
        {
            return;
        }
        outputTexture = device->createTexture(
            "WebglCustomAttributesPoints3Output", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglCustomAttributesPoints3Depth", width, height, 1u);
    }

    /** Uploads immutable expanded geometry and the complete raw-UNORM ball mip chain. */
    void configureScene(
        const eastl::vector<WebglCustomAttributesPoints3Vertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &textureMips)
    {
        pointTexture = device->createTexture(
            "WebglCustomAttributesPoints3BallTexture",
            WebglCustomAttributesPoints3TextureExtent,
            WebglCustomAttributesPoints3TextureExtent,
            1u,
            uint(textureMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglCustomAttributesPoints3VertexCount) *
                    sizeof(WebglCustomAttributesPoints3Vertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglCustomAttributesPoints3IndexCount) * sizeof(uint));
        for (uint mipLevel = 0u; mipLevel < uint(textureMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                pointTexture,
                textureMips[mipLevel].data(),
                uint64_t(textureMips[mipLevel].size()),
                mipLevel);
        }
        graphicsQueue->submit();

        bindGroup = device->createBindGroup<WebglCustomAttributesPoints3BindGroup>(
            uniformBuffer,
            sizeBuffer,
            pointTexture->createView(),
            pointSampler);
        mainPass = device->createRenderClass<WebglCustomAttributesPoints3MainPass>(
            bindGroup);
    }

    /** Uploads one sequential size wave and current Three transforms. */
    void updateFrame(
        const eastl::vector<float> &sizes,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix,
        bool shouldDraw)
    {
        WebglCustomAttributesPoints3Uniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = modelViewMatrix;
        graphicsQueue
            ->writeBuffer(
                BufferRange(sizeBuffer),
                sizes.data(),
                uint64_t(WebglCustomAttributesPoints3LogicalPointCount) *
                    sizeof(float))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        drawCurrentFrame = shouldDraw;
    }

    /** Draws one ordinary indexed Scene object only at the requested capture frame. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (drawCurrentFrame)
        {
            WebglCustomAttributesPoints3FrameBuffer frameBuffer;
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
                    "WebglCustomAttributesPoints3Scene",
                    frameBuffer,
                    mainPass->setVertexBuffer(vertexBuffer),
                    mainPass->setIndexBuffer(indexBuffer),
                    mainPass(
                        WebglCustomAttributesPoints3IndexCount,
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

    /** Returns the direct DSL-created RGBA8 texture used for readback. */
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
        return WebglCustomAttributesPoints3OutputWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return WebglCustomAttributesPoints3OutputHeight;
    }

    /** Releases all ordinary buffers, texture mips, and direct output resources. */
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
