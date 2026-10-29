#ifndef GVM_THREE_WEBGL_CLIPPING_HPP
#define GVM_THREE_WEBGL_CLIPPING_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglClippingShadowMapSize = 1024u;

/** Stores the position and smooth normal of one clipping-scene vertex. */
struct WebglClippingVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores camera, light, fog, and shadow-view transforms for one entity. */
struct WebglClippingObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 shadowModelViewProjection;
    float4 spotDirectionAndIntensity;
    float4 directionalDirectionAndIntensity;
    float4 ambientAndFogNear;
    float4 fogColorAndFar;
};

/** Stores the mandatory one-entry instance record for a non-instanced object. */
struct WebglClippingInstanceData
{
    float4 reserved;
};

/** Stores the base color and side/shadow flags of one material. */
struct WebglClippingMaterialData
{
    float4 baseColorAndFlags;
};

/** Stores local and view-space global clipping planes for one entity. */
struct WebglClippingPlaneData
{
    float4 localPlane;
    float4 globalPlaneView;
    float4 enableAndReserved;
};

/** Stores the render phase, sidedness, and shadow participation flags. */
struct WebglClippingRenderFlagsData
{
    float4 phaseAndFlags;
};

/** Owns the torus knot and ground as the only RenderSet of the logical Scene. */
struct WebglClippingSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry and all private clipping components. */
    constructor(
        BufferComponent<WebglClippingVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglClippingObjectData> objects,
        BufferComponent<WebglClippingInstanceData> instances,
        BufferComponent<WebglClippingMaterialData> materials,
        BufferComponent<WebglClippingPlaneData> clipPlanes,
        BufferComponent<WebglClippingRenderFlagsData> renderFlags)
    {
    }
};

/** Binds the current-frame directional shadow depth map to the main passes. */
struct WebglClippingMainResources final : public IBindGroup
{
    /** Declares the depth texture sampled by the clipped lighting pass. */
    constructor(Texture2D<TextureFormat::Depth32Float> shadowMap [[Binding0]])
    {
    }
};

/** Carries view-space attributes, local coordinates, and entity identity. */
struct WebglClippingVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 localPosition [[Attribute1]];
    float3 viewNormal [[Attribute2]];
    uint entityID [[Attribute3]];
    float4 shadowClip [[Attribute4]];
};

/** Defines one ordinary single-sample color/depth target. */
struct WebglClippingFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel with the Three canvas sRGB transfer. */
float webglClippingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Applies the shared entity transform and forwards local clipping coordinates. */
WebglClippingVertexOutput webglClippingTransform(
    IN RenderSet<WebglClippingSceneRenderSet> sceneSet,
    WebglClippingVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID,
    bool flipNormal,
    bool shadowView)
{
    const WebglClippingObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglClippingInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 local = inputValue.position +
        float4(instanceData.reserved.xyz, 0.0f);
    const float4 view = mul(objectData.modelView, local);
    float4 clip = shadowView
        ? mul(objectData.shadowModelViewProjection, local)
        : mul(objectData.modelViewProjection, local);
    clip.y = -clip.y;
    clip.z = (clip.z + clip.w) * 0.5f;
    WebglClippingVertexOutput outputValue;
    outputValue.position = clip;
    outputValue.viewPosition = view.xyz;
    outputValue.localPosition = local.xyz;
    const float3 transformedNormal = float3(
        mul(objectData.modelView, float4(inputValue.normal.xyz, 0.0f)).xyz);
    outputValue.viewNormal = flipNormal
        ? -normalize(transformedNormal)
        : normalize(transformedNormal);
    outputValue.entityID = renderEntityID;
    float4 shadowClip = mul(objectData.shadowModelViewProjection, local);
    shadowClip.y = -shadowClip.y;
    shadowClip.z = (shadowClip.z + shadowClip.w) * 0.5f;
    outputValue.shadowClip = shadowClip;
    return outputValue;
}

/** Reads one clamped depth texel from the directional shadow map. */
float webglClippingReadShadow(
    IN BindGroup<WebglClippingMainResources> resources,
    uint2 texel)
{
    return resources->shadowMap->read(texel).x;
}

/** Performs a deterministic four-tap LessEqual shadow comparison. */
float webglClippingShadowCompare(
    IN BindGroup<WebglClippingMainResources> resources,
    float2 uv,
    float compareDepth)
{
    const float2 position =
        uv * float(WebglClippingShadowMapSize) - 0.5f;
    const float2 lower = floor(position);
    const float2 fraction = position - lower;
    const uint x0 = uint(clamp(
        lower.x, 0.0f, float(WebglClippingShadowMapSize - 1u)));
    const uint y0 = uint(clamp(
        lower.y, 0.0f, float(WebglClippingShadowMapSize - 1u)));
    const uint x1 = min(x0 + 1u, WebglClippingShadowMapSize - 1u);
    const uint y1 = min(y0 + 1u, WebglClippingShadowMapSize - 1u);
    const float a = compareDepth <= webglClippingReadShadow(
        resources, uint2(x0, y0)) ? 1.0f : 0.0f;
    const float b = compareDepth <= webglClippingReadShadow(
        resources, uint2(x1, y0)) ? 1.0f : 0.0f;
    const float c = compareDepth <= webglClippingReadShadow(
        resources, uint2(x0, y1)) ? 1.0f : 0.0f;
    const float d = compareDepth <= webglClippingReadShadow(
        resources, uint2(x1, y1)) ? 1.0f : 0.0f;
    return lerp(
        lerp(a, b, fraction.x),
        lerp(c, d, fraction.x),
        fraction.y);
}

/** Evaluates the receiver shadow factor for the ground entity. */
float webglClippingShadow(
    IN BindGroup<WebglClippingMainResources> resources,
    WebglClippingVertexOutput inputValue)
{
    if (inputValue.entityID != 1u)
        return 1.0f;
    const float3 coordinate = float3(
        inputValue.shadowClip.xy / inputValue.shadowClip.w * 0.5f +
            float2(0.5f),
        inputValue.shadowClip.z / inputValue.shadowClip.w);
    if (coordinate.x < 0.0f || coordinate.x > 1.0f ||
        coordinate.y < 0.0f || coordinate.y > 1.0f ||
        coordinate.z < 0.0f || coordinate.z > 1.0f)
        return 1.0f;
    return webglClippingShadowCompare(
        resources, coordinate.xy, coordinate.z - 0.0005f);
}

/** Returns whether a fragment lies outside an active clipping half-space. */
bool webglClippingOutsidePlanes(
    IN RenderSet<WebglClippingSceneRenderSet> sceneSet,
    WebglClippingVertexOutput inputValue,
    bool shadowPass)
{
    const WebglClippingPlaneData planes =
        sceneSet->clipPlanes->get(inputValue.entityID, 0u);
    const bool localEnabled = planes.enableAndReserved.x > 0.5f;
    const bool globalEnabled = planes.enableAndReserved.y > 0.5f;
    const bool shadowClipping = planes.enableAndReserved.z > 0.5f;
    if (shadowPass && !shadowClipping)
        return false;
    // The host transforms local material planes into view space, matching the
    // coordinate system used by the interpolated view position and Three.js'
    // clipping implementation.
    const bool localOutside = localEnabled &&
        dot(float3(planes.localPlane.xyz), inputValue.viewPosition) +
            planes.localPlane.w < 0.0f;
    const bool globalOutside = globalEnabled &&
        dot(float3(planes.globalPlaneView.xyz), inputValue.viewPosition) +
            planes.globalPlaneView.w < 0.0f;
    return localOutside || globalOutside;
}

/** Shades one clipped fragment with the frozen ambient/Phong light pair. */
float3 webglClippingShadeColor(
    IN BindGroup<WebglClippingMainResources> resources,
    IN RenderSet<WebglClippingSceneRenderSet> sceneSet,
    WebglClippingVertexOutput inputValue)
{
    const WebglClippingObjectData objectData =
        sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglClippingMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 normal = normalize(inputValue.viewNormal);
    const float3 viewDirection = normalize(-inputValue.viewPosition);
    const float3 spotPosition =
        objectData.spotDirectionAndIntensity.xyz;
    const float3 spotVector = spotPosition - inputValue.viewPosition;
    const float spotDistance = max(length(spotVector), 0.0001f);
    const float3 spotDirectionToFragment = spotVector / spotDistance;
    const float spotConeCos = objectData.ambientAndFogNear.w;
    const float spotPenumbraCos = objectData.fogColorAndFar.w;
    const float3 spotDirection = objectData.fogColorAndFar.xyz;
    const float spotAngleCos = dot(
        spotDirectionToFragment, normalize(spotDirection));
    const float spotCone = smoothstep(
        spotConeCos, spotPenumbraCos, spotAngleCos);
    const float spotDistanceFalloff = 1.0f /
        max(spotDistance * spotDistance, 0.01f);
    const float spotIrradiance = objectData.spotDirectionAndIntensity.w *
        spotCone * spotDistanceFalloff;
    const float3 directionalDirection = normalize(
        float3(objectData.directionalDirectionAndIntensity.xyz));
    const float directionalIrradiance =
        objectData.directionalDirectionAndIntensity.w;
    const float3 directionalColor = float3(
        0.272525132f, 0.24065946f, 0.306725204f);
    const float3 ambientLightColor =
        float3(objectData.ambientAndFogNear.xyz);
    const float3 diffuseColor = materialData.baseColorAndFlags.xyz;
    const float inversePi = 0.3183098861837907f;
    const float shadowFactor = webglClippingShadow(resources, inputValue);
    const float spotDiffuse = max(
        dot(normal, spotDirectionToFragment), 0.0f) * spotIrradiance;
    const float directionalDiffuse = max(
        dot(normal, directionalDirection), 0.0f) * directionalIrradiance;
    const float3 spotHalfDirection = normalize(
        spotDirectionToFragment + viewDirection);
    const float spotSpecular = pow(max(
        dot(normal, spotHalfDirection), 0.0f), 100.0f) * 0.25f;
    const float3 directionalHalfDirection = normalize(
        directionalDirection + viewDirection);
    const float directionalSpecular = pow(max(
        dot(normal, directionalHalfDirection), 0.0f), 100.0f) * 0.25f;
    const float3 linearColor = diffuseColor * (
        ambientLightColor +
        float3(spotDiffuse * shadowFactor * inversePi) +
        directionalColor * directionalDiffuse * shadowFactor * inversePi) +
        (float3(spotSpecular * spotIrradiance) +
         directionalColor * directionalSpecular * directionalIrradiance) *
        shadowFactor * 0.25f;
    const float3 srgb = float3(
        webglClippingLinearToSrgb(linearColor.x),
        webglClippingLinearToSrgb(linearColor.y),
        webglClippingLinearToSrgb(linearColor.z));
    return srgb;
}

/** Executes the shared clipped depth pass before the visible lighting passes. */
class WebglClippingShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the sole Scene Set and writes deterministic shadow depth. */
    constructor(RenderSet<WebglClippingSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms every entity into the shadow camera and applies clip planes. */
    WebglClippingVertexOutput vertex(
        WebglClippingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglClippingTransform(sceneSet, inputValue,
            renderEntityID, renderEntityInstanceID, false, true);
    }

    /** Keeps shadow color neutral while preserving depth writes. */
    WebglClippingFrameBuffer fragment(WebglClippingVertexOutput inputValue)
    {
        if (webglClippingOutsidePlanes(sceneSet, inputValue, true))
            discard_fragment();
        WebglClippingFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the front-facing half of the DoubleSide torus and the ground. */
class WebglClippingFrontPass final : public IRenderClass
{
public:
    /** Configures the first visible pass with depth writes and no MSAA. */
    constructor(
        RenderSet<WebglClippingSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglClippingMainResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms visible geometry while preserving authored normals. */
    WebglClippingVertexOutput vertex(
        WebglClippingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglClippingTransform(sceneSet, inputValue,
            renderEntityID, renderEntityInstanceID, false, false);
    }

    /** Applies half-space clipping, material sidedness, and Phong lighting. */
    WebglClippingFrameBuffer fragment(WebglClippingVertexOutput inputValue)
    {
        if (webglClippingOutsidePlanes(sceneSet, inputValue, false))
            discard_fragment();
        const WebglClippingRenderFlagsData flags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        if (flags.phaseAndFlags.x > 0.5f)
            discard_fragment();
        const float3 outputColor = webglClippingShadeColor(resources, sceneSet, inputValue);
        WebglClippingFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(outputColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the back-facing half of the DoubleSide torus from the same Set. */
class WebglClippingBackPass final : public IRenderClass
{
public:
    /** Configures the second visible pass and loads prior color/depth. */
    constructor(
        RenderSet<WebglClippingSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglClippingMainResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms geometry and flips normals for back-facing fragments. */
    WebglClippingVertexOutput vertex(
        WebglClippingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglClippingTransform(sceneSet, inputValue,
            renderEntityID, renderEntityInstanceID, true, false);
    }

    /** Rejects single-sided ground back faces and shades the remainder. */
    WebglClippingFrameBuffer fragment(WebglClippingVertexOutput inputValue)
    {
        if (webglClippingOutsidePlanes(sceneSet, inputValue, false))
            discard_fragment();
        const WebglClippingRenderFlagsData flags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        if (flags.phaseAndFlags.x > 0.5f || flags.phaseAndFlags.y < 0.5f)
            discard_fragment();
        const float3 outputColor = webglClippingShadeColor(resources, sceneSet, inputValue);
        WebglClippingFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(outputColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and the three ordered clipping passes. */
class WebglClippingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglClippingSceneRenderSet> sceneSet;
    RenderClass<WebglClippingShadowDepthPass> shadowDepthPass;
    RenderClass<WebglClippingFrontPass> frontPass;
    RenderClass<WebglClippingBackPass> backPass;
    BindGroup<WebglClippingMainResources> mainResources;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> shadowColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> shadowDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the one RenderSet and all generated scene passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglClippingSceneRenderSet>();
        shadowDepthPass = device->createRenderClass<WebglClippingShadowDepthPass>(sceneSet);
    }

    /** Allocates fixed single-sample visible and shadow attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglClippingColor", width, height, 1u);
        outputDepth = device->createTexture("WebglClippingDepth", width, height, 1u);
        shadowColor = device->createTexture(
            "WebglClippingShadowColor",
            WebglClippingShadowMapSize,
            WebglClippingShadowMapSize,
            1u);
        shadowDepth = device->createTexture(
            "WebglClippingShadowDepth",
            WebglClippingShadowMapSize,
            WebglClippingShadowMapSize,
            1u);
        mainResources = device->createBindGroup<
            WebglClippingMainResources>(shadowDepth->createView());
        frontPass = device->createRenderClass<
            WebglClippingFrontPass>(sceneSet, mainResources);
        backPass = device->createRenderClass<
            WebglClippingBackPass>(sceneSet, mainResources);
    }

    /** Executes shadow, front, and back passes while reusing one Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglClippingFrameBuffer shadowFrame;
        shadowFrame.color = shadowColor->createView();
        shadowFrame.color.loadOp = LoadOp::Clear;
        shadowFrame.color.storeOp = StoreOp::Store;
        shadowFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        shadowFrame.depth = shadowDepth->createView();
        shadowFrame.depth.depthLoadOp = LoadOp::Clear;
        shadowFrame.depth.depthStoreOp = StoreOp::Store;
        shadowFrame.depth.depthClearValue = 1.0f;
        WebglClippingFrameBuffer frontFrame;
        frontFrame.color = outputColor->createView();
        frontFrame.color.loadOp = LoadOp::Clear;
        frontFrame.color.storeOp = StoreOp::Store;
        frontFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frontFrame.depth = outputDepth->createView();
        frontFrame.depth.depthLoadOp = LoadOp::Clear;
        frontFrame.depth.depthStoreOp = StoreOp::Store;
        frontFrame.depth.depthClearValue = 1.0f;
        WebglClippingFrameBuffer backFrame;
        backFrame.color = outputColor->createView();
        backFrame.color.loadOp = LoadOp::Load;
        backFrame.color.storeOp = StoreOp::Store;
        backFrame.depth = outputDepth->createView();
        backFrame.depth.depthLoadOp = LoadOp::Load;
        backFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglClippingShadowDepth", shadowFrame, shadowDepthPass())
            ->renderPass("WebglClippingFront", frontFrame, frontPass())
            ->renderPass("WebglClippingBack", backFrame, backPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the visible RGBA8 target used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the sole Set and all visible/shadow attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
        device->freeTexture(shadowColor);
        device->freeTexture(shadowDepth);
    }
};

#endif
