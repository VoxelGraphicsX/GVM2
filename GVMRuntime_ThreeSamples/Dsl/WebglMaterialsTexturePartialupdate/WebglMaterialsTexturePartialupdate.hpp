#ifndef GVM_THREE_WEBGL_MATERIALS_TEXTURE_PARTIALUPDATE_HPP
#define GVM_THREE_WEBGL_MATERIALS_TEXTURE_PARTIALUPDATE_HPP

#include "UGL.h"
#include "WebglMaterialsTexturePartialupdateData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglMaterialsTexturePartialupdateWidth = 512u;
static const uint WebglMaterialsTexturePartialupdateHeight = 512u;
static const uint WebglMaterialsTexturePartialupdatePatchExtent = 32u;
static const uint WebglMaterialsTexturePartialupdateMaximumPatchCount = 9u;
static const uint WebglMaterialsTexturePartialupdateOutputWidth = 800u;
static const uint WebglMaterialsTexturePartialupdateOutputHeight = 500u;

/** Stores one coverage-sample transform for the ordinary PlaneGeometry Scene pass. */
struct WebglMaterialsTexturePartialupdateSceneUniforms
{
    float4x4 modelViewProjection;
    float4 samplePositionAndReserved;
};

/** Stores the selected deterministic patch count for the compute update pass. */
struct WebglMaterialsTexturePartialupdateComputeUniforms
{
    uint4 patchCountAndReserved;
};

/** Binds the immutable sRGB checkerboard and writable linear working texture. */
struct WebglMaterialsTexturePartialupdateBaseCopyBindGroup final : public IBindGroup
{
    /** Declares the sampled source and existing RGBA16Float storage destination. */
    constructor(Texture2D<float4> baseTexture [[Binding0]],
                RWTexture2D<TextureFormat::RGBA16Float> workingTexture [[Binding1]])
    {
    }
};

/** Binds all authored patch records and the writable linear working texture. */
struct WebglMaterialsTexturePartialupdatePatchBindGroup final : public IBindGroup
{
    /** Declares the sRGB patch bank, deterministic destinations, and patch-count state. */
    constructor(Texture2D<float4> patchTexture [[Binding0]],
                StructuredBuffer<WebglMaterialsTexturePartialupdatePatch> patches [[Binding1]],
                UniformBuffer<WebglMaterialsTexturePartialupdateComputeUniforms> uniforms [[Binding2]],
                RWTexture2D<TextureFormat::RGBA16Float> workingTexture [[Binding3]])
    {
    }
};

/** Binds the updated texture, linear sampler, and one coverage-sample transform. */
struct WebglMaterialsTexturePartialupdateSceneBindGroup final : public IBindGroup
{
    /** Declares the resources consumed by the private MeshBasicMaterial equivalent. */
    constructor(UniformBuffer<WebglMaterialsTexturePartialupdateSceneUniforms> uniforms [[Binding0]],
                Texture2D<float4> workingTexture [[Binding1]],
                Sampler textureSampler [[Binding2]])
    {
    }
};

/** Carries the transformed plane position and perspective-correct UV. */
struct WebglMaterialsTexturePartialupdateVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the deterministic single-sample color target. */
struct WebglMaterialsTexturePartialupdateFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel with Three r185's output-transfer constants. */
inline float webglMaterialsTexturePartialupdateLinearToSrgb(float value)
{
    const float clampedValue = clamp(value, 0.0f, 1.0f);
    if (clampedValue <= 0.0031308f)
    {
        return clampedValue * 12.92f;
    }
    return pow(clampedValue, 0.41666f) * 1.055f - 0.055f;
}

/** Copies the immutable sRGB base image into a storage-capable linear texture. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglMaterialsTexturePartialupdateBaseCopyPass final : public IComputeClass
{
public:
    /** Binds the immutable source and writable working texture. */
    constructor(BindGroup<WebglMaterialsTexturePartialupdateBaseCopyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Copies one hardware-decoded sRGB texel into the linear working texture. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebglMaterialsTexturePartialupdateWidth ||
            threadID.y >= WebglMaterialsTexturePartialupdateHeight)
        {
            return;
        }
        const float4 linearColor = bindGroup->baseTexture->read(threadID.xy, 0u);
        bindGroup->workingTexture->write(threadID.xy, half4(linearColor));
    }
};

/** Applies every selected 32x32 color patch through the frozen compute/storage surface. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglMaterialsTexturePartialupdatePatchPass final : public IComputeClass
{
public:
    /** Binds the immutable patch bank, destination records, state, and writable texture. */
    constructor(BindGroup<WebglMaterialsTexturePartialupdatePatchBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes one hardware-decoded patch texel to its deterministic destination coordinate. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint patchCount = bindGroup->uniforms->patchCountAndReserved.x;
        if (threadID.x >= WebglMaterialsTexturePartialupdatePatchExtent ||
            threadID.y >= WebglMaterialsTexturePartialupdatePatchExtent ||
            threadID.z >= patchCount)
        {
            return;
        }
        const WebglMaterialsTexturePartialupdatePatch patch =
            bindGroup->patches[threadID.z];
        const uint2 sourceCoordinate = uint2(
            threadID.x,
            patch.destinationAndSourceRow.z + threadID.y);
        const uint2 destinationCoordinate = uint2(
            patch.destinationAndSourceRow.x + threadID.x,
            patch.destinationAndSourceRow.y + threadID.y);
        const float4 linearColor = bindGroup->patchTexture->read(sourceCoordinate, 0u);
        bindGroup->workingTexture->write(destinationCoordinate, half4(linearColor));
    }
};

/** Draws the example's only Mesh through one ordinary indexed RenderClass. */
class WebglMaterialsTexturePartialupdateMainPass final : public IRenderClass
{
public:
    /** Preserves the opaque triangle-list MeshBasicMaterial raster state. */
    constructor(BindGroup<WebglMaterialsTexturePartialupdateSceneBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Applies the fixed camera transform and deterministic coverage-sample offset. */
    WebglMaterialsTexturePartialupdateVertexOutput vertex(
        WebglMaterialsTexturePartialupdateVertex inputValue [[VertexInput0]])
    {
        float4 clipPosition = mul(
            bindGroup->uniforms->modelViewProjection,
            float4(inputValue.position, 1.0f));
        const float2 samplePosition = bindGroup->uniforms->samplePositionAndReserved.xy;
        clipPosition.x +=
            (1.0f - 2.0f * samplePosition.x) /
            float(WebglMaterialsTexturePartialupdateOutputWidth) * clipPosition.w;
        clipPosition.y +=
            (2.0f * samplePosition.y - 1.0f) /
            float(WebglMaterialsTexturePartialupdateOutputHeight) * clipPosition.w;

        WebglMaterialsTexturePartialupdateVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.texCoord = inputValue.texCoord;
        return outputValue;
    }

    /** Samples the updated linear texture and emits Three's default sRGB output. */
    WebglMaterialsTexturePartialupdateFrameBuffer fragment(
        WebglMaterialsTexturePartialupdateVertexOutput inputValue)
    {
        const float2 sampleCoordinate = inputValue.texCoord;
        const float4 linearColor = bindGroup->workingTexture->sample(
            bindGroup->textureSampler,
            sampleCoordinate);
        WebglMaterialsTexturePartialupdateFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMaterialsTexturePartialupdateLinearToSrgb(linearColor.x)),
            half(webglMaterialsTexturePartialupdateLinearToSrgb(linearColor.y)),
            half(webglMaterialsTexturePartialupdateLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the ordinary Scene geometry, compute-update chain, and deterministic output. */
class WebglMaterialsTexturePartialupdateRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsTexturePartialupdateVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsTexturePartialupdatePatch, BufferUsage<Storage, CopyDst>> patchBuffer;
    Buffer<WebglMaterialsTexturePartialupdateComputeUniforms, BufferUsage<Uniform, CopyDst>> computeUniformBuffer;
    Buffer<WebglMaterialsTexturePartialupdateSceneUniforms, BufferUsage<Uniform, CopyDst>> sceneUniformBuffer0;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        baseTexture;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        patchTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D>
        workingTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Sampler textureSampler;
    BindGroup<WebglMaterialsTexturePartialupdateBaseCopyBindGroup> baseCopyBindGroup;
    BindGroup<WebglMaterialsTexturePartialupdatePatchBindGroup> patchBindGroup;
    BindGroup<WebglMaterialsTexturePartialupdateSceneBindGroup> sceneBindGroup0;
    ComputeClass<WebglMaterialsTexturePartialupdateBaseCopyPass> baseCopyPass;
    ComputeClass<WebglMaterialsTexturePartialupdatePatchPass> patchPass;
    RenderClass<WebglMaterialsTexturePartialupdateMainPass> mainPass0;
    uint readbackWidth = WebglMaterialsTexturePartialupdateOutputWidth;
    uint readbackHeight = WebglMaterialsTexturePartialupdateOutputHeight;

public:
    /** Creates all fixed buffers and the existing linear clamp sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer("WebglMaterialsTexturePartialupdateVertices", 4u);
        indexBuffer = device->createBuffer("WebglMaterialsTexturePartialupdateIndices", 6u);
        patchBuffer = device->createBuffer(
            "WebglMaterialsTexturePartialupdatePatches",
            WebglMaterialsTexturePartialupdateMaximumPatchCount);
        computeUniformBuffer = device->createBuffer(
            "WebglMaterialsTexturePartialupdateComputeUniforms", 1u);
        sceneUniformBuffer0 = device->createBuffer(
            "WebglMaterialsTexturePartialupdateSceneUniforms0", 1u);
        textureSampler = device->createSampler({
            .label = "WebglMaterialsTexturePartialupdateLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the final deterministic single-sample RGBA8 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglMaterialsTexturePartialupdateOutput", width, height, 1u);
    }

    /** Uploads the indexed plane, immutable images, patch records, and fixed camera state. */
    void configureScene(
        const eastl::vector<WebglMaterialsTexturePartialupdateVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<WebglMaterialsTexturePartialupdatePatch> &patches,
        const eastl::vector<uint8_t> &baseTextureBytes,
        const eastl::vector<uint8_t> &patchTextureBytes,
        float4x4 modelViewProjection,
        uint patchCount)
    {
        baseTexture = device->createTexture(
            "WebglMaterialsTexturePartialupdateBaseSrgb",
            WebglMaterialsTexturePartialupdateWidth,
            WebglMaterialsTexturePartialupdateHeight,
            1u);
        patchTexture = device->createTexture(
            "WebglMaterialsTexturePartialupdatePatchSrgb",
            WebglMaterialsTexturePartialupdatePatchExtent,
            WebglMaterialsTexturePartialupdatePatchExtent *
                WebglMaterialsTexturePartialupdateMaximumPatchCount,
            1u);
        workingTexture = device->createTexture(
            "WebglMaterialsTexturePartialupdateWorkingLinear",
            WebglMaterialsTexturePartialupdateWidth,
            WebglMaterialsTexturePartialupdateHeight,
            1u);

        WebglMaterialsTexturePartialupdateComputeUniforms computeUniforms;
        computeUniforms.patchCountAndReserved = uint4(patchCount, 0u, 0u, 0u);
        WebglMaterialsTexturePartialupdateSceneUniforms sceneUniforms0;
        sceneUniforms0.modelViewProjection = modelViewProjection;
        sceneUniforms0.samplePositionAndReserved = float4(0.5f, 0.5f, 0.0f, 0.0f);

        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(WebglMaterialsTexturePartialupdateVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(patchBuffer),
                patches.data(),
                uint64_t(patches.size()) * sizeof(WebglMaterialsTexturePartialupdatePatch))
            ->writeBuffer(
                BufferRange(computeUniformBuffer),
                &computeUniforms,
                sizeof(computeUniforms))
            ->writeBuffer(
                BufferRange(sceneUniformBuffer0),
                &sceneUniforms0,
                sizeof(sceneUniforms0))
            ->writeTexture(
                baseTexture,
                baseTextureBytes.data(),
                uint64_t(baseTextureBytes.size()))
            ->writeTexture(
                patchTexture,
                patchTextureBytes.data(),
                uint64_t(patchTextureBytes.size()))
            ->submit();

        baseCopyBindGroup =
            device->createBindGroup<WebglMaterialsTexturePartialupdateBaseCopyBindGroup>(
                baseTexture->createView(),
                workingTexture->createView());
        patchBindGroup =
            device->createBindGroup<WebglMaterialsTexturePartialupdatePatchBindGroup>(
                patchTexture->createView(),
                patchBuffer,
                computeUniformBuffer,
                workingTexture->createView());
        sceneBindGroup0 =
            device->createBindGroup<WebglMaterialsTexturePartialupdateSceneBindGroup>(
                sceneUniformBuffer0,
                workingTexture->createView(),
                textureSampler);
        baseCopyPass =
            device->createComputeClass<WebglMaterialsTexturePartialupdateBaseCopyPass>(
                baseCopyBindGroup);
        patchPass =
            device->createComputeClass<WebglMaterialsTexturePartialupdatePatchPass>(
                patchBindGroup);
        mainPass0 = device->createRenderClass<WebglMaterialsTexturePartialupdateMainPass>(
            sceneBindGroup0);
    }

    /** Rebuilds the linear texture, applies patches, and renders one single sample. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglMaterialsTexturePartialupdateFrameBuffer frameBuffer0;
        frameBuffer0.color = outputTexture->createView();
        frameBuffer0.color.loadOp = LoadOp::Clear;
        frameBuffer0.color.storeOp = StoreOp::Store;
        frameBuffer0.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        graphicsQueue
            ->computePass(
                "partial-texture-base-copy",
                baseCopyPass(
                    WebglMaterialsTexturePartialupdateWidth,
                    WebglMaterialsTexturePartialupdateHeight,
                    1u))
            ->submit();
        graphicsQueue
            ->computePass(
                "partial-texture-update-compute",
                patchPass(
                    WebglMaterialsTexturePartialupdatePatchExtent,
                    WebglMaterialsTexturePartialupdatePatchExtent,
                    WebglMaterialsTexturePartialupdateMaximumPatchCount))
            ->submit();
        graphicsQueue
            ->renderPass(
                "main-textured-single-sample",
                frameBuffer0,
                mainPass0->setVertexBuffer(vertexBuffer),
                mainPass0->setIndexBuffer(indexBuffer),
                mainPass0(6u, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for deterministic readback. */
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

    /** Releases every standalone buffer and texture owned by this sample. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(patchBuffer);
        device->freeBuffer(computeUniformBuffer);
        device->freeBuffer(sceneUniformBuffer0);
        device->freeTexture(baseTexture);
        device->freeTexture(patchTexture);
        device->freeTexture(workingTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
