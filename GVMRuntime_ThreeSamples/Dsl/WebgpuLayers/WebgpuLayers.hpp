#ifndef GVM_THREE_WEBGPU_LAYERS_HPP
#define GVM_THREE_WEBGPU_LAYERS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuLayersTextureCapacity = 8u;

/** Stores one PlaneGeometry position and texture coordinate. */
struct WebgpuLayersVertex
{
    float4 position [[Attribute0]];
    float4 textureCoordinate [[Attribute1]];
};

/** Stores the shared camera matrix and canonical time value. */
struct WebgpuLayersObjectData
{
    float4x4 projectionView;
    float4 timeAndReserved;
};

/** Stores one blossom position, time offset, rotation, and direction. */
struct WebgpuLayersInstanceData
{
    float4 positionAndTimeOffset;
    float4 rotation;
    float4 direction;
};

/** Stores one entity's linear color and locked alpha-test threshold. */
struct WebgpuLayersMaterialData
{
    float4 baseColor;
};

/** Stores entity layer membership and the active camera layer mask. */
struct WebgpuLayersRenderLayerData
{
    uint4 entityLayerAndCameraMask;
};

/** Defines the unique three-entity, 7,500-instance Scene RenderSet. */
struct WebgpuLayersSceneRenderSet : public IRenderSet
{
    /** Declares all geometry, instance, material, layer, and texture data. */
    constructor(
        BufferComponent<WebgpuLayersVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuLayersObjectData> objects,
        BufferComponent<WebgpuLayersInstanceData> instances,
        BufferComponent<WebgpuLayersMaterialData> materials,
        BufferComponent<WebgpuLayersRenderLayerData> renderLayers,
        (TextureComponent<half4, WebgpuLayersTextureCapacity> textures))
    {
    }
};

/** Binds the explicit blossom mip sampler. */
struct WebgpuLayersSceneResources final : public IBindGroup
{
    /** Declares the linear mipmapped clamp sampler. */
    constructor(Sampler blossomSampler [[Binding0]])
    {
    }
};

/** Carries blossom UV, identity, and visibility to the fragment stage. */
struct WebgpuLayersSceneOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
    uint entityID [[Attribute1]];
    float visible [[Attribute2]];
};

/** Carries screen coordinates into the procedural background stage. */
struct WebgpuLayersScreenOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final single-sample Scene output with depth. */
struct WebgpuLayersSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only background pass attachment. */
struct WebgpuLayersBackgroundFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one nonnegative linear channel to output sRGB. */
float webgpuLayersLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies Three r185 RotateNode's Z, Y, then X matrix product. */
float3 webgpuLayersRotate(float3 position, float3 rotation)
{
    const float cosineZ = cos(rotation.z);
    const float sineZ = sin(rotation.z);
    float3 rotated = float3(
        cosineZ * position.x + sineZ * position.y,
        -sineZ * position.x + cosineZ * position.y,
        position.z);
    const float cosineY = cos(rotation.y);
    const float sineY = sin(rotation.y);
    rotated = float3(
        cosineY * rotated.x - sineY * rotated.z,
        rotated.y,
        sineY * rotated.x + cosineY * rotated.z);
    const float cosineX = cos(rotation.x);
    const float sineX = sin(rotation.x);
    return float3(
        rotated.x,
        cosineX * rotated.y + sineX * rotated.z,
        -sineX * rotated.y + cosineX * rotated.z);
}

/** Draws all three 2,500-instance blossom entities through one Scene Set. */
class WebgpuLayersSceneMainPass final : public IRenderClass
{
public:
    /** Binds the unique Set and configures opaque double-sided alpha-test state. */
    constructor(
        RenderSet<WebgpuLayersSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuLayersSceneResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves per-entity and per-instance animation data. */
    WebgpuLayersSceneOutput vertex(
        WebgpuLayersVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuLayersObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuLayersInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const WebgpuLayersRenderLayerData layerData =
            sceneSet->renderLayers->get(renderEntityID, 0u);
        const float localTime =
            instanceData.positionAndTimeOffset.w +
            objectData.timeAndReserved.x * 0.02f;
        const float moduloTime = frac(localTime);
        const float3 rotatedPosition = webgpuLayersRotate(
            inputValue.position.xyz,
            instanceData.rotation.xyz * (moduloTime * 20.0f));
        const float3 worldPosition =
            rotatedPosition +
            instanceData.positionAndTimeOffset.xyz +
            instanceData.direction.xyz * (moduloTime * 50.0f);
        float4 clipPosition = mul(
            objectData.projectionView,
            float4(worldPosition, 1.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuLayersSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityID = renderEntityID;
        outputValue.visible =
            (layerData.entityLayerAndCameraMask.x &
             layerData.entityLayerAndCameraMask.y) != 0u
                ? 1.0f
                : 0.0f;
        return outputValue;
    }

    /** Samples the sRGB blossom map and applies r185's alpha-map red channel. */
    WebgpuLayersSceneFrameBuffer fragment(WebgpuLayersSceneOutput inputValue)
    {
        if (inputValue.visible < 0.5f)
        {
            discard_fragment();
        }
        const float4 texel = float4(
            sceneSet->textures->get(inputValue.entityID, 0u)->sample(
                resources->blossomSampler,
                inputValue.textureCoordinate));
        const float alpha = texel.a * texel.r;
        const WebgpuLayersMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (alpha < materialData.baseColor.w)
        {
            discard_fragment();
        }
        const float3 linearColor =
            texel.xyz * materialData.baseColor.xyz;
        WebgpuLayersSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuLayersLinearToSrgb(linearColor.x)),
            half(webgpuLayersLinearToSrgb(linearColor.y)),
            half(webgpuLayersLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the TSL screenUV gradient before Scene geometry. */
class WebgpuLayersBackgroundPass final : public IRenderClass
{
public:
    /** Configures an ordinary color-only fullscreen draw. */
    constructor()
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle with top-oriented screen UV. */
    WebgpuLayersScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2(
            (vertexID << 1u) & 2u,
            vertexID & 2u);
        WebgpuLayersScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = uv;
        return outputValue;
    }

    /** Evaluates the exact horizontal mix plus upper-center radial light. */
    WebgpuLayersBackgroundFrameBuffer fragment(
        WebgpuLayersScreenOutput inputValue)
    {
        const float3 pink =
            float3(0.94730654f, 0.30498731f, 0.42326767f);
        const float3 yellow =
            float3(0.92158186f, 0.87136712f, 0.36625260f);
        const float3 lavender =
            float3(0.69387176f, 0.46778380f, 0.98225055f);
        const float3 horizontal =
            pink * (1.0f - inputValue.screenUv.x) +
            yellow * inputValue.screenUv.x;
        const float lightAmount =
            1.0f - distance(inputValue.screenUv, float2(0.5f, 1.0f));
        const float3 linearColor = horizontal + lavender * lightAmount;
        WebgpuLayersBackgroundFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuLayersLinearToSrgb(linearColor.x)),
            half(webgpuLayersLinearToSrgb(linearColor.y)),
            half(webgpuLayersLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the procedural background screen pass. */
class WebgpuLayersRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuLayersSceneRenderSet> sceneSet;
    Sampler blossomSampler;
    BindGroup<WebgpuLayersSceneResources> sceneResources;
    RenderClass<WebgpuLayersSceneMainPass> scenePass;
    RenderClass<WebgpuLayersBackgroundPass> backgroundPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the Scene Set, mip sampler, and both private DSL passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuLayersSceneRenderSet>();
        blossomSampler = device->createSampler({
            .label = "WebgpuLayersBlossomSampler",
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
        sceneResources = device->createBindGroup<WebgpuLayersSceneResources>(
            blossomSampler);
        scenePass = device->createRenderClass<WebgpuLayersSceneMainPass>(
            sceneSet, sceneResources);
        backgroundPass =
            device->createRenderClass<WebgpuLayersBackgroundPass>();
    }

    /** Allocates ordinary single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebgpuLayersOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuLayersDepth", width, height, 1u);
    }

    /** Draws the background and all Set-owned blossom instances. */
    void render() override
    {
        sceneSet->update();
        WebgpuLayersBackgroundFrameBuffer backgroundFrame;
        backgroundFrame.color = outputTexture->createView();
        backgroundFrame.color.loadOp = LoadOp::Clear;
        backgroundFrame.color.storeOp = StoreOp::Store;
        WebgpuLayersSceneFrameBuffer sceneFrame;
        sceneFrame.color = outputTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Load;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.depth = depthTexture->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuLayersBackground",
                backgroundFrame,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuLayersScene", sceneFrame, scenePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and both explicit attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
