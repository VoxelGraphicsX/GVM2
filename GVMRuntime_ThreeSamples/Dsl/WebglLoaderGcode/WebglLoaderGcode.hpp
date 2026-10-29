#ifndef GVM_THREE_WEBGL_LOADER_GCODE_HPP
#define GVM_THREE_WEBGL_LOADER_GCODE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one endpoint pair and the endpoint selector used by the native LineList path. */
struct WebglLoaderGcodeVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 cornerAndDistance [[Attribute2]];
};

/** Stores the hierarchical GCode group transform and the capture viewport. */
struct WebglLoaderGcodeObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewport;
};

/** Keeps the mandatory per-entity instance component explicit for the loader group. */
struct WebglLoaderGcodeInstanceData
{
    float4 translation;
};

/** Selects the LineBasicMaterial color for one loaded child. */
struct WebglLoaderGcodeMaterialData
{
    float4 baseColor;
};

/** Consolidates the two loader-created LineSegments children into one Scene RenderSet. */
struct WebglLoaderGcodeSceneRenderSet : public IRenderSet
{
    /** Declares the single geometry store and all per-entity components used by the scene. */
    constructor(BufferComponent<WebglLoaderGcodeVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebglLoaderGcodeObjectData> objects,
                BufferComponent<WebglLoaderGcodeInstanceData> instances,
                BufferComponent<WebglLoaderGcodeMaterialData> materials)
    {
    }
};

/** Carries resolved entity color and line metadata to the fragment stage. */
struct WebglLoaderGcodeVertexOutput
{
    float4 position [[Position]];
    float3 linearColor [[Attribute0]];
    float lineDistance [[Attribute1]];
    uint renderEntityID [[Attribute2]];
    float4 lineEndpoints [[Attribute3]];
};

/** Defines the ordinary single-sample color and depth targets for the GCode scene. */
struct WebglLoaderGcodeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel using the Three.js r185 sRGB output transfer. */
float webglLoaderGcodeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Renders each GCode endpoint pair through the existing native LineList path. */
class WebglLoaderGcodeToolpathPass final : public IRenderClass
{
public:
    /** Binds the only Scene RenderSet and enables opaque line depth semantics. */
    constructor(RenderSet<WebglLoaderGcodeSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the group transform and emits one endpoint of a native line segment. */
    WebglLoaderGcodeVertexOutput vertex(WebglLoaderGcodeVertex inputValue [[VertexInput0]],
                                        uint renderEntityID [[RenderEntityID]],
                                        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLoaderGcodeObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderGcodeInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebglLoaderGcodeMaterialData materialData = sceneSet->materials->get(renderEntityID, 0u);
        const float4 start = inputValue.segmentStart + float4(instanceData.translation.xyz, 0.0f);
        const float4 end = inputValue.segmentEnd + float4(instanceData.translation.xyz, 0.0f);
        const float4 startClip = mul(objectData.modelViewProjection, start);
        const float4 endClip = mul(objectData.modelViewProjection, end);
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 halfViewport = objectData.viewport.xy * 0.5f;
        const float4 linePixels = float4(
            float2(startNdc.x, startNdc.y) * halfViewport + halfViewport,
            float2(endNdc.x, endNdc.y) * halfViewport + halfViewport);
        const bool useEnd = inputValue.cornerAndDistance.x > 0.5f;
        const float4 clipPosition = useEnd ? endClip : startClip;
        WebglLoaderGcodeVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.linearColor = materialData.baseColor.xyz;
        outputValue.lineDistance = useEnd ? inputValue.cornerAndDistance.w :
                                           inputValue.cornerAndDistance.z;
        outputValue.renderEntityID = renderEntityID;
        outputValue.lineEndpoints = linePixels;
        return outputValue;
    }

    /** Writes the resolved LineBasicMaterial color without a C++ display-side shortcut. */
    WebglLoaderGcodeFrameBuffer fragment(WebglLoaderGcodeVertexOutput inputValue)
    {
        const float3 displayColor = float3(
            webglLoaderGcodeLinearToSrgb(inputValue.linearColor.x),
            webglLoaderGcodeLinearToSrgb(inputValue.linearColor.y),
            webglLoaderGcodeLinearToSrgb(inputValue.linearColor.z));
        WebglLoaderGcodeFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the single GCode Scene RenderSet and the final readback target. */
class WebglLoaderGcodeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderGcodeSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderGcodeToolpathPass> toolpathPass;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and its dedicated toolpath pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderGcodeSceneRenderSet>();
        toolpathPass = device->createRenderClass<WebglLoaderGcodeToolpathPass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("WebglLoaderGcodeOutput", width, height, 1u);
        sceneDepth = device->createTexture("WebglLoaderGcodeDepth", width, height, 1u);
    }

    /** Applies pending RenderSet mutations, draws indexed-indirect toolpaths, and presents. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderGcodeFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = sceneDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebglLoaderGcodeToolpaths", frameBuffer, toolpathPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 texture used for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the RenderSet and single-sample output resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
