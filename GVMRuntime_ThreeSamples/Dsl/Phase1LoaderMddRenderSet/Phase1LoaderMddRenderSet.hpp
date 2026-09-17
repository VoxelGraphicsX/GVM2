#ifndef GVM_THREE_PHASE1_LOADER_MDD_RENDER_SET_HPP
#define GVM_THREE_PHASE1_LOADER_MDD_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one expanded BoxGeometry position and its immutable face normal. */
struct MddVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores all four absolute MDD positions for one expanded BoxGeometry vertex. */
struct MddMorphTargetData
{
    float4 frame0;
    float4 frame1;
    float4 frame2;
    float4 frame3;
};

/** Stores camera transforms, current MDD weights, and output dimensions. */
struct MddObjectData
{
    float4x4 modelViewProjection;
    float4x4 normalMatrix;
    float4 frameWeights;
    float4 viewportAndReserved;
};

/** Stores the mandatory single non-instanced component record. */
struct MddInstanceData
{
    float4 translation;
};

/** Stores the private MeshNormal material controls without standalone bindings. */
struct MddMaterialData
{
    float4 opacityAndReserved;
};

/** Defines the unique Scene RenderSet used only by webgl_loader_mdd. */
struct WebglLoaderMddSceneRenderSet : public IRenderSet
{
    /** Declares expanded geometry and every per-entity MDD component. */
    constructor(BufferComponent<MddVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<MddObjectData> objects,
                BufferComponent<MddInstanceData> instances,
                BufferComponent<MddMaterialData> materials,
                BufferComponent<MddMorphTargetData> morphTargets)
    {
    }
};

/** Carries the morphed view normal and entity identity to MeshNormal shading. */
struct MddVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the single-sample Scene output. */
struct MddSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the morphing BoxGeometry through the Scene's only RenderSet. */
class WebglLoaderMddNormalMorphPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet. */
    constructor(RenderSet<WebglLoaderMddSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Blends the four absolute MDD targets at the deterministic animation time. */
    MddVertexOutput vertex(MddVertex inputValue [[VertexInput0]],
                           uint vertexID [[VertexID]],
                           uint renderEntityID [[RenderEntityID]],
                           uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const MddObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const MddInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const MddMorphTargetData targetData =
            sceneSet->morphTargets->get(renderEntityID, vertexID);
        const float4 weights = objectData.frameWeights;
        const float3 position =
            targetData.frame0.xyz * weights.x +
            targetData.frame1.xyz * weights.y +
            targetData.frame2.xyz * weights.z +
            targetData.frame3.xyz * weights.w +
            instanceData.translation.xyz;
        float4 clipPosition =
            mul(objectData.modelViewProjection, float4(position, 1.0f));
        clipPosition.y = -clipPosition.y;
        MddVertexOutput outputValue;
        outputValue.position = clipPosition;
        const float3 transformedNormal = float3(
            mul(objectData.normalMatrix,
                float4(inputValue.normal.xyz, 0.0f)).xyz);
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Implements Three r185 MeshNormalMaterial's direct encoded normal color. */
    MddSceneFrameBuffer fragment(MddVertexOutput inputValue)
    {
        const MddMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normalColor = normalize(inputValue.viewNormal) * 0.5f + 0.5f;
        MddSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(normalColor),
                                  half(materialData.opacityAndReserved.x));
        return frameBuffer;
    }
};

/** Owns the dedicated MDD Scene RenderSet and single-sample output chain. */
class Phase1LoaderMddRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderMddSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderMddNormalMorphPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene RenderSet and its single Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderMddSceneRenderSet>();
        scenePass = device->createRenderClass<WebglLoaderMddNormalMorphPass>(sceneSet);
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLoaderMddOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglLoaderMddOutputDepth", width, height, 1u);
    }

    /** Draws the unique Set directly into the single-sample output. */
    void render() override
    {
        sceneSet->update();
        MddSceneFrameBuffer outputFrame;
        configureSceneFrame(outputFrame, outputColor, outputDepth);
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebglLoaderMddMain", outputFrame, scenePass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 target. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the Scene Set and all private attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }

private:
    /** Configures the independently cleared single-sample Scene target. */
    void configureSceneFrame(
        MddSceneFrameBuffer &frame,
        Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> color,
        Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depth)
    {
        frame.color = color->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frame.depth = depth->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
    }
};

#endif
