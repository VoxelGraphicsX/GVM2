#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_DIFFERENCE_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_DIFFERENCE_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuPostprocessingDifferenceTextureCapacity = 8u;

/** Stores one canonical grouped BoxGeometry position and UV. */
struct WebgpuPostprocessingDifferenceVertex
{
    float4 position [[Attribute0]];
    float4 textureCoordinate [[Attribute1]];
};

/** Stores the target-frame box transform for the unique Scene entity. */
struct WebgpuPostprocessingDifferenceObjectData
{
    float4x4 modelViewProjection;
};

/** Stores the mandatory identity instance record for the ordinary entity. */
struct WebgpuPostprocessingDifferenceInstanceData
{
    float4 reserved;
};

/** Stores the MeshBasic color multiplier for the textured box. */
struct WebgpuPostprocessingDifferenceMaterialData
{
    float4 baseColor;
};

/** Defines the unique grouped-box Scene RenderSet. */
struct WebgpuPostprocessingDifferenceSceneRenderSet : public IRenderSet
{
    /** Declares the complete frozen Scene component ABI. */
    constructor(
        BufferComponent<WebgpuPostprocessingDifferenceVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingDifferenceObjectData> objects,
        BufferComponent<WebgpuPostprocessingDifferenceInstanceData> instances,
        BufferComponent<WebgpuPostprocessingDifferenceMaterialData> materials,
        (TextureComponent<half4, WebgpuPostprocessingDifferenceTextureCapacity> textures))
    {
    }
};

/** Stores the fixed animation speed and reserved state for the history pass. */
struct WebgpuPostprocessingDifferenceEffectData
{
    float4 speedAndReserved;
};

/** Binds samplers and uniforms shared by Scene and screen processing. */
struct WebgpuPostprocessingDifferenceSceneResources final : public IBindGroup
{
    /** Declares the canonical UV-grid sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Binds the offscreen Scene texture and pointer effect values. */
struct WebgpuPostprocessingDifferenceScreenResources final : public IBindGroup
{
    /** Declares current/previous Scene sources, sampler, and effect uniform. */
    constructor(
        Texture2D<half4> sceneTexture [[Binding0]],
        Texture2D<half4> previousTexture [[Binding1]],
        Sampler sceneSampler [[Binding2]],
        UniformBuffer<WebgpuPostprocessingDifferenceEffectData> effectData [[Binding3]])
    {
    }
};

/** Binds the current Scene texture while updating the previous-frame target. */
struct WebgpuPostprocessingDifferenceHistoryResources final : public IBindGroup
{
    /** Declares the current source and its clamped sampler. */
    constructor(Texture2D<half4> sceneTexture [[Binding0]], Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries BoxGeometry UV and entity identity to the Scene fragment stage. */
struct WebgpuPostprocessingDifferenceSceneOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Carries normalized fullscreen coordinates to the composite stage. */
struct WebgpuPostprocessingDifferenceScreenOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
};

/** Defines the ordinary single-sample linear Scene target. */
struct WebgpuPostprocessingDifferenceSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample display output. */
struct WebgpuPostprocessingDifferenceOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines the persistent previous-frame history attachment. */
struct WebgpuPostprocessingDifferenceHistoryFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one extended-linear sRGB channel to display sRGB. */
float webgpuPostprocessingDifferenceLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the exact r185 NeutralToneMapping curve before display transfer. */
float3 webgpuPostprocessingDifferenceNeutralToneMap(float3 color)
{
    const float minimumChannel = min(color.x, min(color.y, color.z));
    const float offset = minimumChannel < 0.08f
        ? minimumChannel - 6.25f * minimumChannel * minimumChannel
        : 0.04f;
    color -= float3(offset);
    const float peak = max(color.x, max(color.y, color.z));
    if (peak < 0.76f) return color;
    const float newPeak = 1.0f - 0.0576f / (peak - 0.52f);
    color *= newPeak / peak;
    const float weight =
        1.0f - 1.0f / (0.15f * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak), weight);
}

/** Draws the grouped textured box through the Scene's unique RenderSet. */
class WebgpuPostprocessingDifferenceScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene Set and its texture sampler. */
    constructor(
        RenderSet<WebgpuPostprocessingDifferenceSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuPostprocessingDifferenceSceneResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the entity transform and mandatory instance component. */
    WebgpuPostprocessingDifferenceSceneOutput vertex(
        WebgpuPostprocessingDifferenceVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingDifferenceObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingDifferenceInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        float4 clipPosition = mul(
            objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingDifferenceSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the entity-local sRGB UV grid into the linear RTT. */
    WebgpuPostprocessingDifferenceSceneFrameBuffer fragment(WebgpuPostprocessingDifferenceSceneOutput inputValue)
    {
        const WebgpuPostprocessingDifferenceMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 textureColor = float3(
            sceneSet->textures->get(inputValue.entityID, 0u)->sample(
                resources->textureSampler,
                inputValue.textureCoordinate).xyz);
        WebgpuPostprocessingDifferenceSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(textureColor * materialData.baseColor.xyz),
            half(materialData.baseColor.w));
        return frameBuffer;
    }
};

/** Applies Three r185 TSL saturation and hue to the offscreen Scene. */
class WebgpuPostprocessingDifferenceCompositePass final : public IRenderClass
{
public:
    /** Binds the linear offscreen Scene without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingDifferenceScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the standard fullscreen triangle. */
    WebgpuPostprocessingDifferenceScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2(
            (vertexID << 1u) & 2u,
            vertexID & 2u);
        WebgpuPostprocessingDifferenceScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.textureCoordinate = uv;
        return outputValue;
    }

    /** Evaluates exact TSL color adjustment followed by output conversion. */
    WebgpuPostprocessingDifferenceOutputFrameBuffer fragment(WebgpuPostprocessingDifferenceScreenOutput inputValue)
    {
        const float3 sourceColor = float3(resources->sceneTexture->sample(
            resources->sceneSampler,
            inputValue.textureCoordinate).xyz);
        const float3 previousColor = float3(resources->previousTexture->sample(
            resources->sceneSampler,
            inputValue.textureCoordinate).xyz);
        const float3 frameDiff = abs(previousColor - sourceColor);
        const float diffLuminance = dot(
            frameDiff,
            float3(0.2126f, 0.7152f, 0.0722f));
        const float saturationAmount = clamp(diffLuminance * 1000.0f, 0.0f, 3.0f);
        const float luminance = dot(
            sourceColor,
            float3(0.2126f, 0.7152f, 0.0722f));
        const float3 saturatedColor = max(
            float3(luminance) * (1.0f - saturationAmount) +
                sourceColor * saturationAmount,
            float3(0.0f));
        WebgpuPostprocessingDifferenceOutputFrameBuffer frameBuffer;
        const float3 toneMappedColor =
            webgpuPostprocessingDifferenceNeutralToneMap(saturatedColor);
        frameBuffer.color = half4(
            half(webgpuPostprocessingDifferenceLinearToSrgb(toneMappedColor.x)),
            half(webgpuPostprocessingDifferenceLinearToSrgb(toneMappedColor.y)),
            half(webgpuPostprocessingDifferenceLinearToSrgb(toneMappedColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Copies the current Scene frame into the persistent previous-frame texture. */
class WebgpuPostprocessingDifferenceHistoryPass final : public IRenderClass
{
public:
    /** Binds the current Scene source and its sampler. */
    constructor(BindGroup<WebgpuPostprocessingDifferenceHistoryResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle used for history storage. */
    WebgpuPostprocessingDifferenceScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuPostprocessingDifferenceScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.textureCoordinate = uv;
        return outputValue;
    }

    /** Writes the current linear Scene texture without additional conversion. */
    WebgpuPostprocessingDifferenceHistoryFrameBuffer fragment(
        WebgpuPostprocessingDifferenceScreenOutput inputValue)
    {
        const half4 color = resources->sceneTexture->sample(
            resources->sceneSampler, inputValue.textureCoordinate);
        WebgpuPostprocessingDifferenceHistoryFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Clears the previous-frame history to the r185 scene background once. */
class WebgpuPostprocessingDifferenceHistoryClearPass final : public IRenderClass
{
public:
    /** Configures a fullscreen color-only initialization pass. */
    constructor()
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle used for history initialization. */
    WebgpuPostprocessingDifferenceScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuPostprocessingDifferenceScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.textureCoordinate = uv;
        return outputValue;
    }

    /** Writes the exact scene background used by the reference. */
    WebgpuPostprocessingDifferenceHistoryFrameBuffer fragment(
        WebgpuPostprocessingDifferenceScreenOutput inputValue)
    {
        (void)inputValue;
        WebgpuPostprocessingDifferenceHistoryFrameBuffer frameBuffer;
        frameBuffer.color = half4(0.0f, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

/** Owns the unique Scene Set, offscreen RTT, and fullscreen composite. */
class WebgpuPostprocessingDifferenceRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuPostprocessingDifferenceSceneRenderSet> sceneSet;
    Sampler textureSampler;
    Sampler sceneSampler;
    Buffer<WebgpuPostprocessingDifferenceEffectData, BufferUsage<Uniform, CopyDst>> effectBuffer;
    BindGroup<WebgpuPostprocessingDifferenceSceneResources> sceneResources;
    BindGroup<WebgpuPostprocessingDifferenceScreenResources> screenResources;
    BindGroup<WebgpuPostprocessingDifferenceHistoryResources> historyResources;
    RenderClass<WebgpuPostprocessingDifferenceScenePass> scenePass;
    RenderClass<WebgpuPostprocessingDifferenceCompositePass> compositePass;
    RenderClass<WebgpuPostprocessingDifferenceHistoryPass> historyPass;
    RenderClass<WebgpuPostprocessingDifferenceHistoryClearPass> historyClearPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> previousTexture;
    uint frameCounter = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the Scene Set, uniform storage, and explicit samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuPostprocessingDifferenceSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebgpuPostprocessingDifferenceTextureSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 16.0f,
            .maxAnisotropy = 1u,
        });
        sceneSampler = device->createSampler({
            .label = "WebgpuPostprocessingDifferenceSceneSampler",
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
        effectBuffer = device->createBuffer("WebgpuPostprocessingDifferenceEffectData", 1u);
        sceneResources = device->createBindGroup<WebgpuPostprocessingDifferenceSceneResources>(
            textureSampler);
        scenePass = device->createRenderClass<WebgpuPostprocessingDifferenceScenePass>(
            sceneSet, sceneResources);
    }

    /** Allocates all ordinary single-sample color and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuPostprocessingDifferenceScene", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuPostprocessingDifferenceDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuPostprocessingDifferenceOutput", width, height, 1u);
        previousTexture = device->createTexture(
            "WebgpuPostprocessingDifferencePrevious", width, height, 1u);
        screenResources = device->createBindGroup<WebgpuPostprocessingDifferenceScreenResources>(
            sceneTexture->createView(), previousTexture->createView(), sceneSampler, effectBuffer);
        compositePass = device->createRenderClass<WebgpuPostprocessingDifferenceCompositePass>(
            screenResources);
        historyResources = device->createBindGroup<WebgpuPostprocessingDifferenceHistoryResources>(
            sceneTexture->createView(), sceneSampler);
        historyPass = device->createRenderClass<WebgpuPostprocessingDifferenceHistoryPass>(
            historyResources);
        historyClearPass = device->createRenderClass<WebgpuPostprocessingDifferenceHistoryClearPass>();
    }

    /** Uploads the canonical pointer values used by the TSL color graph. */
    void configureEffect(float pointerX, float pointerY)
    {
        WebgpuPostprocessingDifferenceEffectData effectData;
        effectData.speedAndReserved =
            float4(pointerX, pointerY, 0.0f, 0.0f);
        graphicsQueue->writeBuffer(
            BufferRange(effectBuffer),
            &effectData,
            sizeof(effectData))->submit();
    }

    /** Renders the Scene Set once and composites its RTT to the output. */
    void render() override
    {
        sceneSet->update();
        WebgpuPostprocessingDifferenceSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {
            0.00121411f, 0.24228112f, 0.76052450f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingDifferenceOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingDifferenceHistoryFrameBuffer historyFrame;
        historyFrame.color = previousTexture->createView();
        historyFrame.color.loadOp = LoadOp::Load;
        historyFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        auto command = graphicsQueue;
        if (frameCounter == 0u)
        {
            WebgpuPostprocessingDifferenceHistoryFrameBuffer clearFrame;
            clearFrame.color = previousTexture->createView();
            clearFrame.color.loadOp = LoadOp::Clear;
            clearFrame.color.storeOp = StoreOp::Store;
            clearFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
            command->renderPass(
                "WebgpuPostprocessingDifferenceHistoryClear",
                clearFrame,
                historyClearPass(3u, 1u, 0u, 0u));
        }
        command
            ->renderPass("WebgpuPostprocessingDifferenceScene", sceneFrame, scenePass())
            ->renderPass(
                "WebgpuPostprocessingDifferenceComposite",
                outputFrame,
                compositePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuPostprocessingDifferenceHistory",
                historyFrame,
                historyPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        ++frameCounter;
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Set, attachments, and explicit uniform storage. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeTexture(previousTexture);
        device->freeBuffer(effectBuffer);
    }
};

#endif
