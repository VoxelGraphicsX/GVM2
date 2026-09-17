#ifndef GVM_THREE_WEBGL_GEOMETRY_EXTRUDE_SPLINES_HPP
#define GVM_THREE_WEBGL_GEOMETRY_EXTRUDE_SPLINES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores either one tube vertex or one endpoint of a wireframe segment. */
struct WebglGeometryExtrudeSplinesVertex
{
    float4 position [[Attribute0]];
    float4 normalOrLineEnd [[Attribute1]];
    float4 lineData [[Attribute2]];
};

/** Stores one spline entity transform, projection, viewport, and lighting state. */
struct WebglGeometryExtrudeSplinesObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
    float4 ambientAndDirectionalIntensity;
    float4 directionalView;
};

/** Stores the mandatory non-instanced component entry. */
struct WebglGeometryExtrudeSplinesInstanceData
{
    float4 reserved;
    float4 lineStart;
    float4 lineEnd;
};

/** Stores one spline material color, opacity, and render phase. */
struct WebglGeometryExtrudeSplinesMaterialData
{
    float4 colorAndOpacity;
    float4 phaseAndReserved;
};

/** Defines the unique four-entity RenderSet for spline extrusion. */
struct WebglGeometryExtrudeSplinesSceneRenderSet : public IRenderSet
{
    /** Declares packed tube, wireframe, helper, and material component storage. */
    constructor(
        BufferComponent<WebglGeometryExtrudeSplinesVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryExtrudeSplinesObjectData> objects,
        BufferComponent<WebglGeometryExtrudeSplinesInstanceData> instances,
        BufferComponent<WebglGeometryExtrudeSplinesMaterialData> materials)
    {
    }
};

/** Carries spline surface and expanded-line data to one Scene fragment pass. */
struct WebglGeometryExtrudeSplinesVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    uint entityID [[Attribute1]];
    float phase [[Attribute2]];
    float4 linePixels [[Attribute3]];
    uint lineInstanceID [[Attribute4]];
};

/** Defines the ordinary color and depth attachments shared by both Scene passes. */
struct WebglGeometryExtrudeSplinesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear-light channel for the browser canvas. */
float webglGeometryExtrudeSplinesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms tube vertices and resolves wireframe segment endpoints. */
WebglGeometryExtrudeSplinesVertexOutput
webglGeometryExtrudeSplinesTransformVertex(
    IN RenderSet<WebglGeometryExtrudeSplinesSceneRenderSet> sceneSet,
    WebglGeometryExtrudeSplinesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglGeometryExtrudeSplinesObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGeometryExtrudeSplinesMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const WebglGeometryExtrudeSplinesInstanceData instanceData =
        sceneSet->instances->get(
            renderEntityID,
            renderEntityInstanceID);
    const float4 localPosition =
        inputValue.position +
        float4(instanceData.reserved.xyz, 0.0f);
    float4 viewPosition =
        mul(objectData.modelView, localPosition);
    float4 clipPosition =
        mul(objectData.projection, viewPosition);
    const float phase =
        materialData.phaseAndReserved.x;
    float4 linePixels = float4(0.0f);
    float3 viewNormal =
        normalize(
            float3(
                mul(
                    objectData.modelView,
                    float4(
                        inputValue.normalOrLineEnd.xyz,
                        0.0f))
                    .xyz));
    if (phase > 0.5f && phase < 1.5f)
    {
        const float4 lineStartView =
            mul(objectData.modelView, instanceData.lineStart);
        const float4 lineEndView =
            mul(objectData.modelView, instanceData.lineEnd);
        const float4 lineStartClip =
            mul(objectData.projection, lineStartView);
        const float4 lineEndClip =
            mul(objectData.projection, lineEndView);
        const float2 startNdc = lineStartClip.xy / lineStartClip.w;
        const float2 endNdc = lineEndClip.xy / lineEndClip.w;
        const float2 directionPixels =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 tangent = directionPixels /
            max(length(directionPixels), 0.0001f);
        const float2 lineNormal =
            float2(-directionPixels.y, directionPixels.x) /
            max(length(directionPixels), 0.0001f);
        // A tiny y-only tie break matches the top-left ownership used by
        // Three's one-pixel GL_LINES rasterization at exact pixel centers.
        const float2 rasterTieBreak = float2(0.25f, 0.0f);
        linePixels = float4(
            float2(startNdc.x, -startNdc.y) * objectData.viewport.xy +
                objectData.viewport.xy + objectData.viewport.zw +
                rasterTieBreak,
            float2(endNdc.x, -endNdc.y) * objectData.viewport.xy +
                objectData.viewport.xy + objectData.viewport.zw +
                rasterTieBreak);
        const bool useEnd = inputValue.lineData.x > 0.5f;
        clipPosition = useEnd ? lineEndClip : lineStartClip;
        const float endpointExtension = 0.0f;
        clipPosition.xy +=
            (lineNormal * inputValue.lineData.y +
                tangent * endpointExtension) /
            objectData.viewport.xy * clipPosition.w;
        viewNormal = float3(0.0f);
    }
    clipPosition.y = -clipPosition.y;
    clipPosition.z =
        (clipPosition.z + clipPosition.w) * 0.5f;
    WebglGeometryExtrudeSplinesVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewNormal = viewNormal;
    outputValue.entityID = renderEntityID;
    outputValue.phase = phase;
    outputValue.linePixels = linePixels;
    outputValue.lineInstanceID = renderEntityInstanceID;
    return outputValue;
}

/** Draws the opaque magenta TubeGeometry through the Scene's unique Set. */
class WebglGeometryExtrudeSplinesOpaquePass final : public IRenderClass
{
public:
    /** Configures the front-sided Lambert tube surface. */
    constructor(
        RenderSet<WebglGeometryExtrudeSplinesSceneRenderSet>
            sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves tube entity and instance components through RenderSet builtins. */
    WebglGeometryExtrudeSplinesVertexOutput vertex(
        WebglGeometryExtrudeSplinesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryExtrudeSplinesTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
    }

    /** Evaluates the exact ambient and directional Lambert material. */
    WebglGeometryExtrudeSplinesFrameBuffer fragment(
        WebglGeometryExtrudeSplinesVertexOutput inputValue)
    {
        if (inputValue.phase > 0.5f)
        {
            discard_fragment();
        }
        const WebglGeometryExtrudeSplinesObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglGeometryExtrudeSplinesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float diffuse =
            objectData.ambientAndDirectionalIntensity.x +
            objectData.ambientAndDirectionalIntensity.y *
                max(
                    dot(
                        normalize(inputValue.viewNormal),
                        normalize(
                            float3(
                                objectData.directionalView.xyz))),
                    0.0f);
        const float3 linearColor =
            materialData.colorAndOpacity.xyz *
            diffuse *
            0.3183098861837907f;
        WebglGeometryExtrudeSplinesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryExtrudeSplinesLinearToSrgb(linearColor.x)),
            half(webglGeometryExtrudeSplinesLinearToSrgb(linearColor.y)),
            half(webglGeometryExtrudeSplinesLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent black GL_LINES-compatible wireframe through the same Set. */
class WebglGeometryExtrudeSplinesTransparentWireframePass final
    : public IRenderClass
{
public:
    /** Configures source-alpha blending without changing opaque tube depth. */
    constructor(
        RenderSet<WebglGeometryExtrudeSplinesSceneRenderSet>
            sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        // The packed wireframe entity is emitted as endpoint pairs and uses
        // the existing native LineList rasterization path.
        setPrimitiveTopology(PrimitiveTopology::LineList);
        // MeshBasicMaterial keeps its default depthWrite=true even when
        // transparent. Preserve that ordering for coincident TubeGeometry
        // edges instead of allowing hidden edges to blend through the front.
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Projects only the wireframe entity through RenderSet entity metadata. */
    WebglGeometryExtrudeSplinesVertexOutput vertex(
        WebglGeometryExtrudeSplinesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryExtrudeSplinesTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
    }

    /** Emits only the quarter-opacity wireframe material. */
    WebglGeometryExtrudeSplinesFrameBuffer fragment(
        WebglGeometryExtrudeSplinesVertexOutput inputValue)
    {
        if (inputValue.phase < 0.5f || inputValue.phase > 1.5f)
        {
            discard_fragment();
        }
        // Native LineList rasterization provides the one-pixel coverage for
        // this RenderSet entity. Keep the fragment branch free of an
        // additional analytic test so endpoint ownership stays backend
        // deterministic.
        const WebglGeometryExtrudeSplinesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglGeometryExtrudeSplinesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(materialData.colorAndOpacity.x),
            half(materialData.colorAndOpacity.y),
            half(materialData.colorAndOpacity.z),
            half(materialData.colorAndOpacity.w));
        return frameBuffer;
    }
};

/** Owns one four-entity Scene Set and the two declared spline Scene passes. */
class WebglGeometryExtrudeSplinesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryExtrudeSplinesSceneRenderSet>
        sceneSet;
    RenderClass<WebglGeometryExtrudeSplinesOpaquePass>
        opaquePass;
    RenderClass<WebglGeometryExtrudeSplinesTransparentWireframePass>
        wireframePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the Scene's unique Set and two dedicated Scene RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglGeometryExtrudeSplinesSceneRenderSet>();
        opaquePass =
            device->createRenderClass<
                WebglGeometryExtrudeSplinesOpaquePass>(
                    sceneSet);
        wireframePass =
            device->createRenderClass<
                WebglGeometryExtrudeSplinesTransparentWireframePass>(
                    sceneSet);
    }

    /** Allocates ordinary single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor =
            device->createTexture(
                "WebglGeometryExtrudeSplinesColor",
                width,
                height,
                1u);
        sceneDepth =
            device->createTexture(
                "WebglGeometryExtrudeSplinesDepth",
                width,
                height,
                1u);
    }

    /** Updates one Set and submits opaque then transparent Scene passes. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryExtrudeSplinesFrameBuffer opaqueFrame;
        opaqueFrame.color = sceneColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue =
            {0.9411764706f, 0.9411764706f, 0.9411764706f, 1.0f};
        opaqueFrame.depth = sceneDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        WebglGeometryExtrudeSplinesFrameBuffer wireframeFrame;
        wireframeFrame.color = sceneColor->createView();
        wireframeFrame.color.loadOp = LoadOp::Load;
        wireframeFrame.color.storeOp = StoreOp::Store;
        wireframeFrame.depth = sceneDepth->createView();
        wireframeFrame.depth.depthLoadOp = LoadOp::Load;
        wireframeFrame.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture =
            swapchain->queryNextTexture();
            graphicsQueue
            ->renderPass(
                "WebglGeometryExtrudeSplinesOpaque",
                opaqueFrame,
                opaquePass())
            ->renderPass(
                "WebglGeometryExtrudeSplinesWireframe",
                wireframeFrame,
                wireframePass())
            ->renderToSwapchain(
                swapchainTexture,
                sceneColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 target. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return sceneColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the unique Scene Set and both attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }
};

#endif
