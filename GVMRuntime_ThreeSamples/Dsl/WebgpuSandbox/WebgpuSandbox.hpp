#ifndef GVM_THREE_WEBGPU_SANDBOX_HPP
#define GVM_THREE_WEBGPU_SANDBOX_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuSandboxTextureSlots = 8u;

/** Stores the union mesh, expanded point, and expanded line vertex layout. */
struct WebgpuSandboxVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
    float4 color [[Attribute3]];
};

/** Stores the camera transform and fixed material animation time. */
struct WebgpuSandboxObjectData
{
    float4x4 model;
    float4x4 viewProjection;
    float4 timeAndViewport;
};

/** Stores the mandatory one-entry non-instanced component. */
struct WebgpuSandboxInstanceData
{
    float4 reserved;
};

/** Selects one of the six private node-material equivalents. */
struct WebgpuSandboxMaterialData
{
    uint4 kindAndPhase;
    float4 parameters;
};

/** Records the CPU triangle expansion mode and authored primitive width. */
struct WebgpuSandboxPrimitiveExpansionData
{
    float4 parameters;
};

/** Records the opaque, transparent, and alpha-test phase flags. */
struct WebgpuSandboxRenderPhaseData
{
    uint4 flags;
};

/** Defines the only RenderSet owned by the six-entity sandbox Scene. */
struct WebgpuSandboxSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, material state, and one fixed texture per entity. */
    constructor(
        BufferComponent<WebgpuSandboxVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuSandboxObjectData> objects,
        BufferComponent<WebgpuSandboxInstanceData> instances,
        BufferComponent<WebgpuSandboxMaterialData> materials,
        BufferComponent<WebgpuSandboxPrimitiveExpansionData> primitiveExpansion,
        BufferComponent<WebgpuSandboxRenderPhaseData> renderPhases,
        (TextureComponent<half4, WebgpuSandboxTextureSlots> textures))
    {
    }
};

/** Binds the repeat sampler used by the authored TextureNode expressions. */
struct WebgpuSandboxResources final : public IBindGroup
{
    /** Declares one repeat-linear sampler shared by all texture-backed entities. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries the common material inputs for both Scene material phases. */
struct WebgpuSandboxVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float3 color [[Attribute1]];
    uint2 entityAndPhase [[Attribute2]];
};

/** Defines the ordinary single-sample sandbox attachments. */
struct WebgpuSandboxFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear working-space channel with the r185 output transfer. */
float webgpuSandboxLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns the procedural checker value used by the animated box node graph. */
float webgpuSandboxChecker(float2 uv)
{
    const float2 cell = floor(uv * 2.0f);
    const float sum = cell.x + cell.y;
    return sum - floor(sum * 0.5f) * 2.0f;
}

/** Resolves one entity transform and the sphere's vertex texture displacement. */
WebgpuSandboxVertexOutput webgpuSandboxTransformVertex(
    IN RenderSet<WebgpuSandboxSceneRenderSet> sceneSet,
    BindGroup<WebgpuSandboxResources> resources,
    WebgpuSandboxVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebgpuSandboxObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebgpuSandboxInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebgpuSandboxMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    float3 localPosition = inputValue.position.xyz +
        instanceData.reserved.xyz;
    if (materialData.kindAndPhase.x == 1u)
    {
        const float displacement = float(
            sceneSet->textures->get(renderEntityID, 0u)->sampleLevel(
                resources->textureSampler, inputValue.uv.xy, 0.0f).x) * 0.25f;
        localPosition += inputValue.normal.xyz * displacement;
    }
    const float4 worldPosition = mul(
        objectData.model, float4(localPosition, 1.0f));
    WebgpuSandboxVertexOutput outputValue;
    outputValue.position = mul(objectData.viewProjection, worldPosition);
    outputValue.uv = inputValue.uv.xy;
    outputValue.color = inputValue.color.xyz;
    outputValue.entityAndPhase = uint2(
        renderEntityID, materialData.kindAndPhase.y);
    return outputValue;
}

/** Evaluates the six r185 node-material expressions in linear working space. */
float4 webgpuSandboxShade(
    IN RenderSet<WebgpuSandboxSceneRenderSet> sceneSet,
    BindGroup<WebgpuSandboxResources> resources,
    WebgpuSandboxVertexOutput inputValue)
{
    const WebgpuSandboxMaterialData materialData =
        sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
    const WebgpuSandboxObjectData objectData =
        sceneSet->objects->get(inputValue.entityAndPhase.x, 0u);
    const uint kind = materialData.kindAndPhase.x;
    float4 color = float4(inputValue.color, 1.0f);
    if (kind == 0u)
    {
        const float2 animatedUv = inputValue.uv +
            objectData.timeAndViewport.x * float2(-0.5f, 0.1f);
        const float3 texel = float3(
            sceneSet->textures->get(inputValue.entityAndPhase.x, 0u)
                ->sample(resources->textureSampler, animatedUv).xyz);
        const float checker = webgpuSandboxChecker(animatedUv);
        color.xyz = lerp(texel, float3(checker), 0.5f);
    }
    else if (kind == 1u)
    {
        const float displacement = float(
            sceneSet->textures->get(inputValue.entityAndPhase.x, 0u)
                ->sample(resources->textureSampler, inputValue.uv).x) * 0.25f;
        color.xyz = float3(displacement);
    }
    else if (kind == 2u)
    {
        color.xyz = float3(1.0f, 0.0f, 1.0f);
    }
    else if (kind == 3u)
    {
        const half4 sampledTexel =
            sceneSet->textures->get(inputValue.entityAndPhase.x, 0u)
                ->sample(resources->textureSampler, inputValue.uv);
        color.x = float(sampledTexel.x);
        color.y = float(sampledTexel.y);
        color.z = float(sampledTexel.z);
        color.w = float(sampledTexel.w);
        color.x += 0.13286832f;
        color.y += 0.03310477f;
    }
    color.xyz = float3(
        webgpuSandboxLinearToSrgb(color.x),
        webgpuSandboxLinearToSrgb(color.y),
        webgpuSandboxLinearToSrgb(color.z));
    return color;
}

/** Draws the opaque box, sphere, points, and line from the unique Scene Set. */
class WebgpuSandboxOpaquePass final : public IRenderClass
{
public:
    /** Configures the common opaque phase without MSAA. */
    constructor(
        RenderSet<WebgpuSandboxSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSandboxResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one packed entity vertex. */
    WebgpuSandboxVertexOutput vertex(
        WebgpuSandboxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuSandboxTransformVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Rejects transparent entities and evaluates the private node material. */
    WebgpuSandboxFrameBuffer fragment(WebgpuSandboxVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 0u) discard_fragment();
        WebgpuSandboxFrameBuffer frameBuffer;
        frameBuffer.color = half4(webgpuSandboxShade(
            sceneSet, resources, inputValue));
        return frameBuffer;
    }
};

/** Draws the transparent data and KTX2 planes through the same Scene Set. */
class WebgpuSandboxTransparentPass final : public IRenderClass
{
public:
    /** Configures standard source-alpha blending for the transparent phase. */
    constructor(
        RenderSet<WebgpuSandboxSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSandboxResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Transforms one transparent entity vertex. */
    WebgpuSandboxVertexOutput vertex(
        WebgpuSandboxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuSandboxTransformVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Rejects opaque entities and evaluates alpha-test plus output transfer. */
    WebgpuSandboxFrameBuffer fragment(WebgpuSandboxVertexOutput inputValue)
    {
        if (inputValue.entityAndPhase.y != 1u) discard_fragment();
        const WebgpuSandboxMaterialData materialData =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const WebgpuSandboxObjectData objectData =
            sceneSet->objects->get(inputValue.entityAndPhase.x, 0u);
        const float4 shaded = webgpuSandboxShade(
            sceneSet, resources, inputValue);
        const float oscillator = sin(
            objectData.timeAndViewport.x * 6.28318530718f) * 0.5f + 0.5f;
        if (materialData.kindAndPhase.x == 3u && shaded.w < oscillator)
        {
            discard_fragment();
        }
        WebgpuSandboxFrameBuffer frameBuffer;
        frameBuffer.color = half4(shaded);
        return frameBuffer;
    }
};

/** Owns the dedicated six-entity sandbox Scene and two material phases. */
class WebgpuSandboxRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSandboxSceneRenderSet> sceneSet;
    Sampler textureSampler;
    BindGroup<WebgpuSandboxResources> resources;
    RenderClass<WebgpuSandboxOpaquePass> opaquePass;
    RenderClass<WebgpuSandboxTransparentPass> transparentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set, repeat sampler, and both material passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuSandboxSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebgpuSandboxRepeatSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebgpuSandboxResources>(
            textureSampler);
        opaquePass = device->createRenderClass<WebgpuSandboxOpaquePass>(
            sceneSet, resources);
        transparentPass =
            device->createRenderClass<WebgpuSandboxTransparentPass>(
                sceneSet, resources);
    }

    /** Allocates the fixed single-sample RGBA8 and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuSandboxColor", width, height, 1u);
        outputDepth = device->createTexture(
            "WebgpuSandboxDepth", width, height, 1u);
    }

    /** Updates one Set and renders opaque then transparent Scene phases. */
    void render() override
    {
        sceneSet->update();
        WebgpuSandboxFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue =
            {0.13333334f, 0.13333334f, 0.13333334f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebgpuSandboxFrameBuffer transparentFrame;
        transparentFrame.color = outputColor->createView();
        transparentFrame.color.loadOp = LoadOp::Load;
        transparentFrame.color.storeOp = StoreOp::Store;
        transparentFrame.depth = outputDepth->createView();
        transparentFrame.depth.depthLoadOp = LoadOp::Load;
        transparentFrame.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuSandboxOpaque", opaqueFrame, opaquePass())
            ->renderPass("WebgpuSandboxTransparent", transparentFrame,
                         transparentPass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and private single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
