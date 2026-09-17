#ifndef GVM_THREE_WEBGL_GEOMETRY_CONVEX_HPP
#define GVM_THREE_WEBGL_GEOMETRY_CONVEX_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometryConvexMaxTextures = 8u;

/** Stores the union vertex layout for hull, axes, and billboard geometry. */
struct WebglGeometryConvexVertex
{
    float4 position [[Attribute0]];
    float4 normalOrEnd [[Attribute1]];
    float4 color [[Attribute2]];
    float4 auxiliary [[Attribute3]];
};

/** Stores one entity transform, camera projection, and high-resolution viewport. */
struct WebglGeometryConvexObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 viewport;
};

/** Stores the mandatory non-instanced component entry. */
struct WebglGeometryConvexInstanceData
{
    float4 reserved;
};

/** Selects point, axes, or transparent hull shading for one entity. */
struct WebglGeometryConvexMaterialData
{
    float4 colorAndPhase;
};

/** Defines the unique RenderSet owned by the convex example Scene. */
struct WebglGeometryConvexSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, components, and the fixed sprite slot. */
    constructor(
        BufferComponent<WebglGeometryConvexVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryConvexObjectData> objects,
        BufferComponent<WebglGeometryConvexInstanceData> instances,
        BufferComponent<WebglGeometryConvexMaterialData> materials,
        (TextureComponent<half4, WebglGeometryConvexMaxTextures> textures))
    {
    }
};

/** Binds the point-sprite sampler used by the RenderSet texture component. */
struct WebglGeometryConvexSamplerResources final : public IBindGroup
{
    /** Declares the immutable clamp sampler. */
    constructor(Sampler spriteSampler [[Binding0]])
    {
    }
};

/** Carries union material inputs and entity identity into fragment shading. */
struct WebglGeometryConvexVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    float3 viewNormal [[Attribute2]];
    float2 uv [[Attribute3]];
    uint entityID [[Attribute4]];
    float phase [[Attribute5]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglGeometryConvexSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear channel with the frozen Three r185 output transfer. */
float webglGeometryConvexLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws point billboards and expanded axes through the Scene RenderSet. */
class WebglGeometryConvexOpaquePass final : public IRenderClass
{
public:
    /** Binds the unique Set and opaque depth state. */
    constructor(
        RenderSet<WebglGeometryConvexSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryConvexSamplerResources>
            samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands points and lines while preserving entity metadata. */
    WebglGeometryConvexVertexOutput vertex(
        WebglGeometryConvexVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryConvexObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryConvexMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const WebglGeometryConvexInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float phase = materialData.colorAndPhase.w;
        float4 viewPosition =
            mul(
                objectData.modelView,
                inputValue.position +
                    float4(instanceData.reserved.xyz, 0.0f));
        float4 clipPosition =
            mul(objectData.projection, viewPosition);
        float2 uv = inputValue.auxiliary.xy;
        if (phase < 0.5f)
        {
            clipPosition.xy +=
                (inputValue.auxiliary.xy - float2(0.5f)) *
                float2(0.625f, 1.0f);
        }
        else if (phase < 1.5f)
        {
            const float4 startView =
                mul(objectData.modelView, inputValue.position);
            const float4 endView =
                mul(objectData.modelView, inputValue.normalOrEnd);
            const float4 startClip =
                mul(objectData.projection, startView);
            const float4 endClip =
                mul(objectData.projection, endView);
            const float2 startNdc =
                startClip.xy / startClip.w;
            const float2 endNdc =
                endClip.xy / endClip.w;
            const float2 pixelDirection =
                (endNdc - startNdc) *
                objectData.viewport.xy;
            const float2 pixelNormal =
                normalize(
                    float2(
                        -pixelDirection.y,
                        pixelDirection.x));
            const float diamondScale =
                max(abs(pixelNormal.x), abs(pixelNormal.y));
            const bool useEnd =
                inputValue.auxiliary.z > 0.5f;
            clipPosition = useEnd ? endClip : startClip;
            clipPosition.xy +=
                pixelNormal *
                inputValue.auxiliary.w /
                objectData.viewport.xy *
                diamondScale *
                clipPosition.w;
            viewPosition = useEnd ? endView : startView;
        }
        WebglGeometryConvexVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color =
            inputValue.color.xyz *
            materialData.colorAndPhase.xyz;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = inputValue.normalOrEnd.xyz;
        outputValue.uv = uv;
        outputValue.entityID = renderEntityID;
        outputValue.phase = phase;
        return outputValue;
    }

    /** Samples the disc or emits the per-axis display color. */
    WebglGeometryConvexSceneFrameBuffer fragment(
        WebglGeometryConvexVertexOutput inputValue)
    {
        if (inputValue.phase > 1.5f)
        {
            discard_fragment();
        }
        float4 color =
            float4(inputValue.color, 1.0f);
        if (inputValue.phase < 0.5f)
        {
            const half4 sprite =
                sceneSet->textures
                    ->get(inputValue.entityID, 0u)
                    ->sample(
                        samplerResources->spriteSampler,
                        inputValue.uv);
            if (sprite.w < half(0.5f))
            {
                discard_fragment();
            }
            color.w = float(sprite.w);
        }
        color.xyz = float3(
            webglGeometryConvexLinearToSrgb(color.x),
            webglGeometryConvexLinearToSrgb(color.y),
            webglGeometryConvexLinearToSrgb(color.z));
        WebglGeometryConvexSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Draws the native one-pixel AxesHelper segments through the same Scene Set. */
class WebglGeometryConvexAxisPass final : public IRenderClass
{
public:
    /** Configures the native single-sample line-list depth contract. */
    constructor(
        RenderSet<WebglGeometryConvexSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one native line endpoint and preserves its endpoint color. */
    WebglGeometryConvexVertexOutput vertex(
        WebglGeometryConvexVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryConvexObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryConvexMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const WebglGeometryConvexInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 viewPosition =
            mul(
                objectData.modelView,
                inputValue.position +
                    float4(instanceData.reserved.xyz, 0.0f));
        WebglGeometryConvexVertexOutput outputValue;
        outputValue.position =
            mul(objectData.projection, viewPosition);
        outputValue.color =
            inputValue.color.xyz *
            materialData.colorAndPhase.xyz;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = float3(0.0f);
        outputValue.uv = float2(0.0f);
        outputValue.entityID = renderEntityID;
        outputValue.phase = materialData.colorAndPhase.w;
        return outputValue;
    }

    /** Emits only the axis entity with Three's output transfer. */
    WebglGeometryConvexSceneFrameBuffer fragment(
        WebglGeometryConvexVertexOutput inputValue)
    {
        if (inputValue.phase < 0.5f ||
            inputValue.phase > 1.5f)
        {
            discard_fragment();
        }
        WebglGeometryConvexSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryConvexLinearToSrgb(inputValue.color.x)),
            half(webglGeometryConvexLinearToSrgb(inputValue.color.y)),
            half(webglGeometryConvexLinearToSrgb(inputValue.color.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the semi-transparent double-sided convex hull through the same Set. */
class WebglGeometryConvexTransparentHullPass final : public IRenderClass
{
public:
    /** Configures source-alpha blending without changing Scene ownership. */
    constructor(
        RenderSet<WebglGeometryConvexSceneRenderSet> sceneSet [[Slot0]])
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
    /** Transforms hull vertices while reading the non-instanced component. */
    WebglGeometryConvexVertexOutput vertex(
        WebglGeometryConvexVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryConvexObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryConvexMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const WebglGeometryConvexInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 localPosition =
            inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        const float4 viewPosition =
            mul(objectData.modelView, localPosition);
        const float4 viewNormal =
            mul(
                objectData.modelView,
                float4(inputValue.normalOrEnd.xyz, 0.0f));
        WebglGeometryConvexVertexOutput outputValue;
        outputValue.position =
            mul(objectData.projection, viewPosition);
        outputValue.color =
            inputValue.color.xyz *
            materialData.colorAndPhase.xyz;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal =
            normalize(
                float3(
                    viewNormal.x,
                    viewNormal.y,
                    viewNormal.z));
        outputValue.uv = float2(0.0f);
        outputValue.entityID = renderEntityID;
        outputValue.phase = materialData.colorAndPhase.w;
        return outputValue;
    }

    /** Evaluates ambient and camera-attached point Lambert lighting. */
    WebglGeometryConvexSceneFrameBuffer fragment(
        WebglGeometryConvexVertexOutput inputValue)
    {
        if (inputValue.phase < 1.5f)
        {
            discard_fragment();
        }
        const float3 normal =
            normalize(inputValue.viewNormal);
        const float3 lightDirection =
            normalize(-inputValue.viewPosition);
        const float pointIrradiance =
            abs(dot(normal, lightDirection)) *
            3.0f;
        const float3 linearColor =
            inputValue.color *
            (float3(0.132868f * 0.3183098862f) +
             float3(pointIrradiance * 0.3183098862f));
        WebglGeometryConvexSceneFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(
                half3(
                    webglGeometryConvexLinearToSrgb(linearColor.x),
                    webglGeometryConvexLinearToSrgb(linearColor.y),
                    webglGeometryConvexLinearToSrgb(linearColor.z)),
                half(0.5f));
        return frameBuffer;
    }
};

/** Owns the convex Scene's unique Set and its two geometry phases. */
class WebglGeometryConvexRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]]
    RenderSet<WebglGeometryConvexSceneRenderSet> sceneSet;
    Sampler spriteSampler;
    BindGroup<WebglGeometryConvexSamplerResources>
        samplerResources;
    RenderClass<WebglGeometryConvexOpaquePass> opaquePass;
    RenderClass<WebglGeometryConvexAxisPass> axisPass;
    RenderClass<WebglGeometryConvexTransparentHullPass>
        transparentHullPass;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    uint outputWidth = 800u;
    uint outputHeight = 500u;

public:
    /** Creates the unique RenderSet and all immutable pass state. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglGeometryConvexSceneRenderSet>();
        spriteSampler = device->createSampler({
            .label = "WebglGeometryConvexSpriteSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        samplerResources =
            device->createBindGroup<
                WebglGeometryConvexSamplerResources>(
                spriteSampler);
        opaquePass =
            device->createRenderClass<
                WebglGeometryConvexOpaquePass>(
                sceneSet,
                samplerResources);
        axisPass =
            device->createRenderClass<
                WebglGeometryConvexAxisPass>(
                sceneSet);
        transparentHullPass =
            device->createRenderClass<
                WebglGeometryConvexTransparentHullPass>(
                sceneSet);
    }

    /** Allocates the host-sized single-sample Scene targets. */
    void configureOutput(uint width, uint height)
    {
        outputWidth = width;
        outputHeight = height;
        sceneDepth = device->createTexture(
            "WebglGeometryConvexSceneDepth",
            width,
            height,
            1u);
        outputColor = device->createTexture(
            "WebglGeometryConvexOutput",
            width,
            height,
            1u);
    }

    /** Draws both Scene phases directly into the single-sample output. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryConvexSceneFrameBuffer opaqueFrameBuffer;
        opaqueFrameBuffer.color =
            outputColor->createView();
        opaqueFrameBuffer.color.loadOp = LoadOp::Clear;
        opaqueFrameBuffer.color.storeOp = StoreOp::Store;
        opaqueFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        opaqueFrameBuffer.depth =
            sceneDepth->createView();
        opaqueFrameBuffer.depth.depthLoadOp =
            LoadOp::Clear;
        opaqueFrameBuffer.depth.depthStoreOp =
            StoreOp::Store;
        opaqueFrameBuffer.depth.depthClearValue = 1.0f;
        WebglGeometryConvexSceneFrameBuffer hullFrameBuffer;
        hullFrameBuffer.color =
            outputColor->createView();
        hullFrameBuffer.color.loadOp = LoadOp::Load;
        hullFrameBuffer.color.storeOp = StoreOp::Store;
        hullFrameBuffer.depth =
            sceneDepth->createView();
        hullFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        hullFrameBuffer.depth.depthStoreOp =
            StoreOp::Store;
        auto nextTexture =
            swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometryConvexOpaque",
                opaqueFrameBuffer,
                opaquePass())
            ->renderPass(
                "WebglGeometryConvexAxis",
                hullFrameBuffer,
                axisPass())
            ->renderPass(
                "WebglGeometryConvexTransparentHull",
                hullFrameBuffer,
                transparentHullPass())
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return outputWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return outputHeight;
    }

    /** Releases all explicit DSL resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
