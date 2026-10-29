#ifndef GVM_THREE_WEBGL_MATERIALS_CUBEMAP_REFRACTION_HPP
#define GVM_THREE_WEBGL_MATERIALS_CUBEMAP_REFRACTION_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Lucy position and flat normal in the single Scene RenderSet. */
struct WebglMaterialsCubemapRefractionVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Carries camera-space and world-space values to the fragment stage. */
struct WebglMaterialsCubemapRefractionVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 screenUv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Stores the model matrices and the deterministic camera basis. */
struct WebglMaterialsCubemapRefractionObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 cameraPositionAndFlags;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Stores the required one-entry instance component. */
struct WebglMaterialsCubemapRefractionInstanceData
{
    float4 reserved;
};

/** Stores Phong tint, refraction ratio, and reflectivity. */
struct WebglMaterialsCubemapRefractionMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Binds the six explicit Park3Med faces with a stable linear sampler. */
struct WebglMaterialsCubemapRefractionSamplerResources final : public IBindGroup
{
    /** Declares the filtered cube-face sampler. */
    constructor(Sampler environmentSampler [[Binding0]])
    {
    }
};

/** Owns the Lucy geometry, material components, and Park3Med texture pool. */
struct WebglMaterialsCubemapRefractionSceneRenderSet : public IRenderSet
{
    /** Declares one consolidated Scene Set for both background and mesh passes. */
    constructor(
        BufferComponent<WebglMaterialsCubemapRefractionVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMaterialsCubemapRefractionObjectData> objects,
        BufferComponent<WebglMaterialsCubemapRefractionInstanceData> instances,
        BufferComponent<WebglMaterialsCubemapRefractionMaterialData> materials,
        (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Defines the explicit single-sample target used by both scene passes. */
struct WebglMaterialsCubemapRefractionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear component into the Three canvas sRGB encoding. */
float webglMaterialsCubemapRefractionLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the GLSL refraction equation with total-internal-reflection fallback. */
float3 webglMaterialsCubemapRefractionRefract(float3 incident, float3 normal, float ratio)
{
    // Match GLSL's built-in refract(I, N, eta) exactly.  The legacy
    // MeshPhong env-map path does not re-orient the normal or invert eta for
    // back-facing fragments; its built-in returns a zero vector on total
    // internal reflection, which is subsequently sampled by the cube map.
    const float cosine = dot(incident, normal);
    const float k = 1.0f - ratio * ratio *
        (1.0f - cosine * cosine);
    return k < 0.0f
        ? float3(0.0f)
        : ratio * incident -
            (ratio * cosine + sqrt(max(k, 0.0f))) * normal;
}

/** Selects the legacy WebGL face orientation used by the locked Park3Med background. */
float3 webglMaterialsCubemapRefractionBackgroundFace(float3 direction)
{
    const float3 a = abs(direction);
    if (a.x > a.z)
    {
        if (a.x > a.y)
            return direction.x > 0.0f
                ? float3(3.0f, direction.z, -direction.y) / a.x
                : float3(0.0f, -direction.z, -direction.y) / a.x;
        return direction.y > 0.0f
            ? float3(1.0f, -direction.x, direction.z) / a.y
            : float3(4.0f, -direction.x, -direction.z) / a.y;
    }
    if (a.z > a.y)
        return direction.z > 0.0f
            ? float3(2.0f, -direction.x, -direction.y) / a.z
            : float3(5.0f, direction.x, -direction.y) / a.z;
    return direction.y > 0.0f
        ? float3(1.0f, -direction.x, direction.z) / a.y
        : float3(4.0f, -direction.x, -direction.z) / a.y;
}

/** Selects the locked cube face orientation used by the Three WebGL refraction path. */
float3 webglMaterialsCubemapRefractionObjectFace(float3 direction)
{
    return webglMaterialsCubemapRefractionBackgroundFace(direction);
}

/** Samples the six Scene-owned faces using the r185 cube loader convention. */
float3 webglMaterialsCubemapRefractionSampleEnvironment(
    IN RenderSet<WebglMaterialsCubemapRefractionSceneRenderSet> sceneSet,
    IN BindGroup<WebglMaterialsCubemapRefractionSamplerResources> samplerResources,
    float3 direction,
    uint mappingMode)
{
    const float3 face = mappingMode == 0u
        ? webglMaterialsCubemapRefractionBackgroundFace(normalize(direction))
        : webglMaterialsCubemapRefractionObjectFace(normalize(direction));
    const uint faceIndex = uint(face.x);
    const float2 uv = clamp(face.yz * 0.5f + 0.5f,
                            float2(0.000001f), float2(0.999999f));
    const float2 textureUv = float2(uv.x, 1.0f - uv.y);
    const uint textureSlot = faceIndex == 0u
        ? 0u
        : faceIndex == 1u
            ? 3u
            : faceIndex == 2u
                ? 4u
                : faceIndex == 3u
                    ? 1u
                    : faceIndex == 4u
                        ? 2u
                        : 5u;
    return textureSlot == 0u
        ? float3(sceneSet->textures->get(0u, 0u)->sample(samplerResources->environmentSampler, textureUv).xyz)
        : textureSlot == 1u
            ? float3(sceneSet->textures->get(0u, 1u)->sample(samplerResources->environmentSampler, textureUv).xyz)
            : textureSlot == 2u
                ? float3(sceneSet->textures->get(0u, 2u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                : textureSlot == 3u
                    ? float3(sceneSet->textures->get(0u, 3u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                    : textureSlot == 4u
                        ? float3(sceneSet->textures->get(0u, 4u)->sample(samplerResources->environmentSampler, textureUv).xyz)
                        : float3(sceneSet->textures->get(0u, 5u)->sample(samplerResources->environmentSampler, textureUv).xyz);
}

/** Draws the Park3Med environment before the three Lucy entities. */
class WebglMaterialsCubemapRefractionBackgroundPass final : public IRenderClass
{
public:
    /** Uses the shared Scene Set textures without depth writes. */
    constructor(
        RenderSet<WebglMaterialsCubemapRefractionSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglMaterialsCubemapRefractionSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the standard fullscreen triangle. */
    WebglMaterialsCubemapRefractionVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglMaterialsCubemapRefractionVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.worldPosition = float3(uv, 0.0f);
        outputValue.worldNormal = float3(0.0f);
        outputValue.screenUv = uv;
        outputValue.entityID = 0u;
        return outputValue;
    }

    /** Reconstructs a camera ray and writes the Park3Med background. */
    WebglMaterialsCubemapRefractionFrameBuffer fragment(
        WebglMaterialsCubemapRefractionVertexOutput inputValue)
    {
        const WebglMaterialsCubemapRefractionObjectData cameraData =
            sceneSet->objects->get(0u, 0u);
        const float2 ndc = inputValue.screenUv * 2.0f - 1.0f;
        const float3 direction = normalize(
            cameraData.cameraForwardAndReserved.xyz +
            cameraData.cameraRightAndTanHalfFov.xyz *
                (ndc.x * cameraData.cameraUpAndAspect.w * cameraData.cameraRightAndTanHalfFov.w) +
            cameraData.cameraUpAndAspect.xyz *
                (-ndc.y * cameraData.cameraRightAndTanHalfFov.w));
        const float3 environment = webglMaterialsCubemapRefractionSampleEnvironment(
            sceneSet, samplerResources, direction, 0u);
        WebglMaterialsCubemapRefractionFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglMaterialsCubemapRefractionLinearToSrgb(environment.x)),
            half(webglMaterialsCubemapRefractionLinearToSrgb(environment.y)),
            half(webglMaterialsCubemapRefractionLinearToSrgb(environment.z)),
            half(1.0f));
        return outputValue;
    }
};

/** Draws all Lucy entities through one indexed-indirect Scene RenderSet. */
class WebglMaterialsCubemapRefractionMainPass final : public IRenderClass
{
public:
    /** Binds the shared texture pool and preserves both sides of Lucy. */
    constructor(
        RenderSet<WebglMaterialsCubemapRefractionSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglMaterialsCubemapRefractionSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance data and computes world-space normals. */
    WebglMaterialsCubemapRefractionVertexOutput vertex(
        WebglMaterialsCubemapRefractionVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMaterialsCubemapRefractionObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMaterialsCubemapRefractionInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + instanceData.reserved;
        const float4 clip = mul(objectData.modelViewProjection, localPosition);
        WebglMaterialsCubemapRefractionVertexOutput outputValue;
        outputValue.position = clip;
        outputValue.position.y = -clip.y;
        outputValue.position.z = (clip.z + clip.w) * 0.5f;
        outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
        outputValue.worldNormal = normalize(float3(
            mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.screenUv = float2(0.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies Three's Phong-like direct term plus environment refraction. */
    WebglMaterialsCubemapRefractionFrameBuffer fragment(
        WebglMaterialsCubemapRefractionVertexOutput inputValue)
    {
        const WebglMaterialsCubemapRefractionObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglMaterialsCubemapRefractionMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 viewDirection = normalize(
            inputValue.worldPosition - objectData.cameraPositionAndFlags.xyz);
        const float3 refractedDirection = webglMaterialsCubemapRefractionRefract(
            viewDirection, normal, materialData.parameters.x);
        const float3 environment = webglMaterialsCubemapRefractionSampleEnvironment(
            sceneSet, samplerResources, refractedDirection, 1u);
        // The r185 scene contains only AmbientLight(0xffffff, 3.5). MeshPhong
        // evaluates its indirect Lambert term as irradiance * diffuse / PI;
        // there is no directional or point-light contribution in this example.
        const float3 direct = materialData.baseColor.xyz * (3.5f / 3.14159265358979323846f);
        const float reflectivity = clamp(materialData.parameters.y, 0.0f, 1.0f);
        const float environmentWeight = reflectivity;
        const float3 linearColor = saturate(direct + (environment - direct) * environmentWeight);
        WebglMaterialsCubemapRefractionFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMaterialsCubemapRefractionLinearToSrgb(linearColor.x)),
            half(webglMaterialsCubemapRefractionLinearToSrgb(linearColor.y)),
            half(webglMaterialsCubemapRefractionLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the single Scene Set, two scene passes, and explicit single-sample targets. */
class WebglMaterialsCubemapRefractionRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsCubemapRefractionSceneRenderSet> sceneSet;
    Sampler environmentSampler;
    BindGroup<WebglMaterialsCubemapRefractionSamplerResources> samplerResources;
    RenderClass<WebglMaterialsCubemapRefractionBackgroundPass> backgroundPass;
    RenderClass<WebglMaterialsCubemapRefractionMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the RenderSet, sampler, and generated passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMaterialsCubemapRefractionSceneRenderSet>();
        environmentSampler = device->createSampler({
            .label = "WebglMaterialsCubemapRefractionSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglMaterialsCubemapRefractionSamplerResources>(environmentSampler);
        backgroundPass = device->createRenderClass<WebglMaterialsCubemapRefractionBackgroundPass>(sceneSet, samplerResources);
        scenePass = device->createRenderClass<WebglMaterialsCubemapRefractionMainPass>(sceneSet, samplerResources);
    }

    /** Allocates the explicit RGBA8/depth targets with one sample. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMaterialsCubemapRefractionColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMaterialsCubemapRefractionDepth", width, height, 1u);
    }

    /** Submits background then Lucy through the same Set and presents once. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglMaterialsCubemapRefractionFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = outputColor->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        backgroundFrameBuffer.depth = outputDepth->createView();
        backgroundFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        backgroundFrameBuffer.depth.depthClearValue = 1.0f;
        WebglMaterialsCubemapRefractionFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Load;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.depth = outputDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        graphicsQueue
            ->renderPass("WebglMaterialsCubemapRefractionBackground", backgroundFrameBuffer, backgroundPass())
            ->renderPass("WebglMaterialsCubemapRefractionScene", sceneFrameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 texture used for readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases explicit attachments and the unique Scene Set. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglMaterialsCubemapRefractionRenderer
#undef WebglMaterialsCubemapRefractionFrameBuffer
#undef WebglMaterialsCubemapRefractionMainPass
#undef WebglMaterialsCubemapRefractionBackgroundPass
#undef WebglMaterialsCubemapRefractionSceneRenderSet

#endif
