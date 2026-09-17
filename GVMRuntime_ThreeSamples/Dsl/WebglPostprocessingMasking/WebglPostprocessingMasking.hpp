#ifndef GVM_THREE_WEBGL_POSTPROCESSING_MASKING_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_MASKING_HPP

#include "UGL.h"
#include "WebglPostprocessingMaskingData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglPostprocessingMaskingTorusVertexCount = 561u;
static const uint WebglPostprocessingMaskingTorusIndexCount = 3072u;

/** Stores the box model-view-projection transform. */
struct WebglPostprocessingMaskingObjectData
{
    float4x4 modelViewProjection;
};

/** Stores the mandatory identity instance component. */
struct WebglPostprocessingMaskingInstanceData
{
    float4 reserved;
};

/** Stores the opaque mask material phase. */
struct WebglPostprocessingMaskingMaterialData
{
    float4 maskAndPhase;
};

/** Defines the unique grouped BoxGeometry Scene RenderSet. */
struct WebglPostprocessingMaskingScene1RenderSet : public IRenderSet
{
    /** Declares the packed grouped box and mandatory Scene components. */
    constructor(
        BufferComponent<WebglPostprocessingMaskingVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingMaskingObjectData> objects,
        BufferComponent<WebglPostprocessingMaskingInstanceData> instances,
        BufferComponent<WebglPostprocessingMaskingMaterialData> materials)
    {
    }
};

/** Binds the standalone simple torus transform. */
struct WebglPostprocessingMaskingTorusResources final : public IBindGroup
{
    /** Declares the immutable target-frame torus uniform. */
    constructor(
        UniformBuffer<WebglPostprocessingMaskingTorusUniforms>
            uniforms [[Binding0]])
    {
    }
};

/** Binds both masks, both sRGB photographs, and their samplers. */
struct WebglPostprocessingMaskingCompositeResources final : public IBindGroup
{
    /** Declares all resources used by the exact ordered mask composition. */
    constructor(
        Texture2D<half> boxMask [[Binding0]],
        Texture2D<half> torusMask [[Binding1]],
        Texture2D<float4> photograph1 [[Binding2]],
        Texture2D<float4> photograph2 [[Binding3]],
        Sampler maskSampler [[Binding4]],
        Sampler photograph1Sampler [[Binding5]],
        Sampler photograph2Sampler [[Binding6]])
    {
    }
};

/** Carries a projected mask position. */
struct WebglPostprocessingMaskingVertexOutput
{
    float4 position [[Position]];
};

/** Carries fullscreen composition coordinates. */
struct WebglPostprocessingMaskingScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines one binary R8 mask and depth attachment. */
struct WebglPostprocessingMaskingMaskFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::R8Unorm> mask;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebglPostprocessingMaskingOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear photograph channel to r185 display sRGB. */
float webglPostprocessingMaskingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Converts WebGL clip coordinates to the generated backend convention. */
float4 webglPostprocessingMaskingBackendClip(float4 clipPosition)
{
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    return clipPosition;
}

/** Rasterizes the grouped box Scene through its unique RenderSet. */
class WebglPostprocessingMaskingScene1MaskPass final : public IRenderClass
{
public:
    /** Binds exactly the Scene1 Set and ordinary mask depth state. */
    constructor(
        RenderSet<WebglPostprocessingMaskingScene1RenderSet>
            sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance data through RenderSet builtins. */
    WebglPostprocessingMaskingVertexOutput vertex(
        WebglPostprocessingMaskingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingMaskingObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingMaskingInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglPostprocessingMaskingVertexOutput outputValue;
        outputValue.position = webglPostprocessingMaskingBackendClip(
            mul(
                objectData.modelViewProjection,
                inputValue.position +
                    float4(instanceData.reserved.xyz, 0.0f)));
        return outputValue;
    }

    /** Writes the opaque box alpha used by MaskPass. */
    WebglPostprocessingMaskingMaskFrameBuffer fragment(
        WebglPostprocessingMaskingVertexOutput inputValue)
    {
        (void)inputValue;
        WebglPostprocessingMaskingMaskFrameBuffer frameBuffer;
        frameBuffer.mask = half(1.0f);
        return frameBuffer;
    }
};

/** Rasterizes the independent simple torus through an ordinary RenderClass. */
class WebglPostprocessingMaskingScene2MaskPass final : public IRenderClass
{
public:
    /** Binds only the standalone torus transform. */
    constructor(
        BindGroup<WebglPostprocessingMaskingTorusResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one ordinary TorusGeometry vertex. */
    WebglPostprocessingMaskingVertexOutput vertex(
        WebglPostprocessingMaskingVertex inputValue [[VertexInput0]])
    {
        WebglPostprocessingMaskingVertexOutput outputValue;
        outputValue.position = webglPostprocessingMaskingBackendClip(
            mul(resources->uniforms->modelViewProjection, inputValue.position));
        return outputValue;
    }

    /** Writes the opaque torus alpha used by the second MaskPass. */
    WebglPostprocessingMaskingMaskFrameBuffer fragment(
        WebglPostprocessingMaskingVertexOutput inputValue)
    {
        (void)inputValue;
        WebglPostprocessingMaskingMaskFrameBuffer frameBuffer;
        frameBuffer.mask = half(1.0f);
        return frameBuffer;
    }
};

/** Composes background, Caravaggio box, and panorama torus in pass order. */
class WebglPostprocessingMaskingCompositePass final : public IRenderClass
{
public:
    /** Binds the two binary masks and explicit photo sampling state. */
    constructor(
        BindGroup<WebglPostprocessingMaskingCompositeResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen composition triangle. */
    WebglPostprocessingMaskingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglPostprocessingMaskingScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Applies both binary selections then performs output color conversion. */
    WebglPostprocessingMaskingOutputFrameBuffer fragment(
        WebglPostprocessingMaskingScreenOutput inputValue)
    {
        const float boxMask = float(resources->boxMask->sampleLevel(
            resources->maskSampler, inputValue.uv, 0.0f).x);
        const float torusMask = float(resources->torusMask->sampleLevel(
            resources->maskSampler, inputValue.uv, 0.0f).x);
        const float3 photo1 = float3(resources->photograph1->sampleLevel(
            resources->photograph1Sampler, inputValue.uv, 0.0f).xyz);
        const float3 photo2 = float3(resources->photograph2->sample(
            resources->photograph2Sampler, inputValue.uv).xyz);
        float3 linearColor = float3(0.87843137f);
        linearColor = lerp(linearColor, photo1, step(0.5f, boxMask));
        linearColor = lerp(linearColor, photo2, step(0.5f, torusMask));
        WebglPostprocessingMaskingOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingMaskingLinearToSrgb(linearColor.x)),
            half(webglPostprocessingMaskingLinearToSrgb(linearColor.y)),
            half(webglPostprocessingMaskingLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the Set mask, simple torus mask, photos, and final composition. */
class WebglPostprocessingMaskingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingMaskingScene1RenderSet> scene1Set;
    Buffer<WebglPostprocessingMaskingVertex,
           BufferUsage<Vertex, CopyDst>> torusVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> torusIndexBuffer;
    Buffer<WebglPostprocessingMaskingTorusUniforms,
           BufferUsage<Uniform, CopyDst>> torusUniformBuffer;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> photograph1;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> photograph2;
    Texture<TextureFormat::R8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> boxMask;
    Texture<TextureFormat::R8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> torusMask;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> boxDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> torusDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler maskSampler;
    Sampler photograph1Sampler;
    Sampler photograph2Sampler;
    BindGroup<WebglPostprocessingMaskingTorusResources> torusResources;
    BindGroup<WebglPostprocessingMaskingCompositeResources> compositeResources;
    RenderClass<WebglPostprocessingMaskingScene1MaskPass> boxPass;
    RenderClass<WebglPostprocessingMaskingScene2MaskPass> torusPass;
    RenderClass<WebglPostprocessingMaskingCompositePass> compositePass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both geometry paths and their immutable samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        scene1Set = device->createRenderSet<
            WebglPostprocessingMaskingScene1RenderSet>();
        torusVertexBuffer = device->createBuffer(
            "WebglPostprocessingMaskingTorusVertices",
            WebglPostprocessingMaskingTorusVertexCount);
        torusIndexBuffer = device->createBuffer(
            "WebglPostprocessingMaskingTorusIndices",
            WebglPostprocessingMaskingTorusIndexCount);
        torusUniformBuffer = device->createBuffer(
            "WebglPostprocessingMaskingTorusUniforms", 1u);
        maskSampler = device->createSampler({
            .label = "WebglPostprocessingMaskingMaskSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        photograph1Sampler = device->createSampler({
            .label = "WebglPostprocessingMaskingPhoto1Sampler",
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
        photograph2Sampler = device->createSampler({
            .label = "WebglPostprocessingMaskingPhoto2Sampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1u,
        });
        boxPass = device->createRenderClass<
            WebglPostprocessingMaskingScene1MaskPass>(scene1Set);
    }

    /** Allocates ordinary single-sample masks, depth, and output. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        boxMask = device->createTexture(
            "WebglPostprocessingMaskingBoxR8", width, height, 1u);
        torusMask = device->createTexture(
            "WebglPostprocessingMaskingTorusR8", width, height, 1u);
        boxDepth = device->createTexture(
            "WebglPostprocessingMaskingBoxDepth", width, height, 1u);
        torusDepth = device->createTexture(
            "WebglPostprocessingMaskingTorusDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingMaskingOutput", width, height, 1u);
    }

    /** Uploads standalone torus data and both explicit photograph mip chains. */
    void configureScene(
        const eastl::vector<WebglPostprocessingMaskingVertex> &torusVertices,
        const eastl::vector<uint> &torusIndices,
        const WebglPostprocessingMaskingTorusUniforms &torusUniforms,
        const eastl::vector<eastl::vector<uint8_t>> &photograph1Mips,
        const eastl::vector<eastl::vector<uint8_t>> &photograph2Mips)
    {
        photograph1 = device->createTexture(
            "WebglPostprocessingMaskingCaravaggio",
            758u, 600u, 1u, uint(photograph1Mips.size()));
        photograph2 = device->createTexture(
            "WebglPostprocessingMaskingPanorama",
            4096u, 2048u, 1u, uint(photograph2Mips.size()));
        for (uint mip = 0u; mip < uint(photograph1Mips.size()); ++mip)
            graphicsQueue->writeTexture(
                photograph1,
                photograph1Mips[mip].data(),
                uint64_t(photograph1Mips[mip].size()),
                mip);
        for (uint mip = 0u; mip < uint(photograph2Mips.size()); ++mip)
            graphicsQueue->writeTexture(
                photograph2,
                photograph2Mips[mip].data(),
                uint64_t(photograph2Mips[mip].size()),
                mip);
        graphicsQueue
            ->writeBuffer(
                BufferRange(torusVertexBuffer),
                torusVertices.data(),
                uint64_t(torusVertices.size()) *
                    sizeof(WebglPostprocessingMaskingVertex))
            ->writeBuffer(
                BufferRange(torusIndexBuffer),
                torusIndices.data(),
                uint64_t(torusIndices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(torusUniformBuffer),
                &torusUniforms,
                sizeof(torusUniforms))
            ->submit();
        torusResources = device->createBindGroup<
            WebglPostprocessingMaskingTorusResources>(torusUniformBuffer);
        torusPass = device->createRenderClass<
            WebglPostprocessingMaskingScene2MaskPass>(torusResources);
        compositeResources = device->createBindGroup<
            WebglPostprocessingMaskingCompositeResources>(
                boxMask->createView(),
                torusMask->createView(),
                photograph1->createView(),
                photograph2->createView(),
                maskSampler,
                photograph1Sampler,
                photograph2Sampler);
        compositePass = device->createRenderClass<
            WebglPostprocessingMaskingCompositePass>(compositeResources);
    }

    /** Renders both masks, composes in pass order, and presents. */
    void render() override
    {
        scene1Set->update();
        WebglPostprocessingMaskingMaskFrameBuffer boxFrame;
        boxFrame.mask = boxMask->createView();
        boxFrame.mask.loadOp = LoadOp::Clear;
        boxFrame.mask.storeOp = StoreOp::Store;
        boxFrame.mask.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        boxFrame.depth = boxDepth->createView();
        boxFrame.depth.depthLoadOp = LoadOp::Clear;
        boxFrame.depth.depthStoreOp = StoreOp::Store;
        boxFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingMaskingMaskFrameBuffer torusFrame;
        torusFrame.mask = torusMask->createView();
        torusFrame.mask.loadOp = LoadOp::Clear;
        torusFrame.mask.storeOp = StoreOp::Store;
        torusFrame.mask.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        torusFrame.depth = torusDepth->createView();
        torusFrame.depth.depthLoadOp = LoadOp::Clear;
        torusFrame.depth.depthStoreOp = StoreOp::Store;
        torusFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingMaskingOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglPostprocessingMaskingBox", boxFrame, boxPass())
            ->renderPass(
                "WebglPostprocessingMaskingTorus",
                torusFrame,
                torusPass->setVertexBuffer(torusVertexBuffer),
                torusPass->setIndexBuffer(torusIndexBuffer),
                torusPass(
                    WebglPostprocessingMaskingTorusIndexCount,
                    1u, 0u, 0, 0u))
            ->renderPass(
                "WebglPostprocessingMaskingComposite",
                outputFrame,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Set, standalone buffers, photographs, and attachments. */
    void destroy() override
    {
        scene1Set->destroy();
        device->freeBuffer(torusVertexBuffer);
        device->freeBuffer(torusIndexBuffer);
        device->freeBuffer(torusUniformBuffer);
        device->freeTexture(photograph1);
        device->freeTexture(photograph2);
        device->freeTexture(boxMask);
        device->freeTexture(torusMask);
        device->freeTexture(boxDepth);
        device->freeTexture(torusDepth);
        device->freeTexture(outputTexture);
    }
};

#endif
