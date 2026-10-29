#ifndef GVM_THREE_SVG_LINES_HPP
#define GVM_THREE_SVG_LINES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one CPU-projected SVG stroke triangle vertex. */
struct SvgLinesVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Stores the required per-entity payload for the unique SVG Scene RenderSet. */
struct SvgLinesObjectData
{
    float4 geometryAndFlags;
};

/** Stores the mandatory non-instanced component entry. */
struct SvgLinesInstanceData
{
    float4 reserved;
};

/** Stores the source stroke color and width for structural parity. */
struct SvgLinesMaterialData
{
    float4 colorAndWidth;
};

/** Defines the unique RenderSet containing all four SVG line entities. */
struct SvgLinesSceneRenderSet : public IRenderSet
{
    /** Declares consolidated triangle geometry and entity components. */
    constructor(BufferComponent<SvgLinesVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<SvgLinesObjectData> objects,
                BufferComponent<SvgLinesInstanceData> instances,
                BufferComponent<SvgLinesMaterialData> materials)
    {
    }
};

/** Carries the direct clip position and source SVG color. */
struct SvgLinesVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
};

/** Defines the normal single-sample SVG compatibility output. */
struct SvgLinesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws all pre-expanded SVG strokes through one RenderSet submission. */
class SvgLinesStrokePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet with the SVG projector depth order. */
    constructor(RenderSet<SvgLinesSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads entity and instance components while forwarding CPU-projected triangles. */
    SvgLinesVertexOutput vertex(
        SvgLinesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const SvgLinesObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const SvgLinesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const SvgLinesMaterialData materialData = sceneSet->materials->get(renderEntityID, 0u);
        SvgLinesVertexOutput outputValue;
        outputValue.position = inputValue.position +
            float4(instanceData.reserved.xyz + objectData.geometryAndFlags.yzw, 0.0f);
        outputValue.color = inputValue.color * materialData.colorAndWidth.x;
        return outputValue;
    }

    /** Writes the non-color-managed SVG stroke bytes directly to RGBA8. */
    SvgLinesFrameBuffer fragment(SvgLinesVertexOutput inputValue)
    {
        SvgLinesFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

/** Owns the dedicated SVG Scene RenderSet and single-sample output target. */
class SvgLinesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<SvgLinesSceneRenderSet> sceneSet;
    RenderClass<SvgLinesStrokePass> strokePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and dedicated stroke pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<SvgLinesSceneRenderSet>();
        strokePass = device->createRenderClass<SvgLinesStrokePass>(sceneSet);
    }

    /** Allocates the ordinary single-sample RGBA8 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("SvgLinesOutput", width, height, 1u);
        depthTexture = device->createTexture("SvgLinesDepth32", width, height, 1u);
    }

    /** Draws all four entities with the RenderSet-only indexed-indirect path. */
    void render() override
    {
        sceneSet->update();
        SvgLinesFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Discard;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("SvgLinesStroke", frameBuffer, strokePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 capture target. */
    auto getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the unique RenderSet and output texture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
