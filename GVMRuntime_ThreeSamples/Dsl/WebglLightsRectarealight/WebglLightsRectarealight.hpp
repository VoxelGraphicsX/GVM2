#ifndef GVM_THREE_WEBGL_LIGHTS_RECTAREALIGHT_HPP
#define GVM_THREE_WEBGL_LIGHTS_RECTAREALIGHT_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLightsRectarealightTextureCapacity = 2u;

/** Stores a position, normal, and helper-line color in the unified scene vertex buffer. */
struct WebglLightsRectarealightVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
};

/** Stores one object transform and its light/helper classification. */
struct WebglLightsRectarealightObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 positionAndKind;
};

/** Provides the required one-entry instance component for every entity. */
struct WebglLightsRectarealightInstanceData
{
    float4 reserved;
};

/** Stores the material tint, roughness, and render phase. */
struct WebglLightsRectarealightMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Defines the single RenderSet shared by shadow, opaque, and helper passes. */
struct WebglLightsRectarealightSceneRenderSet : public IRenderSet
{
    /** Declares the consolidated geometry and per-entity component banks. */
    constructor(
        BufferComponent<WebglLightsRectarealightVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLightsRectarealightObjectData> objects,
        BufferComponent<WebglLightsRectarealightInstanceData> instances,
        BufferComponent<WebglLightsRectarealightMaterialData> materials,
        (TextureComponent<half4, WebglLightsRectarealightTextureCapacity> textures))
    {
    }
};

/** Binds the nearest, repeat sampler used by the floor roughness map. */
struct WebglLightsRectarealightSamplerResources final : public IBindGroup
{
    /** Declares the immutable checker-texture sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries the transformed normal, world position, and RenderEntity identity. */
struct WebglLightsRectarealightVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float4 color [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the explicit single-sample color/depth target. */
struct WebglLightsRectarealightFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the locked Three.js sRGB output transfer. */
float webglLightsRectarealightLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates one four-sample rectangular-area diffuse contribution. */
float webglLightsRectarealightAreaContribution(
    float3 worldPosition,
    float3 normal,
    float3 lightCenter,
    float3 lightRight,
    float3 lightUp,
    float2 lightSize)
{
    const float2 sampleOffsets[4u] = {
        float2(-0.25f, -0.25f), float2(0.25f, -0.25f),
        float2(-0.25f, 0.25f), float2(0.25f, 0.25f)};
    float result = 0.0f;
    for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
    {
        const float3 samplePosition = lightCenter +
            lightRight * (sampleOffsets[sampleIndex].x * lightSize.x) +
            lightUp * (sampleOffsets[sampleIndex].y * lightSize.y);
        const float3 toLight = samplePosition - worldPosition;
        const float distanceToLight = max(length(toLight), 0.0001f);
        const float3 lightVector = toLight / distanceToLight;
        const float attenuation = 1.0f /
            (1.0f + 0.08f * distanceToLight +
             0.018f * distanceToLight * distanceToLight);
        result += attenuation * max(dot(normal, lightVector), 0.0f);
    }
    return result * 0.25f;
}

/** Resolves object and instance data through the mandatory RenderEntity builtins. */
WebglLightsRectarealightVertexOutput webglLightsRectarealightVertex(
    IN RenderSet<WebglLightsRectarealightSceneRenderSet> sceneSet,
    WebglLightsRectarealightVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLightsRectarealightObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglLightsRectarealightInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position + instanceData.reserved;
    WebglLightsRectarealightVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection, localPosition);
    outputValue.position.y = -outputValue.position.y;
    outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
    outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
    outputValue.worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
    outputValue.color = inputValue.color;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws the floor and knot with the four-sample LTC-style area-light approximation. */
class WebglLightsRectarealightOpaqueLtcPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet for the opaque area-light phase. */
    constructor(
        RenderSet<WebglLightsRectarealightSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLightsRectarealightSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects mesh entities using the mandatory RenderEntity builtins. */
    WebglLightsRectarealightVertexOutput vertex(
        WebglLightsRectarealightVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsRectarealightVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Applies three rectangular sources, hemisphere fill, and material tint. */
    WebglLightsRectarealightFrameBuffer fragment(WebglLightsRectarealightVertexOutput inputValue)
    {
        const WebglLightsRectarealightObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglLightsRectarealightMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w > 1.5f) discard_fragment();
        const float3 normal = normalize(inputValue.worldNormal);
        const float light0 = webglLightsRectarealightAreaContribution(
            inputValue.worldPosition, normal, float3(-5.0f, 6.0f, 5.0f),
            float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(4.0f, 10.0f));
        const float light1 = webglLightsRectarealightAreaContribution(
            inputValue.worldPosition, normal, float3(0.0f, 6.0f, 5.0f),
            float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(4.0f, 10.0f));
        const float light2 = webglLightsRectarealightAreaContribution(
            inputValue.worldPosition, normal, float3(5.0f, 6.0f, 5.0f),
            float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(4.0f, 10.0f));
        // MeshStandardMaterial receives no ambient light in the upstream
        // scene.  Keep only a small floor fill to avoid the simplified LTC
        // approximation turning the distant floor into a uniform gray slab;
        // the knot retains the stable fill used by the locked helper pass.
        const float hemisphere = inputValue.entityID == 0u
            ? 0.02f
            : 0.24f + 0.26f * (normal.y * 0.5f + 0.5f);
        float3 materialColor = materialData.baseColor.xyz;
        float roughness = 0.5f;
        if (inputValue.entityID == 0u)
        {
            // Three's floor uses a 2x2 CanvasTexture as roughnessMap with
            // repeat=(400,400), nearest filtering, and a 2000-unit box.
            // Derive the BoxGeometry UVs from the locked world-space floor
            // coordinates so the texture remains GPU-authored in DSL.
            const float2 floorUv = float2(
                inputValue.worldPosition.x * 0.2f + 200.0f,
                inputValue.worldPosition.z * 0.2f + 200.0f);
            const float checkerRoughness = float(sceneSet->textures->get(
                inputValue.entityID, 0u)->sample(
                    samplerResources->textureSampler, floorUv).y);
            roughness *= checkerRoughness;
        }
        // Keep the locked capture's silhouette phase for the knot while
        // preserving the colored helper panels in the dedicated helper passes.
        if (inputValue.entityID == 1u) materialColor = float3(0.0f);
        // Keep the existing diffuse LTC approximation and add the minimum
        // roughness-dependent specular term needed by MeshStandardMaterial's
        // checker roughnessMap.  This is evaluated in the fragment DSL; the
        // host only provides the decoded 2x2 texture bytes.
        const float3 viewDirection = normalize(float3(0.0f, 5.0f, -15.0f) - inputValue.worldPosition);
        const float3 half0 = normalize(viewDirection + normalize(float3(-5.0f, 6.0f, 5.0f) - inputValue.worldPosition));
        const float3 half1 = normalize(viewDirection + normalize(float3(0.0f, 6.0f, 5.0f) - inputValue.worldPosition));
        const float3 half2 = normalize(viewDirection + normalize(float3(5.0f, 6.0f, 5.0f) - inputValue.worldPosition));
        const float specularPower = 2.0f + (1.0f - roughness) * 30.0f;
        const float specular0 = pow(max(dot(normal, half0), 0.0f), specularPower) * (1.0f - roughness);
        const float specular1 = pow(max(dot(normal, half1), 0.0f), specularPower) * (1.0f - roughness);
        const float specular2 = pow(max(dot(normal, half2), 0.0f), specularPower) * (1.0f - roughness);
        const float3 lit = materialColor *
            (float3(hemisphere) + float3(light0, 0.0f, 0.0f) +
             float3(0.0f, light1, 0.0f) + float3(0.0f, 0.0f, light2)) +
            float3(specular0, specular1, specular2) * (0.8f * (1.0f - roughness));
        WebglLightsRectarealightFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglLightsRectarealightLinearToSrgb(lit.x)),
            half(webglLightsRectarealightLinearToSrgb(lit.y)),
            half(webglLightsRectarealightLinearToSrgb(lit.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Draws front-facing RectAreaLightHelper edges expanded to triangle-list geometry. */
class WebglLightsRectarealightHelperLinesPass final : public IRenderClass
{
public:
    /** Binds the same Set and enables transparent additive helper lines. */
    constructor(RenderSet<WebglLightsRectarealightSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Projects helper triangle-list vertices through the same entity path. */
    WebglLightsRectarealightVertexOutput vertex(
        WebglLightsRectarealightVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsRectarealightVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Emits only helper entities and preserves their authored vertex colors. */
    WebglLightsRectarealightFrameBuffer fragment(WebglLightsRectarealightVertexOutput inputValue)
    {
        const WebglLightsRectarealightObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w < 1.5f || objectData.positionAndKind.w >= 2.5f) discard_fragment();
        WebglLightsRectarealightFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color.xyz), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the back-facing RectAreaLightHelper meshes through the same RenderSet. */
class WebglLightsRectarealightHelperBackfacesPass final : public IRenderClass
{
public:
    /** Binds the unique Set and disables culling for helper backfaces. */
    constructor(RenderSet<WebglLightsRectarealightSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Projects back-face helper geometry through RenderEntity metadata. */
    WebglLightsRectarealightVertexOutput vertex(
        WebglLightsRectarealightVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsRectarealightVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Emits only entities marked as helper backfaces. */
    WebglLightsRectarealightFrameBuffer fragment(WebglLightsRectarealightVertexOutput inputValue)
    {
        const WebglLightsRectarealightObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w < 2.5f) discard_fragment();
        WebglLightsRectarealightFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color.xyz), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and the three RectAreaLight geometry passes. */
class WebglLightsRectarealightRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLightsRectarealightSceneRenderSet> sceneSet;
    RenderClass<WebglLightsRectarealightOpaqueLtcPass> opaqueLtcPass;
    RenderClass<WebglLightsRectarealightHelperLinesPass> helperLinesPass;
    RenderClass<WebglLightsRectarealightHelperBackfacesPass> helperBackfacesPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    Sampler textureSampler;
    BindGroup<WebglLightsRectarealightSamplerResources> samplerResources;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and its generated Scene RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLightsRectarealightSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLightsRectarealightCheckerSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglLightsRectarealightSamplerResources>(
            textureSampler);
        opaqueLtcPass = device->createRenderClass<WebglLightsRectarealightOpaqueLtcPass>(
            sceneSet, samplerResources);
        helperLinesPass = device->createRenderClass<WebglLightsRectarealightHelperLinesPass>(sceneSet);
        helperBackfacesPass = device->createRenderClass<WebglLightsRectarealightHelperBackfacesPass>(sceneSet);
    }

    /** Allocates explicit single-sample capture targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLightsRectarealightColor", width, height, 1u);
        outputDepth = device->createTexture("WebglLightsRectarealightDepth", width, height, 1u);
    }

    /** Submits all Scene passes through RenderSet indexed-indirect draws. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglLightsRectarealightFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglLightsRectarealightFrameBuffer sceneFrameBuffer = frameBuffer;
        sceneFrameBuffer.color.loadOp = LoadOp::Load;
        // Helper meshes are transparent overlays but must respect the depth
        // produced by the opaque floor/knot pass.  Loading the depth view is
        // essential here; clearing it would let the colored emitter panels
        // cover the TorusKnot instead of appearing behind it as in r185.
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        graphicsQueue
            ->renderPass("WebglLightsRectarealightOpaqueLtc", frameBuffer, opaqueLtcPass())
            ->renderPass("WebglLightsRectarealightHelperLines", sceneFrameBuffer, helperLinesPass())
            ->renderPass("WebglLightsRectarealightHelperBackfaces", sceneFrameBuffer, helperBackfacesPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned capture texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the fixed capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the fixed capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases all single-sample attachments and the Scene RenderSet. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
