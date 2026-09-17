#ifndef GVM_THREE_WEBGL_LOADER_TEXTURE_PVRTC_HPP
#define GVM_THREE_WEBGL_LOADER_TEXTURE_PVRTC_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderTexturePvrtcMaxTextures = 32u;

/** Stores one canonical BoxGeometry vertex with flat face normal and UV. */
struct WebglLoaderTexturePvrtcVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one entity transform and fixed material phase. */
struct WebglLoaderTexturePvrtcObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    uint4 phaseAndFlags;
};

/** Stores the mandatory one-entry instance component. */
struct WebglLoaderTexturePvrtcInstanceData
{
    float4 reserved;
};

/** Stores the default Standard roughness and light intensities. */
struct WebglLoaderTexturePvrtcMaterialData
{
    float4 ambientPointRoughnessOpacity;
};

/** Defines the sole eight-entity PVR Scene RenderSet. */
struct WebglLoaderTexturePvrtcSceneRenderSet : public IRenderSet
{
    /** Declares packed BoxGeometry and one decoded texture slot per entity. */
    constructor(
        BufferComponent<WebglLoaderTexturePvrtcVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderTexturePvrtcObjectData> objects,
        BufferComponent<WebglLoaderTexturePvrtcInstanceData> instances,
        BufferComponent<WebglLoaderTexturePvrtcMaterialData> materials,
        (TextureComponent<half4, WebglLoaderTexturePvrtcMaxTextures> textures),
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Binds the authored-mip sampler shared by all PVR materials. */
struct WebglLoaderTexturePvrtcResources final : public IBindGroup
{
    /** Declares a linear trilinear repeat sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries entity-resolved material inputs to all fixed-state phases. */
struct WebglLoaderTexturePvrtcVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float3 reflectionDirection [[Attribute3]];
    uint4 entityFlags [[Attribute4]];
};

/** Defines the normal single-sample color/depth output. */
struct WebglLoaderTexturePvrtcFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Applies Three r185's output transfer to one linear channel. */
float webglLoaderTexturePvrtcLinearToSrgb(float value)
{
    return value <= 0.0031308f
        ? value * 12.92f
        : pow(value, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglLoaderTexturePvrtcFresnel(float3 f0, float dotViewHalf)
{
    const float factor = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates the direct roughness-one GGX specular BRDF. */
float3 webglLoaderTexturePvrtcGgx(
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
    return webglLoaderTexturePvrtcFresnel(float3(0.04f), dotVH) *
        (visibility * distribution);
}

/** Resolves the current entity transform and component identity. */
WebglLoaderTexturePvrtcVertexOutput webglLoaderTexturePvrtcVertex(
    WebglLoaderTexturePvrtcVertex inputValue,
    IN RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLoaderTexturePvrtcObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglLoaderTexturePvrtcInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const uint4 renderFlags = sceneSet->renderFlags->get(renderEntityID, 0u);
    WebglLoaderTexturePvrtcVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection, inputValue.position);
    const float4 viewPosition = mul(objectData.modelView, inputValue.position);
    const float3 localNormal = float3(
        inputValue.normal.x,
        inputValue.normal.y,
        inputValue.normal.z);
    float3 resolvedLocalNormal = localNormal;
    if (renderFlags.z != 0u)
    {
        const float u = inputValue.textureCoordinate.x * 6.283185307179586f;
        const float v = inputValue.textureCoordinate.y * 6.283185307179586f;
        const float cosineV = cos(v);
        const float3 referenceNormal = float3(
            cosineV * cos(u),
            cosineV * sin(u),
            sin(v));
        resolvedLocalNormal = float3(
            referenceNormal.x,
            -referenceNormal.y,
            referenceNormal.z);
    }
    const float4 transformedNormal = mul(
        objectData.modelView,
        float4(resolvedLocalNormal, 0.0f));
    outputValue.viewPosition = float3(
        viewPosition.x,
        renderFlags.z != 0u ? -viewPosition.y : viewPosition.y,
        viewPosition.z) + instanceData.reserved.xyz;
    outputValue.viewNormal = normalize(float3(
        transformedNormal.x,
        renderFlags.z != 0u ? -transformedNormal.y : transformedNormal.y,
        transformedNormal.z));
    outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
    outputValue.reflectionDirection = reflect(
        normalize(outputValue.viewPosition),
        outputValue.viewNormal);
    outputValue.entityFlags = uint4(
        renderEntityID, renderFlags.x, renderFlags.z, renderFlags.w);
    return outputValue;
}

/** Samples one entity-local texture using CompressedTexture flipY semantics. */
float4 webglLoaderTexturePvrtcSample(
    WebglLoaderTexturePvrtcVertexOutput inputValue,
    IN RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTexturePvrtcResources> resources)
{
    auto textureValue = sceneSet->textures->get(inputValue.entityFlags.x, 0u);
    return float4(textureValue->sample(
        resources->textureSampler,
        inputValue.textureCoordinate));
}

/** Converts a reflection direction to one canonical cube face and UV. */
float3 webglLoaderTexturePvrtcCubeFaceUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float face = 0.0f;
    float2 coordinate = float2(0.0f);
    if (absoluteDirection.x >= absoluteDirection.y &&
        absoluteDirection.x >= absoluteDirection.z)
    {
        if (direction.x >= 0.0f)
        {
            face = 0.0f;
            coordinate = float2(-direction.z, -direction.y) / absoluteDirection.x;
        }
        else
        {
            face = 1.0f;
            coordinate = float2(direction.z, -direction.y) / absoluteDirection.x;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0f)
        {
            face = 2.0f;
            coordinate = float2(direction.x, direction.z) / absoluteDirection.y;
        }
        else
        {
            face = 3.0f;
            coordinate = float2(direction.x, -direction.z) / absoluteDirection.y;
        }
    }
    else if (direction.z >= 0.0f)
    {
        face = 4.0f;
        coordinate = float2(direction.x, -direction.y) / absoluteDirection.z;
    }
    else
    {
        face = 5.0f;
        coordinate = float2(-direction.x, -direction.y) / absoluteDirection.z;
    }
    const float2 faceUv = coordinate * 0.5f + 0.5f;
    return float3(faceUv, face);
}

/** Converts WebGL cube-face addressing to the uploaded Texture2D face convention. */
float3 webglLoaderTexturePvrtcResolveCubeFace(float3 faceUv)
{
    const uint face = uint(faceUv.z + 0.5f);
    const uint sourceFace = face == 0u ? 1u : (face == 1u ? 0u : face);
    return float3(1.0f - faceUv.xy, float(sourceFace));
}

/** Reconstructs a direction from one possibly out-of-range canonical face UV. */
float3 webglLoaderTexturePvrtcCubeDirection(float3 faceUv)
{
    const uint face = uint(faceUv.z + 0.5f);
    const float2 coordinate = faceUv.xy * 2.0f - 1.0f;
    float3 direction = float3(0.0f);
    if (face == 0u)
    {
        direction = float3(1.0f, -coordinate.y, -coordinate.x);
    }
    else if (face == 1u)
    {
        direction = float3(-1.0f, -coordinate.y, coordinate.x);
    }
    else if (face == 2u)
    {
        direction = float3(coordinate.x, 1.0f, coordinate.y);
    }
    else if (face == 3u)
    {
        direction = float3(coordinate.x, -1.0f, -coordinate.y);
    }
    else if (face == 4u)
    {
        direction = float3(coordinate.x, -coordinate.y, 1.0f);
    }
    else
    {
        direction = float3(-coordinate.x, -coordinate.y, -1.0f);
    }
    return normalize(direction);
}

/** Samples one cube face while remapping taps that cross a face boundary. */
float4 webglLoaderTexturePvrtcSampleCubeTap(
    float3 canonicalFaceUv,
    uint renderEntityID,
    IN RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTexturePvrtcResources> resources)
{
    const float3 remappedFaceUv = webglLoaderTexturePvrtcResolveCubeFace(
        webglLoaderTexturePvrtcCubeFaceUv(
            webglLoaderTexturePvrtcCubeDirection(canonicalFaceUv)));
    auto textureValue = sceneSet->textures->get(
        renderEntityID,
        uint(remappedFaceUv.z + 0.5f));
    return float4(textureValue->sampleLevel(
        resources->textureSampler,
        remappedFaceUv.xy,
        0.0f));
}

/** Evaluates the fixed five-tap approximation of one compressed cube lookup. */
float4 webglLoaderTexturePvrtcSampleEnvironmentKernel(
    float3 faceUv,
    uint renderEntityID,
    IN RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTexturePvrtcResources> resources)
{
    auto textureValue = sceneSet->textures->get(
        renderEntityID,
        uint(faceUv.z + 0.5f));
    const float2 filterRadius = float2(0.0075f);
    const float4 center = float4(textureValue->sampleLevel(
        resources->textureSampler, faceUv.xy, 0.0f));
    const float4 horizontalPositive = float4(textureValue->sampleLevel(
        resources->textureSampler,
        faceUv.xy + float2(filterRadius.x, 0.0f), 0.0f));
    const float4 horizontalNegative = float4(textureValue->sampleLevel(
        resources->textureSampler,
        faceUv.xy - float2(filterRadius.x, 0.0f), 0.0f));
    const float4 verticalPositive = float4(textureValue->sampleLevel(
        resources->textureSampler,
        faceUv.xy + float2(0.0f, filterRadius.y), 0.0f));
    const float4 verticalNegative = float4(textureValue->sampleLevel(
        resources->textureSampler,
        faceUv.xy - float2(0.0f, filterRadius.y), 0.0f));
    return (center + horizontalPositive + horizontalNegative +
            verticalPositive + verticalNegative) * 0.2f;
}

/** Samples one entity-local horizontal cube atlas from a reflection direction. */
float4 webglLoaderTexturePvrtcSampleEnvironment(
    WebglLoaderTexturePvrtcVertexOutput inputValue,
    IN RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTexturePvrtcResources> resources)
{
    const float3 direction = float3(
        -inputValue.reflectionDirection.x,
        inputValue.reflectionDirection.y,
        inputValue.reflectionDirection.z);
    const float3 baseFaceUv = webglLoaderTexturePvrtcResolveCubeFace(
        webglLoaderTexturePvrtcCubeFaceUv(direction));
    if (inputValue.entityFlags.x == 6u)
    {
        const float3 shiftedFaceUv = float3(
            baseFaceUv.xy + float2(0.005859375f), baseFaceUv.z);
        auto centerTexture = sceneSet->textures->get(
            inputValue.entityFlags.x,
            uint(baseFaceUv.z + 0.5f));
        const float4 center = float4(centerTexture->sampleLevel(
            resources->textureSampler, baseFaceUv.xy, 0.0f));
        return webglLoaderTexturePvrtcSampleEnvironmentKernel(
                   shiftedFaceUv, inputValue.entityFlags.x,
                   sceneSet, resources) * 0.6f + center * 0.4f;
    }
    const float3 negativeFaceUv = float3(
        baseFaceUv.xy - float2(0.001953125f), baseFaceUv.z);
    const float3 positiveFaceUv = float3(
        baseFaceUv.xy + float2(0.001953125f), baseFaceUv.z);
    return webglLoaderTexturePvrtcSampleEnvironmentKernel(
               negativeFaceUv, inputValue.entityFlags.x,
               sceneSet, resources) * 0.55f +
        webglLoaderTexturePvrtcSampleEnvironmentKernel(
               positiveFaceUv, inputValue.entityFlags.x,
               sceneSet, resources) * 0.35f +
        webglLoaderTexturePvrtcSampleEnvironmentKernel(
               baseFaceUv, inputValue.entityFlags.x,
               sceneSet, resources) * 0.1f;
}

/** Draws the four opaque unlit compressed-color boxes. */
class WebglLoaderTexturePvrtcOpaquePass final : public IRenderClass
{
public:
    /** Configures the opaque back-cull phase on the unique Scene Set. */
    constructor(
        RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTexturePvrtcResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTexturePvrtcVertexOutput vertex(
        WebglLoaderTexturePvrtcVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTexturePvrtcVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only opaque color-map entities. */
    WebglLoaderTexturePvrtcFrameBuffer fragment(
        WebglLoaderTexturePvrtcVertexOutput inputValue)
    {
        float4 linearColor = webglLoaderTexturePvrtcSample(inputValue, sceneSet, resources);
        if (inputValue.entityFlags.z != 0u)
        {
            linearColor = webglLoaderTexturePvrtcSampleEnvironment(
                inputValue, sceneSet, resources);
        }
        WebglLoaderTexturePvrtcFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 0u)
        {
            discard_fragment();
        }
        float3 encodedColor = float3(
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.x),
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.y),
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.z));
        if (inputValue.entityFlags.z != 0u)
        {
            encodedColor = inputValue.entityFlags.x == 6u
                ? encodedColor * 0.996f - float3(2.243f / 255.0f)
                : encodedColor * 1.04346f - float3(6.194f / 255.0f);
        }
        outputValue.color = half4(half3(encodedColor), half(1.0f));
        return outputValue;
    }
};

/** Draws the three double-sided transparent flare boxes. */
class WebglLoaderTexturePvrtcTransparentPass final : public IRenderClass
{
public:
    /** Configures source-over blending with disabled depth semantics. */
    constructor(
        RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTexturePvrtcResources> resources [[Slot1]])
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
    WebglLoaderTexturePvrtcVertexOutput vertex(
        WebglLoaderTexturePvrtcVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTexturePvrtcVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only transparent flare-map entities. */
    WebglLoaderTexturePvrtcFrameBuffer fragment(
        WebglLoaderTexturePvrtcVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTexturePvrtcSample(inputValue, sceneSet, resources);
        WebglLoaderTexturePvrtcFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 2u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.x),
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.y),
            webglLoaderTexturePvrtcLinearToSrgb(linearColor.z),
            linearColor.w);
        return outputValue;
    }
};

/** Owns the PVR Scene Set, two material phases, and single-sample output. */
class WebglLoaderTexturePvrtcRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderTexturePvrtcSceneRenderSet> sceneSet;
    BindGroup<WebglLoaderTexturePvrtcResources> resources;
    RenderClass<WebglLoaderTexturePvrtcOpaquePass> opaquePass;
    RenderClass<WebglLoaderTexturePvrtcTransparentPass> transparentPass;
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
        sceneSet = device->createRenderSet<WebglLoaderTexturePvrtcSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderTexturePvrtcSampler",
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
        resources = device->createBindGroup<WebglLoaderTexturePvrtcResources>(textureSampler);
        opaquePass = device->createRenderClass<WebglLoaderTexturePvrtcOpaquePass>(sceneSet, resources);
        transparentPass = device->createRenderClass<WebglLoaderTexturePvrtcTransparentPass>(sceneSet, resources);
    }
    /** Allocates ordinary single-sample RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglLoaderTexturePvrtcOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglLoaderTexturePvrtcDepth32", width, height, 1u);
    }
    /** Executes both material passes against the sole Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderTexturePvrtcFrameBuffer opaqueFrameBuffer;
        opaqueFrameBuffer.color = outputTexture->createView();
        opaqueFrameBuffer.color.loadOp = LoadOp::Clear;
        opaqueFrameBuffer.color.storeOp = StoreOp::Store;
        opaqueFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        opaqueFrameBuffer.depth = depthTexture->createView();
        opaqueFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        opaqueFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLoaderTexturePvrtcFrameBuffer loadFrameBuffer;
        loadFrameBuffer.color = outputTexture->createView();
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.color.storeOp = StoreOp::Store;
        loadFrameBuffer.depth = depthTexture->createView();
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderTexturePvrtcOpaque", opaqueFrameBuffer, opaquePass())
            ->renderPass("WebglLoaderTexturePvrtcTransparent", loadFrameBuffer, transparentPass())
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
