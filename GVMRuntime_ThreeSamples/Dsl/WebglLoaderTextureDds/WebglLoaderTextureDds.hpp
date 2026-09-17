#ifndef GVM_THREE_WEBGL_LOADER_TEXTURE_DDS_HPP
#define GVM_THREE_WEBGL_LOADER_TEXTURE_DDS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderTextureDdsMaxTextures = 32u;

/** Stores one canonical BoxGeometry vertex with flat face normal and UV. */
struct WebglLoaderTextureDdsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one entity transform and fixed material phase. */
struct WebglLoaderTextureDdsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 environmentModel;
    uint4 phaseAndFlags;
};

/** Stores the mandatory one-entry instance component. */
struct WebglLoaderTextureDdsInstanceData
{
    float4 reserved;
};

/** Stores the default Standard roughness and light intensities. */
struct WebglLoaderTextureDdsMaterialData
{
    float4 ambientPointRoughnessOpacity;
};

/** Defines the sole nine-entity DDS Scene RenderSet. */
struct WebglLoaderTextureDdsSceneRenderSet : public IRenderSet
{
    /** Declares packed BoxGeometry and one decoded texture slot per entity. */
    constructor(
        BufferComponent<WebglLoaderTextureDdsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderTextureDdsObjectData> objects,
        BufferComponent<WebglLoaderTextureDdsInstanceData> instances,
        BufferComponent<WebglLoaderTextureDdsMaterialData> materials,
        (TextureComponent<half4, WebglLoaderTextureDdsMaxTextures> textures),
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Binds the authored-mip sampler shared by all DDS materials. */
struct WebglLoaderTextureDdsResources final : public IBindGroup
{
    /** Declares a linear trilinear repeat sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries entity-resolved material inputs to all fixed-state phases. */
struct WebglLoaderTextureDdsVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float3 reflectionDirection [[Attribute3]];
    uint4 entityFlags [[Attribute4]];
};

/** Defines the normal single-sample color/depth output. */
struct WebglLoaderTextureDdsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Applies Three r185's output transfer to one linear channel. */
float webglLoaderTextureDdsLinearToSrgb(float value)
{
    return value <= 0.0031308f
        ? value * 12.92f
        : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglLoaderTextureDdsFresnel(float3 f0, float dotViewHalf)
{
    const float factor = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - factor) + float3(factor);
}

/** Evaluates the direct roughness-one GGX specular BRDF. */
float3 webglLoaderTextureDdsGgx(
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
    return webglLoaderTextureDdsFresnel(float3(0.04f), dotVH) *
        (visibility * distribution);
}

/** Resolves the current entity transform and component identity. */
WebglLoaderTextureDdsVertexOutput webglLoaderTextureDdsVertex(
    WebglLoaderTextureDdsVertex inputValue,
    IN RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLoaderTextureDdsObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglLoaderTextureDdsInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const uint4 renderFlags = sceneSet->renderFlags->get(renderEntityID, 0u);
    WebglLoaderTextureDdsVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection, inputValue.position);
    const float4 viewPosition = mul(objectData.modelView, inputValue.position);
    outputValue.viewPosition = viewPosition.xyz + instanceData.reserved.xyz;
    const float4 transformedNormal = mul(
        objectData.modelView,
        float4(inputValue.normal.xyz, 0.0f));
    outputValue.viewNormal = normalize(float3(
        transformedNormal.x, transformedNormal.y, transformedNormal.z));
    outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
    const float4 environmentPositionValue = mul(
        objectData.environmentModel,
        float4(inputValue.position.x, -inputValue.position.y,
               inputValue.position.z, 1.0f));
    const float4 environmentNormalValue = mul(
        objectData.environmentModel,
        float4(inputValue.normal.x, -inputValue.normal.y,
               inputValue.normal.z, 0.0f));
    const float3 environmentPosition = float3(
        environmentPositionValue.x, environmentPositionValue.y,
        environmentPositionValue.z);
    const float3 environmentNormal = normalize(float3(
        environmentNormalValue.x, environmentNormalValue.y,
        environmentNormalValue.z));
    outputValue.reflectionDirection = reflect(
        normalize(environmentPosition - float3(0.0f, -2.0f, 16.0f)),
        environmentNormal);
    outputValue.entityFlags = uint4(
        renderEntityID, renderFlags.x, renderFlags.y, renderFlags.z);
    return outputValue;
}

/** Samples one entity-local texture using CompressedTexture flipY semantics. */
float4 webglLoaderTextureDdsSample(
    WebglLoaderTextureDdsVertexOutput inputValue,
    IN RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTextureDdsResources> resources)
{
    auto textureValue = sceneSet->textures->get(inputValue.entityFlags.x, 0u);
    return float4(textureValue->sample(
        resources->textureSampler,
        inputValue.textureCoordinate));
}

/** Converts one reflection direction to a canonical DDS cube face and UV. */
float3 webglLoaderTextureDdsCubeFaceUv(float3 direction)
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
            coordinate = float2(-direction.z, -direction.y) /
                absoluteDirection.x;
        }
        else
        {
            face = 1.0f;
            coordinate = float2(direction.z, -direction.y) /
                absoluteDirection.x;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0f)
        {
            face = 2.0f;
            coordinate = float2(direction.x, direction.z) /
                absoluteDirection.y;
        }
        else
        {
            face = 3.0f;
            coordinate = float2(direction.x, -direction.z) /
                absoluteDirection.y;
        }
    }
    else if (direction.z >= 0.0f)
    {
        face = 4.0f;
        coordinate = float2(direction.x, -direction.y) /
            absoluteDirection.z;
    }
    else
    {
        face = 5.0f;
        coordinate = float2(-direction.x, -direction.y) /
            absoluteDirection.z;
    }
    return float3(coordinate * 0.5f + 0.5f, face);
}

/** Resolves canonical cube coordinates to the uploaded DDS face convention. */
float3 webglLoaderTextureDdsResolveCubeFace(float3 faceUv)
{
    const uint canonicalFace = uint(faceUv.z + 0.5f);
    return float3(faceUv.xy, float(canonicalFace));
}

/** Reconstructs a direction from possibly out-of-range canonical face UV. */
float3 webglLoaderTextureDdsCubeDirection(float3 faceUv)
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

/** Samples a canonical cube coordinate after remapping cross-face taps. */
float4 webglLoaderTextureDdsSampleCubeTap(
    float3 canonicalFaceUv,
    uint firstTextureSlot,
    uint renderEntityID,
    IN RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTextureDdsResources> resources)
{
    const float3 remappedFaceUv = webglLoaderTextureDdsResolveCubeFace(
        webglLoaderTextureDdsCubeFaceUv(
            webglLoaderTextureDdsCubeDirection(canonicalFaceUv)));
    auto textureValue = sceneSet->textures->get(
        renderEntityID,
        firstTextureSlot + uint(remappedFaceUv.z + 0.5f));
    return float4(textureValue->sampleLevel(
        resources->textureSampler, remappedFaceUv.xy, 0.0f));
}

/** Samples one DDS cube stored as six fixed entity texture slots. */
float4 webglLoaderTextureDdsSampleEnvironment(
    WebglLoaderTextureDdsVertexOutput inputValue,
    uint firstTextureSlot,
    IN RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet,
    IN BindGroup<WebglLoaderTextureDdsResources> resources)
{
    const float3 direction = float3(
        inputValue.reflectionDirection.x,
        inputValue.reflectionDirection.y,
        inputValue.reflectionDirection.z);
    const float3 canonicalFaceUv =
        webglLoaderTextureDdsCubeFaceUv(direction);
    const float3 resolvedFaceUv =
        webglLoaderTextureDdsResolveCubeFace(canonicalFaceUv);
    auto textureValue = sceneSet->textures->get(
        inputValue.entityFlags.x,
        firstTextureSlot + uint(resolvedFaceUv.z + 0.5f));
    float4 color = float4(textureValue->sampleLevel(
        resources->textureSampler, resolvedFaceUv.xy, 0.0f));
    const float faceSize = firstTextureSlot == 1u ? 512.0f : 128.0f;
    const float edgeRadius = 0.5f / faceSize;
    if (canonicalFaceUv.x < edgeRadius)
    {
        const float weight = 0.5f - canonicalFaceUv.x * faceSize;
        color = lerp(color, webglLoaderTextureDdsSampleCubeTap(
            float3(canonicalFaceUv.x - 1.0f / faceSize,
                   canonicalFaceUv.y, canonicalFaceUv.z),
            firstTextureSlot, inputValue.entityFlags.x,
            sceneSet, resources), weight);
    }
    else if (canonicalFaceUv.x > 1.0f - edgeRadius)
    {
        const float weight = 0.5f -
            (1.0f - canonicalFaceUv.x) * faceSize;
        color = lerp(color, webglLoaderTextureDdsSampleCubeTap(
            float3(canonicalFaceUv.x + 1.0f / faceSize,
                   canonicalFaceUv.y, canonicalFaceUv.z),
            firstTextureSlot, inputValue.entityFlags.x,
            sceneSet, resources), weight);
    }
    if (canonicalFaceUv.y < edgeRadius)
    {
        const float weight = 0.5f - canonicalFaceUv.y * faceSize;
        color = lerp(color, webglLoaderTextureDdsSampleCubeTap(
            float3(canonicalFaceUv.x,
                   canonicalFaceUv.y - 1.0f / faceSize,
                   canonicalFaceUv.z),
            firstTextureSlot, inputValue.entityFlags.x,
            sceneSet, resources), weight);
    }
    else if (canonicalFaceUv.y > 1.0f - edgeRadius)
    {
        const float weight = 0.5f -
            (1.0f - canonicalFaceUv.y) * faceSize;
        color = lerp(color, webglLoaderTextureDdsSampleCubeTap(
            float3(canonicalFaceUv.x,
                   canonicalFaceUv.y + 1.0f / faceSize,
                   canonicalFaceUv.z),
            firstTextureSlot, inputValue.entityFlags.x,
            sceneSet, resources), weight);
    }
    return color;
}

/** Draws the four opaque unlit compressed-color boxes. */
class WebglLoaderTextureDdsOpaquePass final : public IRenderClass
{
public:
    /** Configures the opaque back-cull phase on the unique Scene Set. */
    constructor(
        RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureDdsResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureDdsVertexOutput vertex(
        WebglLoaderTextureDdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureDdsVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only opaque color-map entities. */
    WebglLoaderTextureDdsFrameBuffer fragment(
        WebglLoaderTextureDdsVertexOutput inputValue)
    {
        float4 linearColor = webglLoaderTextureDdsSample(
            inputValue, sceneSet, resources);
        WebglLoaderTextureDdsFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 0u)
        {
            discard_fragment();
        }
        if (inputValue.entityFlags.z == 1u)
        {
            linearColor *= webglLoaderTextureDdsSampleEnvironment(
                inputValue, 1u, sceneSet, resources);
        }
        else if (inputValue.entityFlags.z == 2u)
        {
            linearColor = webglLoaderTextureDdsSampleEnvironment(
                inputValue, 0u, sceneSet, resources);
        }
        outputValue.color = half4(
            webglLoaderTextureDdsLinearToSrgb(linearColor.x),
            webglLoaderTextureDdsLinearToSrgb(linearColor.y),
            webglLoaderTextureDdsLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Draws the double-sided DXT3 alpha-tested box. */
class WebglLoaderTextureDdsAlphaMaskPass final : public IRenderClass
{
public:
    /** Configures the no-cull alpha-mask phase on the unique Scene Set. */
    constructor(
        RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureDdsResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureDdsVertexOutput vertex(
        WebglLoaderTextureDdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureDdsVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Applies the exact fixed 0.5 alpha-test threshold. */
    WebglLoaderTextureDdsFrameBuffer fragment(
        WebglLoaderTextureDdsVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTextureDdsSample(
            inputValue, sceneSet, resources);
        WebglLoaderTextureDdsFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 1u || linearColor.w < 0.5f)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureDdsLinearToSrgb(linearColor.x),
            webglLoaderTextureDdsLinearToSrgb(linearColor.y),
            webglLoaderTextureDdsLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Draws the double-sided DXT5 additive box without depth testing. */
class WebglLoaderTextureDdsAdditivePass final : public IRenderClass
{
public:
    /** Configures additive blending with disabled depth semantics. */
    constructor(
        RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureDdsResources> resources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureDdsVertexOutput vertex(
        WebglLoaderTextureDdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureDdsVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only the additive DXT5 entity. */
    WebglLoaderTextureDdsFrameBuffer fragment(
        WebglLoaderTextureDdsVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTextureDdsSample(inputValue, sceneSet, resources);
        WebglLoaderTextureDdsFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 2u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureDdsLinearToSrgb(linearColor.x),
            webglLoaderTextureDdsLinearToSrgb(linearColor.y),
            webglLoaderTextureDdsLinearToSrgb(linearColor.z),
            linearColor.w);
        return outputValue;
    }
};

/** Draws the final source-over uncompressed normal-map box. */
class WebglLoaderTextureDdsAlphaPass final : public IRenderClass
{
public:
    /** Configures source-over blending while retaining Scene depth testing. */
    constructor(
        RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureDdsResources> resources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }
private:
    /** Forwards the shared entity-resolved vertex calculation. */
    WebglLoaderTextureDdsVertexOutput vertex(
        WebglLoaderTextureDdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLoaderTextureDdsVertex(
            inputValue, sceneSet, renderEntityID, renderEntityInstanceID);
    }
    /** Samples only the final alpha-blended entity. */
    WebglLoaderTextureDdsFrameBuffer fragment(
        WebglLoaderTextureDdsVertexOutput inputValue)
    {
        const float4 linearColor = webglLoaderTextureDdsSample(
            inputValue, sceneSet, resources);
        WebglLoaderTextureDdsFrameBuffer outputValue;
        if (inputValue.entityFlags.y != 3u)
        {
            discard_fragment();
        }
        outputValue.color = half4(
            webglLoaderTextureDdsLinearToSrgb(linearColor.x),
            webglLoaderTextureDdsLinearToSrgb(linearColor.y),
            webglLoaderTextureDdsLinearToSrgb(linearColor.z),
            linearColor.w);
        return outputValue;
    }
};

/** Owns the DDS Scene Set, four material phases, and single-sample output. */
class WebglLoaderTextureDdsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderTextureDdsSceneRenderSet> sceneSet;
    BindGroup<WebglLoaderTextureDdsResources> resources;
    RenderClass<WebglLoaderTextureDdsOpaquePass> opaquePass;
    RenderClass<WebglLoaderTextureDdsAlphaMaskPass> alphaMaskPass;
    RenderClass<WebglLoaderTextureDdsAdditivePass> additivePass;
    RenderClass<WebglLoaderTextureDdsAlphaPass> alphaPass;
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
        sceneSet = device->createRenderSet<WebglLoaderTextureDdsSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderTextureDdsSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 9,
            .maxAnisotropy = 4,
        });
        resources = device->createBindGroup<WebglLoaderTextureDdsResources>(textureSampler);
        opaquePass = device->createRenderClass<WebglLoaderTextureDdsOpaquePass>(sceneSet, resources);
        alphaMaskPass = device->createRenderClass<WebglLoaderTextureDdsAlphaMaskPass>(sceneSet, resources);
        additivePass = device->createRenderClass<WebglLoaderTextureDdsAdditivePass>(sceneSet, resources);
        alphaPass = device->createRenderClass<WebglLoaderTextureDdsAlphaPass>(sceneSet, resources);
    }
    /** Allocates ordinary single-sample RGBA8 and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglLoaderTextureDdsOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglLoaderTextureDdsDepth32", width, height, 1u);
    }
    /** Executes all four material phases against the sole Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderTextureDdsFrameBuffer opaqueFrameBuffer;
        opaqueFrameBuffer.color = outputTexture->createView();
        opaqueFrameBuffer.color.loadOp = LoadOp::Clear;
        opaqueFrameBuffer.color.storeOp = StoreOp::Store;
        opaqueFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        opaqueFrameBuffer.depth = depthTexture->createView();
        opaqueFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        opaqueFrameBuffer.depth.depthClearValue = 1.0f;
        WebglLoaderTextureDdsFrameBuffer loadFrameBuffer;
        loadFrameBuffer.color = outputTexture->createView();
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.color.storeOp = StoreOp::Store;
        loadFrameBuffer.depth = depthTexture->createView();
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderTextureDdsOpaque", opaqueFrameBuffer, opaquePass())
            ->renderPass("WebglLoaderTextureDdsAlphaMask", loadFrameBuffer, alphaMaskPass())
            ->renderPass("WebglLoaderTextureDdsAdditive", loadFrameBuffer, additivePass())
            ->renderPass("WebglLoaderTextureDdsAlpha", loadFrameBuffer, alphaPass())
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
