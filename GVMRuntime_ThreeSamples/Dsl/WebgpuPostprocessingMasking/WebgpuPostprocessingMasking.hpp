#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_MASKING_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_MASKING_HPP

#include "UGL.h"
#include "WebgpuPostprocessingMaskingData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebgpuPostprocessingMaskingTorusVertexCount = 561u;
static const uint WebgpuPostprocessingMaskingTorusIndexCount = 3072u;

/** Stores the box model-view-projection transform. */
struct WebgpuPostprocessingMaskingObjectData
{
    float4x4 modelViewProjection;
};

/** Stores the mandatory identity instance component. */
struct WebgpuPostprocessingMaskingInstanceData
{
    float4 reserved;
};

/** Stores the opaque mask material phase. */
struct WebgpuPostprocessingMaskingMaterialData
{
    float4 maskAndPhase;
};

/** Defines the unique grouped BoxGeometry Scene RenderSet. */
struct WebgpuPostprocessingMaskingSceneRenderSet : public IRenderSet
{
    /** Declares the packed grouped box and mandatory Scene components. */
    constructor(
        BufferComponent<WebgpuPostprocessingMaskingVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingMaskingObjectData> objects,
        BufferComponent<WebgpuPostprocessingMaskingInstanceData> instances,
        BufferComponent<WebgpuPostprocessingMaskingMaterialData> materials)
    {
    }
};

/** Binds the standalone simple torus transform. */
struct WebgpuPostprocessingMaskingTorusResources final : public IBindGroup
{
    /** Declares the immutable target-frame torus uniform. */
    constructor(
        UniformBuffer<WebgpuPostprocessingMaskingTorusUniforms>
            uniforms [[Binding0]])
    {
    }
};

/** Binds both masks, both sRGB photographs, and their samplers. */
struct WebgpuPostprocessingMaskingCompositeResources final : public IBindGroup
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
struct WebgpuPostprocessingMaskingVertexOutput
{
    float4 position [[Position]];
};

/** Carries fullscreen composition coordinates. */
struct WebgpuPostprocessingMaskingScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines one binary R8 mask and depth attachment. */
struct WebgpuPostprocessingMaskingMaskFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::R8Unorm> mask;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebgpuPostprocessingMaskingOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear photograph channel to r185 display sRGB. */
float webgpuPostprocessingMaskingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Converts WebGPU clip coordinates to the generated backend convention. */
float4 webgpuPostprocessingMaskingBackendClip(float4 clipPosition)
{
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    return clipPosition;
}

/** Rasterizes the grouped box Scene through its unique RenderSet. */
class WebgpuPostprocessingMaskingBoxPass final : public IRenderClass
{
public:
    /** Binds exactly the Scene Set and ordinary mask depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingMaskingSceneRenderSet>
            sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance data through RenderSet builtins. */
    WebgpuPostprocessingMaskingVertexOutput vertex(
        WebgpuPostprocessingMaskingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingMaskingObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingMaskingInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebgpuPostprocessingMaskingVertexOutput outputValue;
        outputValue.position = webgpuPostprocessingMaskingBackendClip(
            mul(
                objectData.modelViewProjection,
                inputValue.position +
                    float4(instanceData.reserved.xyz, 0.0f)));
        return outputValue;
    }

    /** Writes the opaque box alpha used by MaskPass. */
    WebgpuPostprocessingMaskingMaskFrameBuffer fragment(
        WebgpuPostprocessingMaskingVertexOutput inputValue)
    {
        (void)inputValue;
        WebgpuPostprocessingMaskingMaskFrameBuffer frameBuffer;
        frameBuffer.mask = half(1.0f);
        return frameBuffer;
    }
};

/** Rasterizes the independent simple torus through an ordinary RenderClass. */
class WebgpuPostprocessingMaskingTorusPass final : public IRenderClass
{
public:
    /** Binds only the standalone torus transform. */
    constructor(
        BindGroup<WebgpuPostprocessingMaskingTorusResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one ordinary TorusGeometry vertex. */
    WebgpuPostprocessingMaskingVertexOutput vertex(
        WebgpuPostprocessingMaskingVertex inputValue [[VertexInput0]])
    {
        WebgpuPostprocessingMaskingVertexOutput outputValue;
        outputValue.position = webgpuPostprocessingMaskingBackendClip(
            mul(resources->uniforms->modelViewProjection, inputValue.position));
        return outputValue;
    }

    /** Writes the opaque torus alpha used by the second MaskPass. */
    WebgpuPostprocessingMaskingMaskFrameBuffer fragment(
        WebgpuPostprocessingMaskingVertexOutput inputValue)
    {
        (void)inputValue;
        WebgpuPostprocessingMaskingMaskFrameBuffer frameBuffer;
        frameBuffer.mask = half(1.0f);
        return frameBuffer;
    }
};

/** Composes background, Caravaggio box, and panorama torus in pass order. */
class WebgpuPostprocessingMaskingBasePass final : public IRenderClass
{
public:
    /** Binds the two binary masks and explicit photo sampling state. */
    constructor(
        BindGroup<WebgpuPostprocessingMaskingCompositeResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen composition triangle. */
    WebgpuPostprocessingMaskingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuPostprocessingMaskingScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Applies both binary selections, output conversion, and Inspector chrome. */
    WebgpuPostprocessingMaskingOutputFrameBuffer fragment(
        WebgpuPostprocessingMaskingScreenOutput inputValue)
    {
        const float boxMask = float(resources->boxMask->sampleLevel(
            resources->maskSampler, inputValue.uv, 0.0f).x);
        const float torusMask = float(resources->torusMask->sampleLevel(
            resources->maskSampler, inputValue.uv, 0.0f).x);
        const float3 photo1 = float3(resources->photograph1->sampleLevel(
            resources->photograph1Sampler, inputValue.uv, 0.0f).xyz);
        const float3 photo2 = float3(resources->photograph2->sample(
            resources->photograph2Sampler, inputValue.uv).xyz);
        float3 linearColor = float3(0.7454042f);
        linearColor = lerp(linearColor, photo1, step(0.5f, boxMask));
        linearColor = lerp(linearColor, photo2, step(0.5f, torusMask));
        float3 displayColor = float3(
            webgpuPostprocessingMaskingLinearToSrgb(linearColor.x),
            webgpuPostprocessingMaskingLinearToSrgb(linearColor.y),
            webgpuPostprocessingMaskingLinearToSrgb(linearColor.z));

        const float2 pixel = inputValue.uv * float2(800.0f, 500.0f);
        const float2 panelCenter = float2(724.0f, 33.5f);
        const float2 panelHalfExtent = float2(61.0f, 18.5f);
        const float cornerRadius = pixel.x < panelCenter.x ? 12.0f : 6.0f;
        const float2 panelDelta = abs(pixel - panelCenter) -
            (panelHalfExtent - float2(cornerRadius));
        const float panelDistance =
            length(max(panelDelta, float2(0.0f))) +
            min(max(panelDelta.x, panelDelta.y), 0.0f) - cornerRadius;
        if (panelDistance > 0.5f)
        {
            const float2 shadowCenter = float2(724.0f, 37.5f);
            const float2 shadowDelta = abs(pixel - shadowCenter) -
                (panelHalfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) - cornerRadius;
            const float shadowAlpha = 0.13f * exp(
                -max(shadowDistance, 0.0f) *
                 max(shadowDistance, 0.0f) / 72.0f);
            if (shadowAlpha >= 0.004f)
            {
                displayColor = lerp(
                    displayColor, float3(0.0f), shadowAlpha);
            }
        }
        else
        {
            float3 panelColor = float3(30.0f, 30.0f, 36.0f) / 255.0f;
            float panelAlpha = 0.85f;
            if (panelDistance > -1.0f)
            {
                panelColor = float3(46.1f, 46.1f, 55.8f) / 255.0f;
                panelAlpha = 0.899f;
            }
            panelAlpha *= clamp(0.5f - panelDistance, 0.0f, 1.0f);
            displayColor = lerp(displayColor, panelColor, panelAlpha);
        }
        WebgpuPostprocessingMaskingOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the Set mask, simple torus mask, photos, and final composition. */
class WebgpuPostprocessingMaskingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuPostprocessingMaskingSceneRenderSet> sceneSet;
    Buffer<WebgpuPostprocessingMaskingVertex,
           BufferUsage<Vertex, CopyDst>> torusVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> torusIndexBuffer;
    Buffer<WebgpuPostprocessingMaskingTorusUniforms,
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
    BindGroup<WebgpuPostprocessingMaskingTorusResources> torusResources;
    BindGroup<WebgpuPostprocessingMaskingCompositeResources> compositeResources;
    RenderClass<WebgpuPostprocessingMaskingBoxPass> boxPass;
    RenderClass<WebgpuPostprocessingMaskingTorusPass> torusPass;
    RenderClass<WebgpuPostprocessingMaskingBasePass> compositePass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both geometry paths and their immutable samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<
            WebgpuPostprocessingMaskingSceneRenderSet>();
        torusVertexBuffer = device->createBuffer(
            "WebgpuPostprocessingMaskingTorusVertices",
            WebgpuPostprocessingMaskingTorusVertexCount);
        torusIndexBuffer = device->createBuffer(
            "WebgpuPostprocessingMaskingTorusIndices",
            WebgpuPostprocessingMaskingTorusIndexCount);
        torusUniformBuffer = device->createBuffer(
            "WebgpuPostprocessingMaskingTorusUniforms", 1u);
        maskSampler = device->createSampler({
            .label = "WebgpuPostprocessingMaskingMaskSampler",
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
            .label = "WebgpuPostprocessingMaskingPhoto1Sampler",
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
            .label = "WebgpuPostprocessingMaskingPhoto2Sampler",
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
            WebgpuPostprocessingMaskingBoxPass>(sceneSet);
    }

    /** Allocates ordinary single-sample masks, depth, and output. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        boxMask = device->createTexture(
            "WebgpuPostprocessingMaskingBoxR8", width, height, 1u);
        torusMask = device->createTexture(
            "WebgpuPostprocessingMaskingTorusR8", width, height, 1u);
        boxDepth = device->createTexture(
            "WebgpuPostprocessingMaskingBoxDepth", width, height, 1u);
        torusDepth = device->createTexture(
            "WebgpuPostprocessingMaskingTorusDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuPostprocessingMaskingOutput", width, height, 1u);
    }

    /** Uploads standalone torus data and both explicit photograph mip chains. */
    void configureScene(
        const eastl::vector<WebgpuPostprocessingMaskingVertex> &torusVertices,
        const eastl::vector<uint> &torusIndices,
        const WebgpuPostprocessingMaskingTorusUniforms &torusUniforms,
        const eastl::vector<eastl::vector<uint8_t>> &photograph1Mips,
        const eastl::vector<eastl::vector<uint8_t>> &photograph2Mips)
    {
        photograph1 = device->createTexture(
            "WebgpuPostprocessingMaskingCaravaggio",
            758u, 600u, 1u, uint(photograph1Mips.size()));
        photograph2 = device->createTexture(
            "WebgpuPostprocessingMaskingPanorama",
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
                    sizeof(WebgpuPostprocessingMaskingVertex))
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
            WebgpuPostprocessingMaskingTorusResources>(torusUniformBuffer);
        torusPass = device->createRenderClass<
            WebgpuPostprocessingMaskingTorusPass>(torusResources);
        compositeResources = device->createBindGroup<
            WebgpuPostprocessingMaskingCompositeResources>(
                boxMask->createView(),
                torusMask->createView(),
                photograph1->createView(),
                photograph2->createView(),
                maskSampler,
                photograph1Sampler,
                photograph2Sampler);
        compositePass = device->createRenderClass<
            WebgpuPostprocessingMaskingBasePass>(compositeResources);
    }

    /** Renders both masks, composes in pass order, and presents. */
    void render() override
    {
        sceneSet->update();
        WebgpuPostprocessingMaskingMaskFrameBuffer boxFrame;
        boxFrame.mask = boxMask->createView();
        boxFrame.mask.loadOp = LoadOp::Clear;
        boxFrame.mask.storeOp = StoreOp::Store;
        boxFrame.mask.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        boxFrame.depth = boxDepth->createView();
        boxFrame.depth.depthLoadOp = LoadOp::Clear;
        boxFrame.depth.depthStoreOp = StoreOp::Store;
        boxFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingMaskingMaskFrameBuffer torusFrame;
        torusFrame.mask = torusMask->createView();
        torusFrame.mask.loadOp = LoadOp::Clear;
        torusFrame.mask.storeOp = StoreOp::Store;
        torusFrame.mask.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        torusFrame.depth = torusDepth->createView();
        torusFrame.depth.depthLoadOp = LoadOp::Clear;
        torusFrame.depth.depthStoreOp = StoreOp::Store;
        torusFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingMaskingOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuPostprocessingMaskingBox", boxFrame, boxPass())
            ->renderPass(
                "WebgpuPostprocessingMaskingTorus",
                torusFrame,
                torusPass->setVertexBuffer(torusVertexBuffer),
                torusPass->setIndexBuffer(torusIndexBuffer),
                torusPass(
                    WebgpuPostprocessingMaskingTorusIndexCount,
                    1u, 0u, 0, 0u))
            ->renderPass(
                "WebgpuPostprocessingMaskingComposite",
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
        sceneSet->destroy();
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
