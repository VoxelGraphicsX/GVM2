#ifndef GVM_THREE_WEBGPU_RTT_HPP
#define GVM_THREE_WEBGPU_RTT_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuRttTextureCapacity = 8u;

/** Stores one canonical grouped BoxGeometry position and UV. */
struct WebgpuRttVertex
{
    float4 position [[Attribute0]];
    float4 textureCoordinate [[Attribute1]];
};

/** Stores the target-frame box transform for the unique Scene entity. */
struct WebgpuRttObjectData
{
    float4x4 modelViewProjection;
};

/** Stores the mandatory identity instance record for the ordinary entity. */
struct WebgpuRttInstanceData
{
    float4 reserved;
};

/** Stores the MeshBasic color multiplier for the textured box. */
struct WebgpuRttMaterialData
{
    float4 baseColor;
};

/** Defines the unique grouped-box Scene RenderSet. */
struct WebgpuRttSceneRenderSet : public IRenderSet
{
    /** Declares the complete frozen Scene component ABI. */
    constructor(
        BufferComponent<WebgpuRttVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuRttObjectData> objects,
        BufferComponent<WebgpuRttInstanceData> instances,
        BufferComponent<WebgpuRttMaterialData> materials,
        (TextureComponent<half4, WebgpuRttTextureCapacity> textures))
    {
    }
};

/** Stores pointer-controlled saturation and hue values. */
struct WebgpuRttEffectData
{
    float4 pointerAndReserved;
};

/** Binds samplers and uniforms shared by Scene and screen processing. */
struct WebgpuRttSceneResources final : public IBindGroup
{
    /** Declares the canonical UV-grid sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Binds the offscreen Scene texture and pointer effect values. */
struct WebgpuRttScreenResources final : public IBindGroup
{
    /** Declares the Scene source, linear sampler, and effect uniform. */
    constructor(
        Texture2D<half4> sceneTexture [[Binding0]],
        Sampler sceneSampler [[Binding1]],
        UniformBuffer<WebgpuRttEffectData> effectData [[Binding2]])
    {
    }
};

/** Carries BoxGeometry UV and entity identity to the Scene fragment stage. */
struct WebgpuRttSceneOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Carries normalized fullscreen coordinates to the composite stage. */
struct WebgpuRttScreenOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
};

/** Defines the ordinary single-sample linear Scene target. */
struct WebgpuRttSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample display output. */
struct WebgpuRttOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one extended-linear sRGB channel to display sRGB. */
float webgpuRttLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the grouped textured box through the Scene's unique RenderSet. */
class WebgpuRttScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene Set and its texture sampler. */
    constructor(
        RenderSet<WebgpuRttSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuRttSceneResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the entity transform and mandatory instance component. */
    WebgpuRttSceneOutput vertex(
        WebgpuRttVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuRttObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuRttInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        float4 clipPosition = mul(
            objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuRttSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the entity-local sRGB UV grid into the linear RTT. */
    WebgpuRttSceneFrameBuffer fragment(WebgpuRttSceneOutput inputValue)
    {
        const WebgpuRttMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 textureColor = float3(
            sceneSet->textures->get(inputValue.entityID, 0u)->sample(
                resources->textureSampler,
                inputValue.textureCoordinate).xyz);
        WebgpuRttSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(textureColor * materialData.baseColor.xyz),
            half(materialData.baseColor.w));
        return frameBuffer;
    }
};

/** Applies Three r185 TSL saturation and hue to the offscreen Scene. */
class WebgpuRttCompositePass final : public IRenderClass
{
public:
    /** Binds the linear offscreen Scene without depth or blending. */
    constructor(
        BindGroup<WebgpuRttScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the standard fullscreen triangle. */
    WebgpuRttScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2(
            (vertexID << 1u) & 2u,
            vertexID & 2u);
        WebgpuRttScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.textureCoordinate = uv;
        return outputValue;
    }

    /** Evaluates exact TSL color adjustment followed by output conversion. */
    WebgpuRttOutputFrameBuffer fragment(WebgpuRttScreenOutput inputValue)
    {
        const float3 sourceColor = float3(resources->sceneTexture->sample(
            resources->sceneSampler,
            inputValue.textureCoordinate).xyz);
        const float saturationAmount =
            1.0f - resources->effectData->pointerAndReserved.x;
        const float luminance = dot(
            sourceColor,
            float3(0.2126f, 0.7152f, 0.0722f));
        const float3 saturatedColor = max(
            float3(luminance) * (1.0f - saturationAmount) +
                sourceColor * saturationAmount,
            float3(0.0f));
        const float hueAmount =
            resources->effectData->pointerAndReserved.y;
        const float cosineAngle = cos(hueAmount);
        const float sineAngle = sin(hueAmount);
        const float3 axis = float3(0.57735f);
        const float3 hueColor = max(
            saturatedColor * cosineAngle +
                cross(axis, saturatedColor) * sineAngle +
                axis * dot(axis, saturatedColor) * (1.0f - cosineAngle),
            float3(0.0f));
        WebgpuRttOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuRttLinearToSrgb(hueColor.x)),
            half(webgpuRttLinearToSrgb(hueColor.y)),
            half(webgpuRttLinearToSrgb(hueColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set, offscreen RTT, and fullscreen composite. */
class WebgpuRttRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuRttSceneRenderSet> sceneSet;
    Sampler textureSampler;
    Sampler sceneSampler;
    Buffer<WebgpuRttEffectData, BufferUsage<Uniform, CopyDst>> effectBuffer;
    BindGroup<WebgpuRttSceneResources> sceneResources;
    BindGroup<WebgpuRttScreenResources> screenResources;
    RenderClass<WebgpuRttScenePass> scenePass;
    RenderClass<WebgpuRttCompositePass> compositePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the Scene Set, uniform storage, and explicit samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuRttSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebgpuRttTextureSampler",
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
            .label = "WebgpuRttSceneSampler",
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
        effectBuffer = device->createBuffer("WebgpuRttEffectData", 1u);
        sceneResources = device->createBindGroup<WebgpuRttSceneResources>(
            textureSampler);
        scenePass = device->createRenderClass<WebgpuRttScenePass>(
            sceneSet, sceneResources);
    }

    /** Allocates all ordinary single-sample color and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuRttScene", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuRttDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuRttOutput", width, height, 1u);
        screenResources = device->createBindGroup<WebgpuRttScreenResources>(
            sceneTexture->createView(), sceneSampler, effectBuffer);
        compositePass = device->createRenderClass<WebgpuRttCompositePass>(
            screenResources);
    }

    /** Uploads the canonical pointer values used by the TSL color graph. */
    void configureEffect(float pointerX, float pointerY)
    {
        WebgpuRttEffectData effectData;
        effectData.pointerAndReserved =
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
        WebgpuRttSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {
            0.0f, 0.13286832f, 1.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuRttOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuRttScene", sceneFrame, scenePass())
            ->renderPass(
                "WebgpuRttComposite",
                outputFrame,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
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
        device->freeBuffer(effectBuffer);
    }
};

#endif
