#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_INTEGER_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_INTEGER_HPP

#include "UGL.h"
#include "WebglBuffergeometryAttributesIntegerVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryAttributesIntegerVertexCount = 30000u;

/** Stores one fixed Three transform and one deterministic coverage-sample position. */
struct WebglBuffergeometryAttributesIntegerUniforms
{
    float4x4 modelViewProjection;
    float4 samplePositionAndReserved;
};

/** Binds the frame transform, three authored textures, and their common sampler. */
struct WebglBuffergeometryAttributesIntegerBindGroup final : public IBindGroup
{
    /** Declares the existing Texture2D and uniform bindings used by the private material. */
    constructor(UniformBuffer<WebglBuffergeometryAttributesIntegerUniforms> uniforms [[Binding0]],
                Texture2D<float4> crateTexture [[Binding1]],
                Texture2D<float4> floorTexture [[Binding2]],
                Texture2D<float4> grassTexture [[Binding3]],
                Sampler textureSampler [[Binding4]])
    {
    }
};

/** Carries the perspective UV and flat signed texture selector to the fragment stage. */
struct WebglBuffergeometryAttributesIntegerVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
    int textureIndex [[Attribute1]];
};

/** Defines the deterministic single-sample color and depth target. */
struct WebglBuffergeometryAttributesIntegerFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Re-expresses the integer-attribute ShaderMaterial as one ordinary DSL RenderClass. */
class WebglBuffergeometryAttributesIntegerMainPass final : public IRenderClass
{
public:
    /** Preserves Three.DoubleSide, triangle-list, and default opaque depth semantics. */
    constructor(BindGroup<WebglBuffergeometryAttributesIntegerBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the fixed transform and forwards the signed selector without interpolation. */
    WebglBuffergeometryAttributesIntegerVertexOutput vertex(
        WebglBuffergeometryAttributesIntegerVertex inputValue [[VertexInput0]])
    {
        float4 clipPosition = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        const float2 samplePosition =
            bindGroup->uniforms->samplePositionAndReserved.xy;
        clipPosition.x +=
            (1.0f - 2.0f * samplePosition.x) / 800.0f * clipPosition.w;
        clipPosition.y +=
            (2.0f * samplePosition.y - 1.0f) / 500.0f * clipPosition.w;

        WebglBuffergeometryAttributesIntegerVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.texCoord = inputValue.texCoord;
        outputValue.textureIndex = inputValue.textureIndex;
        return outputValue;
    }

    /** Selects the authored Texture2D from the flat signed integer varying. */
    WebglBuffergeometryAttributesIntegerFrameBuffer fragment(
        WebglBuffergeometryAttributesIntegerVertexOutput inputValue)
    {
        float4 color = bindGroup->grassTexture->sample(
            bindGroup->textureSampler,
            inputValue.texCoord);
        if (inputValue.textureIndex == 0)
        {
            color = bindGroup->crateTexture->sample(
                bindGroup->textureSampler,
                inputValue.texCoord);
        }
        else if (inputValue.textureIndex == 1)
        {
            color = bindGroup->floorTexture->sample(
                bindGroup->textureSampler,
                inputValue.texCoord);
        }

        WebglBuffergeometryAttributesIntegerFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns standalone integer geometry, authored textures, and deterministic DSL passes. */
class WebglBuffergeometryAttributesIntegerRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryAttributesIntegerVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglBuffergeometryAttributesIntegerUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        crateTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        floorTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        grassTexture;
    Sampler textureSampler;
    BindGroup<WebglBuffergeometryAttributesIntegerBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometryAttributesIntegerMainPass> mainPass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates fixed buffers and the existing trilinear clamp sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometryAttributesIntegerVertices",
            WebglBuffergeometryAttributesIntegerVertexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglBuffergeometryAttributesIntegerUniforms0", 1u);
        textureSampler = device->createSampler({
            .label = "WebglBuffergeometryAttributesIntegerSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 12,
            .maxAnisotropy = 1,
        });
    }

    /** Allocates one single-sample color target and one depth target. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryAttributesIntegerOutputRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryAttributesIntegerDepth32", width, height, 1u);
    }

    /** Uploads immutable geometry, complete mip chains, and target-frame transforms. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryAttributesIntegerVertex> &vertices,
        float4x4 modelViewProjection,
        uint crateWidth,
        uint crateHeight,
        const eastl::vector<eastl::vector<uint8_t>> &crateMips,
        uint floorWidth,
        uint floorHeight,
        const eastl::vector<eastl::vector<uint8_t>> &floorMips,
        uint grassWidth,
        uint grassHeight,
        const eastl::vector<eastl::vector<uint8_t>> &grassMips)
    {
        crateTexture = device->createTexture(
            "WebglBuffergeometryAttributesIntegerCrate",
            crateWidth,
            crateHeight,
            1u,
            uint(crateMips.size()));
        floorTexture = device->createTexture(
            "WebglBuffergeometryAttributesIntegerFloor",
            floorWidth,
            floorHeight,
            1u,
            uint(floorMips.size()));
        grassTexture = device->createTexture(
            "WebglBuffergeometryAttributesIntegerGrass",
            grassWidth,
            grassHeight,
            1u,
            uint(grassMips.size()));

        WebglBuffergeometryAttributesIntegerUniforms uniforms0;
        uniforms0.modelViewProjection = modelViewProjection;
        uniforms0.samplePositionAndReserved = float4(0.5f, 0.5f, 0.0f, 0.0f);

        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglBuffergeometryAttributesIntegerVertexCount) *
                    sizeof(WebglBuffergeometryAttributesIntegerVertex))
            ->writeBuffer(BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ;
        for (uint mipLevel = 0u; mipLevel < uint(crateMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                crateTexture,
                crateMips[mipLevel].data(),
                uint64_t(crateMips[mipLevel].size()),
                mipLevel);
        }
        for (uint mipLevel = 0u; mipLevel < uint(floorMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                floorTexture,
                floorMips[mipLevel].data(),
                uint64_t(floorMips[mipLevel].size()),
                mipLevel);
        }
        for (uint mipLevel = 0u; mipLevel < uint(grassMips.size()); ++mipLevel)
        {
            graphicsQueue->writeTexture(
                grassTexture,
                grassMips[mipLevel].data(),
                uint64_t(grassMips[mipLevel].size()),
                mipLevel);
        }
        graphicsQueue->submit();

        bindGroup0 =
            device->createBindGroup<WebglBuffergeometryAttributesIntegerBindGroup>(
                uniformBuffer0,
                crateTexture->createView(),
                floorTexture->createView(),
                grassTexture->createView(),
                textureSampler);
        mainPass0 = device->createRenderClass<WebglBuffergeometryAttributesIntegerMainPass>(
            bindGroup0);
    }

    /** Rasterizes the integer-attribute geometry into one single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryAttributesIntegerFrameBuffer frameBuffer0;
        frameBuffer0.color = outputTexture->createView();
        frameBuffer0.color.loadOp = LoadOp::Clear;
        frameBuffer0.color.storeOp = StoreOp::Store;
        frameBuffer0.color.clearValue = {5.0 / 255.0, 5.0 / 255.0, 5.0 / 255.0, 1.0};
        frameBuffer0.depth = depthTexture->createView();
        frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer0.depth.depthStoreOp = StoreOp::Store;
        frameBuffer0.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglBuffergeometryAttributesIntegerMain",
                frameBuffer0,
                mainPass0->setVertexBuffer(vertexBuffer),
                mainPass0(WebglBuffergeometryAttributesIntegerVertexCount, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output used for deterministic readback. */
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
        return readbackWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases every standalone buffer and explicit texture owned by this sample. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(crateTexture);
        device->freeTexture(floorTexture);
        device->freeTexture(grassTexture);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
