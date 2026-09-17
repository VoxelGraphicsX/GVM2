#ifndef GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_AFTERIMAGE_RENDER_SET_HPP
#define GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_AFTERIMAGE_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact segmented BoxGeometry position and face normal. */
struct AfterimageSceneVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the current deterministic model-view transforms. */
struct AfterimageObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
};

/** Stores the mandatory non-instanced translation component. */
struct AfterimageInstanceData
{
    float4 translation;
};

/** Stores the private MeshNormal material multiplier. */
struct AfterimageMaterialData
{
    float4 normalColorMultiplier;
};

/** Defines the only RenderSet owned by the Afterimage Scene. */
struct WebglPostprocessingAfterimageSceneRenderSet : public IRenderSet
{
    /** Declares the exact five-component Scene ABI. */
    constructor(
        BufferComponent<AfterimageSceneVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<AfterimageObjectData> objects,
        BufferComponent<AfterimageInstanceData> instances,
        BufferComponent<AfterimageMaterialData> materials)
    {
    }
};

/** Stores the fixed feedback damping value and output dimensions. */
struct AfterimageScreenUniforms
{
    float4 dampEnabledAndReserved;
    float4 viewportAndReserved;
};

/** Binds the two feedback inputs and exact nearest sampler. */
struct AfterimageComposeResources final : public IBindGroup
{
    /** Declares old history, current Scene, sampler, and damping state. */
    constructor(
        Texture2D<float4> oldHistory [[Binding0]],
        Texture2D<float4> newScene [[Binding1]],
        Sampler nearestSampler [[Binding2]],
        UniformBuffer<AfterimageScreenUniforms> uniforms [[Binding3]])
    {
    }
};

/** Binds one linear intermediate texture for copy or output. */
struct AfterimageTextureResources final : public IBindGroup
{
    /** Declares one sampled texture and its exact nearest sampler. */
    constructor(
        Texture2D<float4> inputTexture [[Binding0]],
        Sampler nearestSampler [[Binding1]])
    {
    }
};

/** Carries view-space normal and entity identity into MeshNormal shading. */
struct AfterimageSceneOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Carries fullscreen UV coordinates into temporal screen passes. */
struct AfterimageScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear half-float feedback attachment. */
struct AfterimageLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the Scene attachment with depth. */
struct AfterimageSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final display-encoded capture attachment. */
struct AfterimageOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel with Three r185's exact sRGB transfer. */
float afterimageLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Emits the shared fullscreen triangle with unmodified EffectComposer UVs. */
AfterimageScreenOutput afterimageFullscreenVertex(uint vertexID)
{
    const float2 uv =
        float2((vertexID << 1u) & 2u, vertexID & 2u);
    AfterimageScreenOutput outputValue;
    outputValue.position =
        float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Draws the segmented box through the Scene's only RenderSet. */
class WebglPostprocessingAfterimageMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet. */
    constructor(
        RenderSet<WebglPostprocessingAfterimageSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies entity and instance transforms with current frame rotation. */
    AfterimageSceneOutput vertex(
        AfterimageSceneVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const AfterimageObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const AfterimageInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float4 position = float4(
            inputValue.position.xyz + instanceData.translation.xyz,
            1.0f);
        float4 clipPosition =
            mul(objectData.modelViewProjection, position);
        clipPosition.y = -clipPosition.y;
        clipPosition.z =
            (clipPosition.z + clipPosition.w) * 0.5f;
        AfterimageSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewNormal = mul(
            objectData.modelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Reproduces MeshNormalMaterial's linear packed-normal output. */
    AfterimageSceneFrameBuffer fragment(
        AfterimageSceneOutput inputValue)
    {
        const AfterimageMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 packedNormal =
            (normalize(inputValue.viewNormal) * 0.5f + 0.5f) *
            materialData.normalColorMultiplier.xyz;
        AfterimageSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(packedNormal), half(1.0f));
        return frameBuffer;
    }
};

/** Clears both persistent feedback textures before the first frame. */
class WebglPostprocessingAfterimageHistoryClearPass final : public IRenderClass
{
public:
    /** Configures a depth-free overwrite pass. */
    constructor()
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    AfterimageScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return afterimageFullscreenVertex(vertexID);
    }

    /** Writes deterministic transparent black history. */
    AfterimageLinearFrameBuffer fragment(
        AfterimageScreenOutput inputValue)
    {
        (void)inputValue;
        AfterimageLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(0.0f);
        return frameBuffer;
    }
};

/** Applies the exact r185 thresholded maximum feedback equation. */
class WebglPostprocessingAfterimageComposePass final : public IRenderClass
{
public:
    /** Binds one fixed old-history/current-Scene pair. */
    constructor(
        BindGroup<AfterimageComposeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    AfterimageScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return afterimageFullscreenVertex(vertexID);
    }

    /** Reproduces AfterimageShader including per-channel thresholding. */
    AfterimageLinearFrameBuffer fragment(
        AfterimageScreenOutput inputValue)
    {
        float4 oldTexel = resources->oldHistory->sample(
            resources->nearestSampler, inputValue.uv);
        const float4 newTexel = resources->newScene->sample(
            resources->nearestSampler, inputValue.uv);
        const float4 thresholdMask =
            max(sign(oldTexel - 0.1f), float4(0.0f));
        oldTexel *=
            resources->uniforms->dampEnabledAndReserved.x *
            thresholdMask;
        AfterimageLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(max(newTexel, oldTexel));
        return frameBuffer;
    }
};

/** Copies the composed feedback into EffectComposer's current buffer. */
class WebglPostprocessingAfterimageCopyPass final : public IRenderClass
{
public:
    /** Binds one composed history texture. */
    constructor(
        BindGroup<AfterimageTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    AfterimageScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return afterimageFullscreenVertex(vertexID);
    }

    /** Performs CopyShader's unmodified linear texture copy. */
    AfterimageLinearFrameBuffer fragment(
        AfterimageScreenOutput inputValue)
    {
        AfterimageLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->inputTexture->sample(
            resources->nearestSampler, inputValue.uv));
        return frameBuffer;
    }
};

/** Converts the selected composer input into display sRGB. */
class WebglPostprocessingAfterimageOutputPass final : public IRenderClass
{
public:
    /** Binds one enabled or disabled path input. */
    constructor(
        BindGroup<AfterimageTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    AfterimageScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return afterimageFullscreenVertex(vertexID);
    }

    /** Applies OutputPass's linear-to-sRGB conversion. */
    AfterimageOutputFrameBuffer fragment(
        AfterimageScreenOutput inputValue)
    {
        const float4 linearColor =
            resources->inputTexture->sample(
                resources->nearestSampler, inputValue.uv);
        AfterimageOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(afterimageLinearToSrgb(linearColor.x)),
            half(afterimageLinearToSrgb(linearColor.y)),
            half(afterimageLinearToSrgb(linearColor.z)),
            half(linearColor.w));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and exact temporal feedback chain. */
class Phase1WebglPostprocessingAfterimageRenderSetRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingAfterimageSceneRenderSet> sceneSet;
    Buffer<AfterimageScreenUniforms, BufferUsage<Uniform, CopyDst>>
        screenUniformBuffer;
    Sampler nearestSampler;
    RenderClass<WebglPostprocessingAfterimageMainPass> scenePass;
    RenderClass<WebglPostprocessingAfterimageHistoryClearPass> clearPass;
    BindGroup<AfterimageComposeResources> composeToAResources;
    BindGroup<AfterimageComposeResources> composeToBResources;
    RenderClass<WebglPostprocessingAfterimageComposePass> composeToAPass;
    RenderClass<WebglPostprocessingAfterimageComposePass> composeToBPass;
    BindGroup<AfterimageTextureResources> copyAResources;
    BindGroup<AfterimageTextureResources> copyBResources;
    RenderClass<WebglPostprocessingAfterimageCopyPass> copyAPass;
    RenderClass<WebglPostprocessingAfterimageCopyPass> copyBPass;
    BindGroup<AfterimageTextureResources> outputComposerResources;
    BindGroup<AfterimageTextureResources> outputSceneResources;
    RenderClass<WebglPostprocessingAfterimageOutputPass> outputComposerPass;
    RenderClass<WebglPostprocessingAfterimageOutputPass> outputScenePass;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> historyA;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> historyB;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> composerColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    uint width = 800u;
    uint height = 500u;
    bool effectEnabled = true;
    bool oldHistoryIsA = true;

public:
    /** Creates the unique Scene Set and immutable nearest sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglPostprocessingAfterimageSceneRenderSet>();
        screenUniformBuffer =
            device->createBuffer("AfterimageScreenUniforms", 1u);
        SamplerDescriptor samplerDescriptor;
        samplerDescriptor.addressModeU = AddressMode::ClampToEdge;
        samplerDescriptor.addressModeV = AddressMode::ClampToEdge;
        samplerDescriptor.addressModeW = AddressMode::ClampToEdge;
        samplerDescriptor.magFilter = FilterMode::Nearest;
        samplerDescriptor.minFilter = FilterMode::Nearest;
        samplerDescriptor.mipmapFilter = MipmapFilterMode::Nearest;
        samplerDescriptor.lodMinClamp = 0.0f;
        samplerDescriptor.lodMaxClamp = 0.0f;
        samplerDescriptor.compare = CompareFunction::Undefined;
        samplerDescriptor.maxAnisotropy = 1u;
        nearestSampler = device->createSampler(samplerDescriptor);
        scenePass =
            device->createRenderClass<
                WebglPostprocessingAfterimageMainPass>(sceneSet);
        clearPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageHistoryClearPass>();
    }

    /** Stores the host capture dimensions before scenario allocation. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
    }

    /** Allocates all temporal targets and freezes scenario controls. */
    void configureScenario(
        uint inWidth,
        uint inHeight,
        float damp,
        bool enabled)
    {
        width = inWidth;
        height = inHeight;
        effectEnabled = enabled;
        oldHistoryIsA = true;
        sceneColor =
            device->createTexture("AfterimageSceneColor", width, height, 1u);
        sceneDepth =
            device->createTexture("AfterimageSceneDepth", width, height, 1u);
        historyA =
            device->createTexture("AfterimageHistoryA", width, height, 1u);
        historyB =
            device->createTexture("AfterimageHistoryB", width, height, 1u);
        composerColor =
            device->createTexture("AfterimageComposerColor", width, height, 1u);
        outputColor =
            device->createTexture("AfterimageOutput", width, height, 1u);
        const AfterimageScreenUniforms uniforms = {
            float4(damp, enabled ? 1.0f : 0.0f, 0.0f, 0.0f),
            float4(float(width), float(height), 0.0f, 0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(screenUniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        composeToAResources =
            device->createBindGroup<AfterimageComposeResources>(
                historyB->createView(),
                sceneColor->createView(),
                nearestSampler,
                screenUniformBuffer);
        composeToBResources =
            device->createBindGroup<AfterimageComposeResources>(
                historyA->createView(),
                sceneColor->createView(),
                nearestSampler,
                screenUniformBuffer);
        composeToAPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageComposePass>(
                composeToAResources);
        composeToBPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageComposePass>(
                composeToBResources);
        copyAResources =
            device->createBindGroup<AfterimageTextureResources>(
                historyA->createView(), nearestSampler);
        copyBResources =
            device->createBindGroup<AfterimageTextureResources>(
                historyB->createView(), nearestSampler);
        copyAPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageCopyPass>(copyAResources);
        copyBPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageCopyPass>(copyBResources);
        outputComposerResources =
            device->createBindGroup<AfterimageTextureResources>(
                composerColor->createView(), nearestSampler);
        outputSceneResources =
            device->createBindGroup<AfterimageTextureResources>(
                sceneColor->createView(), nearestSampler);
        outputComposerPass =
            device->createRenderClass<
                WebglPostprocessingAfterimageOutputPass>(
                outputComposerResources);
        outputScenePass =
            device->createRenderClass<
                WebglPostprocessingAfterimageOutputPass>(
                outputSceneResources);
        AfterimageLinearFrameBuffer clearA;
        configureLinearFrame(clearA, historyA);
        AfterimageLinearFrameBuffer clearB;
        configureLinearFrame(clearB, historyB);
        graphicsQueue
            ->renderPass(
                "AfterimageClearA",
                clearA,
                clearPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "AfterimageClearB",
                clearB,
                clearPass(3u, 1u, 0u, 0u))
            ->submit();
    }

    /** Draws one deterministic frame and advances enabled feedback history. */
    void render() override
    {
        sceneSet->update();
        AfterimageSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneColor->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        AfterimageLinearFrameBuffer historyAFrame;
        configureLinearFrame(historyAFrame, historyA);
        AfterimageLinearFrameBuffer historyBFrame;
        configureLinearFrame(historyBFrame, historyB);
        AfterimageLinearFrameBuffer composerFrame;
        configureLinearFrame(composerFrame, composerColor);
        AfterimageOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto nextTexture = swapchain->queryNextTexture();
        if (effectEnabled && oldHistoryIsA)
        {
            graphicsQueue
                ->renderPass(
                    "AfterimageScene",
                    sceneFrame,
                    scenePass())
                ->renderPass(
                    "AfterimageComposeToB",
                    historyBFrame,
                    composeToBPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "AfterimageCopyB",
                    composerFrame,
                    copyBPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "AfterimageOutput",
                    outputFrame,
                    outputComposerPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputColor,
                    RenderToSwapchainDescriptor{})
                ->submit();
            oldHistoryIsA = false;
        }
        else if (effectEnabled)
        {
            graphicsQueue
                ->renderPass(
                    "AfterimageScene",
                    sceneFrame,
                    scenePass())
                ->renderPass(
                    "AfterimageComposeToA",
                    historyAFrame,
                    composeToAPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "AfterimageCopyA",
                    composerFrame,
                    copyAPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "AfterimageOutput",
                    outputFrame,
                    outputComposerPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputColor,
                    RenderToSwapchainDescriptor{})
                ->submit();
            oldHistoryIsA = true;
        }
        else
        {
            graphicsQueue
                ->renderPass(
                    "AfterimageScene",
                    sceneFrame,
                    scenePass())
                ->renderPass(
                    "AfterimageDirectOutput",
                    outputFrame,
                    outputScenePass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputColor,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 target. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the Scene Set and every temporal attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(screenUniformBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(historyA);
        device->freeTexture(historyB);
        device->freeTexture(composerColor);
        device->freeTexture(outputColor);
    }

private:
    /** Configures one preserved linear half-float target. */
    void configureLinearFrame(
        AfterimageLinearFrameBuffer &frame,
        Texture<TextureFormat::RGBA16Float,
                TextureUsage<RenderAttachment, TextureBinding>,
                TextureDimension::e2D> texture)
    {
        frame.color = texture->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue = {0.0, 0.0, 0.0, 0.0};
    }
};

#endif
