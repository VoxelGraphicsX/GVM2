#ifndef GVM_THREE_WEBGL_GEOMETRY_NURBS_HPP
#define GVM_THREE_WEBGL_GEOMETRY_NURBS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometryNurbsMaxTextures = 8u;

/** Stores the attribute union for NURBS surfaces and expanded line segments. */
struct WebglGeometryNurbsVertex
{
    float4 position [[Attribute0]];
    float4 normalOrLineEnd [[Attribute1]];
    float4 uvAndLineData [[Attribute2]];
};

/** Stores one entity transform, projection, viewport, and lighting state. */
struct WebglGeometryNurbsObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
    float4 ambientAndDirectionalIntensity;
    float4 directionalView;
};

/** Stores the mandatory non-instanced component entry. */
struct WebglGeometryNurbsInstanceData
{
    float4 reserved;
};

/** Selects textured surface, opaque curve, or transparent control-line shading. */
struct WebglGeometryNurbsMaterialData
{
    float4 colorAndOpacity;
    float4 phaseAndReserved;
};

/** Defines the unique RenderSet owned by the NURBS example Scene. */
struct WebglGeometryNurbsSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, entity data, materials, and UV-grid textures. */
    constructor(
        BufferComponent<WebglGeometryNurbsVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryNurbsObjectData> objects,
        BufferComponent<WebglGeometryNurbsInstanceData> instances,
        BufferComponent<WebglGeometryNurbsMaterialData> materials,
        (TextureComponent<half4, WebglGeometryNurbsMaxTextures> textures))
    {
    }
};

/** Binds the repeat sampler used by all six textured surface entities. */
struct WebglGeometryNurbsSamplerResources final : public IBindGroup
{
    /** Declares the one immutable trilinear repeat sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries surface and expanded-line data into the two Scene fragment stages. */
struct WebglGeometryNurbsVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
    float phase [[Attribute4]];
};

/** Defines the ordinary single-sample NURBS Scene attachments. */
struct WebglGeometryNurbsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear-light channel for the browser canvas. */
float webglGeometryNurbsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Transforms surfaces directly and expands one-pixel line segments in screen space. */
WebglGeometryNurbsVertexOutput webglGeometryNurbsTransformVertex(
    IN RenderSet<WebglGeometryNurbsSceneRenderSet> sceneSet,
    WebglGeometryNurbsVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglGeometryNurbsObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGeometryNurbsMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const WebglGeometryNurbsInstanceData instanceData =
        sceneSet->instances->get(
            renderEntityID,
            renderEntityInstanceID);
    const float phase = materialData.phaseAndReserved.x;
    const float4 localPosition =
        inputValue.position +
        float4(instanceData.reserved.xyz, 0.0f);
    float4 viewPosition =
        mul(objectData.modelView, localPosition);
    float4 clipPosition =
        mul(objectData.projection, viewPosition);
    float3 viewNormal =
        normalize(
            float3(
                mul(
                    objectData.modelView,
                    float4(inputValue.normalOrLineEnd.xyz, 0.0f))
                    .xyz));

    if (phase > 0.5f)
    {
        const float4 lineEndView =
            mul(
                objectData.modelView,
                inputValue.normalOrLineEnd);
        const float4 lineEndClip =
            mul(objectData.projection, lineEndView);
        const float2 startNdc =
            clipPosition.xy / clipPosition.w;
        const float2 endNdc =
            lineEndClip.xy / lineEndClip.w;
        const float2 pixelDirection =
            (endNdc - startNdc) *
            objectData.viewport.xy;
        const float inverseLength =
            1.0f / max(length(pixelDirection), 0.0001f);
        const float2 pixelNormal =
            float2(
                -pixelDirection.y,
                pixelDirection.x) *
            inverseLength;
        const bool useEnd =
            inputValue.uvAndLineData.z > 0.5f;
        clipPosition =
            useEnd ? lineEndClip : clipPosition;
        viewPosition =
            useEnd ? lineEndView : viewPosition;
        clipPosition.xy +=
            pixelNormal *
            inputValue.uvAndLineData.w /
            objectData.viewport.xy *
            clipPosition.w;
        viewNormal = float3(0.0f);
    }
    clipPosition.y = -clipPosition.y;
    clipPosition.z =
        (clipPosition.z + clipPosition.w) * 0.5f;

    WebglGeometryNurbsVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewPosition.xyz;
    outputValue.viewNormal = viewNormal;
    outputValue.uv = inputValue.uvAndLineData.xy;
    outputValue.entityID = renderEntityID;
    outputValue.phase = phase;
    return outputValue;
}

/** Draws all opaque NURBS surfaces and the primary curve through one Set. */
class WebglGeometryNurbsOpaquePass final : public IRenderClass
{
public:
    /** Configures the ordinary opaque Scene depth contract. */
    constructor(
        RenderSet<WebglGeometryNurbsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryNurbsSamplerResources>
            samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies entity transforms and line expansion through shared DSL math. */
    WebglGeometryNurbsVertexOutput vertex(
        WebglGeometryNurbsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryNurbsTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
    }

    /** Evaluates opaque line color or double-sided Lambert texture lighting. */
    WebglGeometryNurbsFrameBuffer fragment(
        WebglGeometryNurbsVertexOutput inputValue)
    {
        if (inputValue.phase > 1.5f)
        {
            discard_fragment();
        }
        const WebglGeometryNurbsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglGeometryNurbsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        float3 linearColor =
            materialData.colorAndOpacity.xyz;
        if (inputValue.phase < 0.5f)
        {
            const half4 texel =
                sceneSet->textures
                    ->get(inputValue.entityID, 0u)
                    ->sample(
                        samplerResources->textureSampler,
                        inputValue.uv);
            const float3 textureColor =
                float3(texel.xyz);
            const float3 normal =
                normalize(inputValue.viewNormal);
            const float diffuse =
                objectData.ambientAndDirectionalIntensity.x +
                objectData.ambientAndDirectionalIntensity.y *
                    max(
                        dot(
                            normal,
                            normalize(
                                float3(
                                    objectData.directionalView.xyz))),
                        0.0f);
            linearColor *=
                textureColor *
                diffuse *
                0.3183098861837907f;
        }
        WebglGeometryNurbsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryNurbsLinearToSrgb(linearColor.x)),
            half(webglGeometryNurbsLinearToSrgb(linearColor.y)),
            half(webglGeometryNurbsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws only the transparent control polygon through the same Scene Set. */
class WebglGeometryNurbsTransparentControlLinePass final
    : public IRenderClass
{
public:
    /** Configures source-alpha blending and preserves opaque Scene depth. */
    constructor(
        RenderSet<WebglGeometryNurbsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
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
    /** Applies the same deterministic one-pixel line expansion. */
    WebglGeometryNurbsVertexOutput vertex(
        WebglGeometryNurbsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryNurbsTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID);
    }

    /** Emits only the quarter-opacity control-line entity. */
    WebglGeometryNurbsFrameBuffer fragment(
        WebglGeometryNurbsVertexOutput inputValue)
    {
        if (inputValue.phase < 1.5f)
        {
            discard_fragment();
        }
        const WebglGeometryNurbsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglGeometryNurbsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryNurbsLinearToSrgb(
                materialData.colorAndOpacity.x)),
            half(webglGeometryNurbsLinearToSrgb(
                materialData.colorAndOpacity.y)),
            half(webglGeometryNurbsLinearToSrgb(
                materialData.colorAndOpacity.z)),
            half(materialData.colorAndOpacity.w));
        return frameBuffer;
    }
};

/** Owns the dedicated eight-entity NURBS Scene and two real Scene passes. */
class WebglGeometryNurbsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryNurbsSceneRenderSet> sceneSet;
    Sampler textureSampler;
    BindGroup<WebglGeometryNurbsSamplerResources> samplerResources;
    RenderClass<WebglGeometryNurbsOpaquePass> opaquePass;
    RenderClass<WebglGeometryNurbsTransparentControlLinePass>
        transparentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates one Scene RenderSet, one sampler, and the two declared passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglGeometryNurbsSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglGeometryNurbsTextureSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 16,
        });
        samplerResources =
            device->createBindGroup<WebglGeometryNurbsSamplerResources>(
                textureSampler);
        opaquePass =
            device->createRenderClass<WebglGeometryNurbsOpaquePass>(
                sceneSet,
                samplerResources);
        transparentPass =
            device->createRenderClass<
                WebglGeometryNurbsTransparentControlLinePass>(
                    sceneSet);
    }

    /** Allocates the ordinary single-sample color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor =
            device->createTexture(
                "WebglGeometryNurbsColor",
                width,
                height,
                1u);
        sceneDepth =
            device->createTexture(
                "WebglGeometryNurbsDepth",
                width,
                height,
                1u);
    }

    /** Updates one Set and submits the opaque then transparent Scene passes. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryNurbsFrameBuffer opaqueFrame;
        configureOpaqueFrame(opaqueFrame);
        WebglGeometryNurbsFrameBuffer transparentFrame;
        configureTransparentFrame(transparentFrame);
        auto swapchainTexture =
            swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometryNurbsOpaque",
                opaqueFrame,
                opaquePass())
            ->renderPass(
                "WebglGeometryNurbsTransparentControlLine",
                transparentFrame,
                transparentPass())
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

    /** Releases the unique Scene Set and both private attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }

private:
    /** Configures the first NURBS Scene pass to clear color and depth. */
    void configureOpaqueFrame(
        WebglGeometryNurbsFrameBuffer &frame)
    {
        frame.color = sceneColor->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue =
            {0.9411764706f, 0.9411764706f, 0.9411764706f, 1.0f};
        frame.depth = sceneDepth->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
    }

    /** Configures the transparent NURBS pass to preserve opaque color and depth. */
    void configureTransparentFrame(
        WebglGeometryNurbsFrameBuffer &frame)
    {
        frame.color = sceneColor->createView();
        frame.color.loadOp = LoadOp::Load;
        frame.color.storeOp = StoreOp::Store;
        frame.depth = sceneDepth->createView();
        frame.depth.depthLoadOp = LoadOp::Load;
        frame.depth.depthStoreOp = StoreOp::Store;
    }
};

#endif
