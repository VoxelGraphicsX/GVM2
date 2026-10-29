#ifndef GVM_THREE_WEBGL_LOADER_TEXTURE_KTX_HPP
#define GVM_THREE_WEBGL_LOADER_TEXTURE_KTX_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderTextureKtxMaxTextures = 16u;

/** Stores one canonical BoxGeometry vertex with flat face normal and UV. */
struct WebglLoaderTextureKtxVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one entity transform and fixed material phase. */
struct WebglLoaderTextureKtxObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    uint4 phaseAndFlags;
};

/** Stores the mandatory one-entry instance component. */
struct WebglLoaderTextureKtxInstanceData
{
    float4 reserved;
};

/** Stores the default Standard roughness and light intensities. */
struct WebglLoaderTextureKtxMaterialData
{
    float4 ambientPointRoughnessOpacity;
};

/** Defines the sole nine-entity KTX Scene RenderSet. */
struct WebglLoaderTextureKtxSceneRenderSet : public IRenderSet
{
    /** Declares packed BoxGeometry and one decoded texture slot per entity. */
    constructor(
        BufferComponent<WebglLoaderTextureKtxVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderTextureKtxObjectData> objects,
        BufferComponent<WebglLoaderTextureKtxInstanceData> instances,
        BufferComponent<WebglLoaderTextureKtxMaterialData> materials,
        (TextureComponent<half4, WebglLoaderTextureKtxMaxTextures> textures),
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Binds the authored-mip sampler shared by all KTX materials. */
struct WebglLoaderTextureKtxResources final : public IBindGroup
{
    /** Declares a linear trilinear repeat sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries entity-resolved material inputs to all fixed-state phases. */
struct WebglLoaderTextureKtxVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    uint2 entityAndPhase [[Attribute3]];
};

/** Defines the normal single-sample color/depth output. */
struct WebglLoaderTextureKtxFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Applies Three r185's output transfer to one linear channel. */
float webglLoaderTextureKtxLinearToSrgb(float value)
{
    return value <= 0.0031308f
        ? value * 12.92f
        : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglLoaderTextureKtxFresnel(float3 f0, float dotViewHalf)
{
    const float factor = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates the direct roughness-one GGX specular BRDF. */
float3 webglLoaderTextureKtxGgx(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal,
    float roughness)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNL = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNV = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotNH = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotVH = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = dotNH * dotNH * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared /
        max(3.141592653589793f * denominator * denominator, 0.000001f);
    const float visibility = 0.5f / max(
        dotNL * sqrt(alphaSquared + (1.0f - alphaSquared) * dotNV * dotNV) +
        dotNV * sqrt(alphaSquared + (1.0f - alphaSquared) * dotNL * dotNL),
        0.000001f);
    return webglLoaderTextureKtxFresnel(float3(0.04f), dotVH) *
        (visibility * distribution);
}

/** Resolves the current entity transform and component identity. */
WebglLoaderTextureKtxVertexOutput webglLoaderTextureKtxVertex(
    WebglLoaderTextureKtxVertex inputValue,
    IN RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLoaderTextureKtxObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglLoaderTextureKtxInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const uint4 renderFlags = sceneSet->renderFlags->get(renderEntityID, 0u);
    WebglLoaderTextureKtxVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection, inputValue.position);
    const float4 viewPosition = mul(objectData.modelView, inputValue.position);
    outputValue.viewPosition = viewPosition.xyz + instanceData.reserved.xyz;
    const float4 transformedNormal = mul(
        objectData.modelView,
        float4(inputValue.normal.xyz, 0.0f));
    outputValue.viewNormal = normalize(float3(
        transformedNormal.x, transformedNormal.y, transformedNormal.z));
    outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
    outputValue.entityAndPhase = uint2(renderEntityID, renderFlags.x);
    return outputValue;
}

/** Samples one entity-local texture using CompressedTexture flipY semantics. */
float4 webglLoaderTextureKtxSample(
    WebglLoaderTextureKtxVertexOutput inputValue,
    IN RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTextureKtxResources> resources)
{
    auto textureValue = sceneSet->textures->get(inputValue.entityAndPhase.x, 0u);
    return float4(textureValue->sample(
        resources->textureSampler,
        inputValue.textureCoordinate));
}

/** Draws the four opaque unlit compressed-color boxes. */
class WebglLoaderTextureKtxOpaquePass final : public IRenderClass
{
public:
    /** Configures the opaque back-cull phase on the unique Scene Set. */
    constructor(
        RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureKtxResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureKtxVertexOutput vertex(
        WebglLoaderTextureKtxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureKtxVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only opaque color-map entities. */
    WebglLoaderTextureKtxFrameBuffer fragment(
        WebglLoaderTextureKtxVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTextureKtxSample(inputValue, sceneSet, resources);
        WebglLoaderTextureKtxFrameBuffer outputValue;
        if (inputValue.entityAndPhase.y != 0u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureKtxLinearToSrgb(linearColor.x),
            webglLoaderTextureKtxLinearToSrgb(linearColor.y),
            webglLoaderTextureKtxLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Draws the two Standard-material normal-map boxes. */
class WebglLoaderTextureKtxNormalPass final : public IRenderClass
{
public:
    /** Configures the opaque normal-mapped phase on the unique Scene Set. */
    constructor(
        RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureKtxResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureKtxVertexOutput vertex(
        WebglLoaderTextureKtxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureKtxVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Reconstructs the tangent frame and evaluates default Standard lighting. */
    WebglLoaderTextureKtxFrameBuffer fragment(
        WebglLoaderTextureKtxVertexOutput inputValue)
    {
        const WebglLoaderTextureKtxMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const float2 sampledNormalXY =
            webglLoaderTextureKtxSample(inputValue, sceneSet, resources).xy * 2.0f - 1.0f;
        const float3 sampledNormal = float3(
            sampledNormalXY,
            sqrt(clamp(1.0f - dot(sampledNormalXY, sampledNormalXY), 0.0f, 1.0f)));
        const float3 geometricNormal = normalize(inputValue.viewNormal);
        const float3 q0 = ddx(-inputValue.viewPosition);
        const float3 q1 = ddy(-inputValue.viewPosition);
        const float2 st0 = ddx(inputValue.textureCoordinate);
        const float2 st1 = ddy(inputValue.textureCoordinate);
        const float3 q1Perpendicular = cross(q1, geometricNormal);
        const float3 q0Perpendicular = cross(geometricNormal, q0);
        const float3 tangent = q1Perpendicular * st0.x + q0Perpendicular * st1.x;
        const float3 bitangent = q1Perpendicular * st0.y + q0Perpendicular * st1.y;
        const float inverseScale = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
        const float3 normal = normalize(
            tangent * (-sampledNormal.x * inverseScale) +
            bitangent * (sampledNormal.y * inverseScale) +
            geometricNormal * sampledNormal.z);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection = normalize(
            float3(0.0f, 0.0f, -700.0f) - inputValue.viewPosition);
        const float dotNL = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
        const float ambient = material.ambientPointRoughnessOpacity.x;
        const float pointIntensity = material.ambientPointRoughnessOpacity.y;
        const float roughness = material.ambientPointRoughnessOpacity.z;
        const float3 linearColor = float3(1.0f) *
                (ambient + pointIntensity * dotNL) * 0.3183098861837907f +
            pointIntensity * dotNL * webglLoaderTextureKtxGgx(
                lightDirection, viewDirection, normal, roughness);
        WebglLoaderTextureKtxFrameBuffer outputValue;
        if (inputValue.entityAndPhase.y != 1u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureKtxLinearToSrgb(linearColor.x),
            webglLoaderTextureKtxLinearToSrgb(linearColor.y),
            webglLoaderTextureKtxLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Draws the three double-sided transparent flare boxes. */
class WebglLoaderTextureKtxTransparentPass final : public IRenderClass
{
public:
    /** Configures source-over blending with disabled depth semantics. */
    constructor(
        RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureKtxResources> resources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureKtxVertexOutput vertex(
        WebglLoaderTextureKtxVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureKtxVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only transparent flare-map entities. */
    WebglLoaderTextureKtxFrameBuffer fragment(
        WebglLoaderTextureKtxVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTextureKtxSample(inputValue, sceneSet, resources);
        WebglLoaderTextureKtxFrameBuffer outputValue;
        if (inputValue.entityAndPhase.y != 2u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureKtxLinearToSrgb(linearColor.x),
            webglLoaderTextureKtxLinearToSrgb(linearColor.y),
            webglLoaderTextureKtxLinearToSrgb(linearColor.z),
            linearColor.w);
        return outputValue;
    }
};

/** Owns the KTX Scene Set, three material phases, and single-sample output. */
class WebglLoaderTextureKtxRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderTextureKtxSceneRenderSet> sceneSet;
    BindGroup<WebglLoaderTextureKtxResources> resources;
    RenderClass<WebglLoaderTextureKtxOpaquePass> opaquePass;
    RenderClass<WebglLoaderTextureKtxNormalPass> normalPass;
    RenderClass<WebglLoaderTextureKtxTransparentPass> transparentPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler textureSampler;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
public:
    /** Creates the unique Scene Set and all fixed-state material phases. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderTextureKtxSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderTextureKtxSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 9,
            .maxAnisotropy = 1,
        });
        resources = device->createBindGroup<WebglLoaderTextureKtxResources>(textureSampler);
        opaquePass = device->createRenderClass<WebglLoaderTextureKtxOpaquePass>(sceneSet, resources);
        normalPass = device->createRenderClass<WebglLoaderTextureKtxNormalPass>(sceneSet, resources);
        transparentPass = device->createRenderClass<WebglLoaderTextureKtxTransparentPass>(sceneSet, resources);
    }
    /** Allocates ordinary single-sample RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglLoaderTextureKtxOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglLoaderTextureKtxDepth32", width, height, 1u);
    }
    /** Executes all three passes against the sole Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderTextureKtxFrameBuffer opaqueFrameBuffer;
        opaqueFrameBuffer.color = outputTexture->createView();
        opaqueFrameBuffer.color.loadOp = LoadOp::Clear;
        opaqueFrameBuffer.color.storeOp = StoreOp::Store;
        opaqueFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        opaqueFrameBuffer.depth = depthTexture->createView();
        opaqueFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        opaqueFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLoaderTextureKtxFrameBuffer loadFrameBuffer;
        loadFrameBuffer.color = outputTexture->createView();
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.color.storeOp = StoreOp::Store;
        loadFrameBuffer.depth = depthTexture->createView();
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderTextureKtxOpaque", opaqueFrameBuffer, opaquePass())
            ->renderPass("WebglLoaderTextureKtxNormal", loadFrameBuffer, normalPass())
            ->renderPass("WebglLoaderTextureKtxTransparent", loadFrameBuffer, transparentPass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }
    /** Returns the DSL-owned output texture. */
    auto getReadbackTextureHandle() const { return outputTexture; }
    /** Returns the configured output width. */
    uint getReadbackWidth() const { return readbackWidth; }
    /** Returns the configured output height. */
    uint getReadbackHeight() const { return readbackHeight; }
    /** Releases the unique Set and explicit output textures. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
