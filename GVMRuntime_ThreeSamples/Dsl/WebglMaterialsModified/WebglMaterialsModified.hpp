#ifndef GVM_THREE_WEBGL_MATERIALS_MODIFIED_HPP
#define GVM_THREE_WEBGL_MATERIALS_MODIFIED_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Lee Perry Smith position and normal. */
struct WebglMaterialsModifiedVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores one mesh model-view, projection, and normal transform. */
struct WebglMaterialsModifiedObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4x4 normalTransform;
};

/** Stores the mandatory identity instance payload. */
struct WebglMaterialsModifiedInstanceData
{
    float4 reserved;
};

/** Stores one entity's opposed twist amount and deterministic time. */
struct WebglMaterialsModifiedMaterialData
{
    float4 twistAmountAndTime;
};

/** Defines the unique two-entity Scene RenderSet. */
struct WebglMaterialsModifiedSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry and entity component layout. */
    constructor(
        BufferComponent<WebglMaterialsModifiedVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMaterialsModifiedObjectData> objects,
        BufferComponent<WebglMaterialsModifiedInstanceData> instances,
        BufferComponent<WebglMaterialsModifiedMaterialData> materials)
    {
    }
};

/** Carries the twisted view normal into the normal material fragment. */
struct WebglMaterialsModifiedVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglMaterialsModifiedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws both opposed-twist heads through the Scene's unique RenderSet. */
class WebglMaterialsModifiedMainPass final : public IRenderClass
{
public:
    /** Configures the opaque front-sided normal material. */
    constructor(
        RenderSet<WebglMaterialsModifiedSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the exact onBeforeCompile row-vector twist to position and normal. */
    WebglMaterialsModifiedVertexOutput vertex(
        WebglMaterialsModifiedVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMaterialsModifiedObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMaterialsModifiedInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const WebglMaterialsModifiedMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        (void)instanceData;
        const float theta = sin(
            materialData.twistAmountAndTime.y + inputValue.position.y) /
            materialData.twistAmountAndTime.x;
        const float cosine = cos(theta);
        const float sine = sin(theta);
        const float3 twistedPosition = float3(
            cosine * inputValue.position.x + sine * inputValue.position.z,
            inputValue.position.y,
            -sine * inputValue.position.x + cosine * inputValue.position.z);
        const float3 viewNormal = float3(mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f)).xyz);
        const float3 twistedViewNormal = float3(
            cosine * viewNormal.x + sine * viewNormal.z,
            viewNormal.y,
            -sine * viewNormal.x + cosine * viewNormal.z);
        const float4 viewPosition = mul(
            objectData.modelView, float4(twistedPosition, 1.0f));
        float4 clipPosition = mul(objectData.projection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglMaterialsModifiedVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewNormal = normalize(twistedViewNormal);
        return outputValue;
    }

    /** Emits Three's front-sided MeshNormalMaterial display color. */
    WebglMaterialsModifiedFrameBuffer fragment(
        WebglMaterialsModifiedVertexOutput inputValue)
    {
        const float3 linearColor = normalize(inputValue.viewNormal) * 0.5f +
            float3(0.5f);
        WebglMaterialsModifiedFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns one two-entity Scene Set and a deterministic offscreen output. */
class WebglMaterialsModifiedRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsModifiedSceneRenderSet> sceneSet;
    RenderClass<WebglMaterialsModifiedMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and its single geometry pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<
            WebglMaterialsModifiedSceneRenderSet>();
        mainPass = device->createRenderClass<
            WebglMaterialsModifiedMainPass>(sceneSet);
    }

    /** Allocates ordinary single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglMaterialsModifiedRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglMaterialsModifiedDepth", width, height, 1u);
    }

    /** Submits the automatic two-entity indexed-indirect Scene draw. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglMaterialsModifiedFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass(
                "WebglMaterialsModifiedMain", frameBuffer, mainPass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final RGBA8 texture for deterministic readback. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases all private GPU resources. */
    void destroy() override
    {
        sceneSet->destroy();
        outputTexture->destroy();
        depthTexture->destroy();
    }
};

#endif
