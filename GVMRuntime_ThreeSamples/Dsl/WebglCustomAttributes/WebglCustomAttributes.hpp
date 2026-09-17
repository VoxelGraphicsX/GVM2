#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_HPP

#include "UGL.h"
#include "WebglCustomAttributesVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglCustomAttributesVertexCount = 8385u;
static const uint WebglCustomAttributesIndexCount = 48384u;
static const uint WebglCustomAttributesTextureExtent = 512u;
static const uint WebglCustomAttributesTextureMipCount = 10u;

/** Stores the separate Three projection/model-view matrices and private ShaderMaterial uniforms. */
struct WebglCustomAttributesUniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
    float4 amplitudeAndReserved;
    float4 colorAndReserved;
};

/** Binds the frame state, raw-color water texture, and trilinear Repeat sampler. */
struct WebglCustomAttributesBindGroup final : public IBindGroup
{
    /** Declares every resource read by the private ShaderMaterial equivalent. */
    constructor(UniformBuffer<WebglCustomAttributesUniforms> uniforms [[Binding0]],
                Texture2D<float4> colorTexture [[Binding1]],
                Sampler colorSampler [[Binding2]])
    {
    }
};

/** Carries the displaced clip position, untransformed source normal, and authored UV. */
struct WebglCustomAttributesVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    float2 texCoord [[Attribute1]];
};

/** Defines the direct RGBA8 plus native-depth target for the only Scene pass. */
struct WebglCustomAttributesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the example's single indexed Sphere Mesh through an ordinary RenderClass. */
class WebglCustomAttributesMainPass final : public IRenderClass
{
public:
    /** Preserves ShaderMaterial's visible front faces after the explicit clip-y reflection. */
    constructor(BindGroup<WebglCustomAttributesBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the original displacement and UV equations before separate matrix multiplies. */
    WebglCustomAttributesVertexOutput vertex(
        WebglCustomAttributesVertex inputValue [[VertexInput0]])
    {
        const float amplitude = bindGroup->uniforms->amplitudeAndReserved.x;
        const float3 displacedPosition =
            inputValue.position + amplitude * inputValue.normal * inputValue.displacement;
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelViewMatrix,
            float4(displacedPosition, 1.0f));

        WebglCustomAttributesVertexOutput outputValue;
        float4 clipPosition = mul(
            bindGroup->uniforms->projectionMatrix,
            viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = clipPosition;
        outputValue.normal = inputValue.normal;
        outputValue.texCoord =
            (0.5f + amplitude) * inputValue.texCoord + float2(amplitude);
        return outputValue;
    }

    /** Reproduces the custom shader's raw-texture grayscale and direct output assignment. */
    WebglCustomAttributesFrameBuffer fragment(
        WebglCustomAttributesVertexOutput inputValue)
    {
        const float3 light = normalize(float3(0.5f, 0.2f, 1.0f));
        const float lightProduct = dot(inputValue.normal, light) * 0.5f + 0.5f;
        const float4 textureColor = bindGroup->colorTexture->sample(
            bindGroup->colorSampler,
            inputValue.texCoord);
        const float grayscale =
            textureColor.x * 0.3f +
            textureColor.y * 0.59f +
            textureColor.z * 0.11f;
        const float3 linearColor =
            grayscale * lightProduct * bindGroup->uniforms->colorAndReserved.xyz;

        WebglCustomAttributesFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns all DSL-created resources and GPU commands for webgl_custom_attributes. */
class WebglCustomAttributesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglCustomAttributesVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglCustomAttributesUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        colorTexture;
    Sampler colorSampler;
    BindGroup<WebglCustomAttributesBindGroup> bindGroup;
    RenderClass<WebglCustomAttributesMainPass> mainPass;
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
    /** Creates fixed buffers and the existing trilinear Repeat sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglCustomAttributesVertices",
            WebglCustomAttributesVertexCount);
        indexBuffer = device->createBuffer(
            "WebglCustomAttributesIndices",
            WebglCustomAttributesIndexCount);
        uniformBuffer = device->createBuffer(
            "WebglCustomAttributesUniforms",
            1u);
        colorSampler = device->createSampler({
            .label = "WebglCustomAttributesWaterSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 9.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the fixed direct RGBA8 output and native-depth attachment. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglCustomAttributesOutputRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglCustomAttributesDepth32",
            width,
            height,
            1u);
    }

    /** Uploads immutable indices, initial vertices, and the complete CPU-authored mip chain. */
    void configureScene(
        const eastl::vector<WebglCustomAttributesVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &textureMips)
    {
        colorTexture = device->createTexture(
            "WebglCustomAttributesWaterTexture",
            WebglCustomAttributesTextureExtent,
            WebglCustomAttributesTextureExtent,
            1u,
            uint(textureMips.size()));
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglCustomAttributesVertexCount) *
                    sizeof(WebglCustomAttributesVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglCustomAttributesIndexCount) * sizeof(uint));
        for (uint mipLevel = 0u; mipLevel < uint(textureMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                colorTexture,
                textureMips[mipLevel].data(),
                uint64_t(textureMips[mipLevel].size()),
                mipLevel);
        }
        graphicsQueue->submit();

        bindGroup = device->createBindGroup<WebglCustomAttributesBindGroup>(
            uniformBuffer,
            colorTexture->createView(),
            colorSampler);
        mainPass = device->createRenderClass<WebglCustomAttributesMainPass>(bindGroup);
    }

    /** Uploads one sequentially advanced frame's dynamic attribute and uniform state. */
    void updateFrame(
        const eastl::vector<WebglCustomAttributesVertex> &vertices,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix,
        float amplitude,
        float3 color)
    {
        WebglCustomAttributesUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = modelViewMatrix;
        uniforms.amplitudeAndReserved = float4(amplitude, 0.0f, 0.0f, 0.0f);
        uniforms.colorAndReserved = float4(color, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglCustomAttributesVertexCount) *
                    sizeof(WebglCustomAttributesVertex))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
    }

    /** Draws exactly one explicit indexed Scene object and presents its DSL output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglCustomAttributesFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            5.0 / 255.0,
            5.0 / 255.0,
            5.0 / 255.0,
            1.0,
        };
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglCustomAttributesScene",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(
                    WebglCustomAttributesIndexCount,
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

    /** Returns the DSL-created RGBA8 texture used for deterministic readback. */
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

    /** Releases all standalone geometry, uniform, texture, color, and depth resources. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(colorTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
