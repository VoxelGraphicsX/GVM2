#ifndef GVM_THREE_WEBGL_MATERIALS_CUBEMAP_MIPMAPS_HPP
#define GVM_THREE_WEBGL_MATERIALS_CUBEMAP_MIPMAPS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one indexed SphereGeometry vertex with its analytic normal. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Carries transformed sphere data into the environment fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Stores the camera and world transform for one comparison sphere. */
struct ThreeBasicObjectData
{
    float4x4 modelViewProjection;
    float4x4 model;
    float4 cameraPosition;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForward;
};

/** Stores the mandatory one-entry instance component. */
struct ThreeBasicInstanceData
{
    float4 reserved;
};

/** Stores the white MeshBasicMaterial color. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
};

/** Binds the single linear sampler used for both explicit cubemap mip chains. */
struct WebglMaterialsCubemapMipmapsSamplerResources final : public IBindGroup
{
    /** Declares the six-face sampler without adding a public texture type. */
    constructor(Sampler environmentSampler [[Binding0]])
    {
    }
};

/** Defines one Scene RenderSet for the manual/generated mipmap comparison. */
struct WebglMaterialsCubemapMipmapsSceneRenderSet : public IRenderSet
{
    /** Consolidates sphere geometry, entity data, and six texture slots. */
    constructor(BufferComponent<ThreeBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ThreeBasicObjectData> objects,
                BufferComponent<ThreeBasicInstanceData> instances,
                BufferComponent<ThreeBasicMaterialData> materials,
                (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Defines the explicit single-sample capture attachments. */
struct WebglMaterialsCubemapMipmapsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel into the r185 sRGB output encoding. */
float webglMaterialsCubemapMipmapsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Selects the Three-compatible face and normalized coordinates for a ray. */
float3 webglMaterialsCubemapMipmapsFaceUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x > absoluteDirection.z)
    {
        if (absoluteDirection.x > absoluteDirection.y)
        {
            return direction.x > 0.0f
                ? float3(3.0f, direction.z, -direction.y) / absoluteDirection.x
                : float3(0.0f, -direction.z, -direction.y) / absoluteDirection.x;
        }
        return direction.y > 0.0f
            ? float3(1.0f, -direction.x, direction.z) / absoluteDirection.y
            : float3(4.0f, -direction.x, -direction.z) / absoluteDirection.y;
    }
    if (absoluteDirection.z > absoluteDirection.y)
    {
        return direction.z > 0.0f
            ? float3(2.0f, -direction.x, -direction.y) / absoluteDirection.z
            : float3(5.0f, direction.x, -direction.y) / absoluteDirection.z;
    }
    return direction.y > 0.0f
        ? float3(1.0f, -direction.x, direction.z) / absoluteDirection.y
        : float3(4.0f, -direction.x, -direction.z) / absoluteDirection.y;
}

/** Samples one entity's six packed faces through its RenderSet texture component. */
float3 webglMaterialsCubemapMipmapsSampleEnvironment(
    IN RenderSet<WebglMaterialsCubemapMipmapsSceneRenderSet> sceneSet,
    IN BindGroup<WebglMaterialsCubemapMipmapsSamplerResources> resources,
    uint entityID,
    float3 direction,
    float lod)
{
    const float3 faceUv = webglMaterialsCubemapMipmapsFaceUv(normalize(direction));
    const uint face = uint(faceUv.x);
    const float2 baseUv = clamp(float2(faceUv.y * 0.5f + 0.5f, faceUv.z * 0.5f + 0.5f),
                                float2(0.000001f), float2(0.999999f));
    // TextureComponent exposes eight total slots in this phase.  The generated
    // and manual variants are therefore packed side-by-side in each of the six
    // face textures; x=0 selects generated mipmaps and x=1 selects manual ones.
    const bool manualVariant = sceneSet->objects->get(entityID, 0u).cameraPosition.w > 0.5f;
    // sample() derives the LOD from the sphere derivatives.  Keep the packed
    // half-texture coordinate in base-level space; shrinking it by the
    // requested roughness level would make implicit filtering address a
    // different cube face and produces the characteristic repeated blobs.
    const float mipWidth = 512.0f;
    const float halfTexel = 0.5f / mipWidth;
    const float variantOrigin = manualVariant ? 0.5f : 0.0f;
    const float variantX = manualVariant
        ? variantOrigin + halfTexel + baseUv.x * (0.5f - 2.0f * halfTexel)
        : variantOrigin + halfTexel + baseUv.x * (0.5f - 2.0f * halfTexel);
    const float2 uv = float2(variantX, 1.0f - baseUv.y);
    // The Angus files use the CubeTextureLoader order px,nx,py,ny,pz,nz.
    const uint textureSlot = face == 0u
        ? 0u
        : face == 1u
            ? 3u
            : face == 2u
                ? 4u
                : face == 3u
                    ? 1u
                    : face == 4u
                        ? 2u
                        : 5u;
    return textureSlot == 0u
        ? float3(sceneSet->textures->get(0u, 0u)->sample(resources->environmentSampler, uv).xyz)
        : textureSlot == 1u
            ? float3(sceneSet->textures->get(0u, 1u)->sample(resources->environmentSampler, uv).xyz)
            : textureSlot == 2u
                ? float3(sceneSet->textures->get(0u, 2u)->sample(resources->environmentSampler, uv).xyz)
                : textureSlot == 3u
                    ? float3(sceneSet->textures->get(0u, 3u)->sample(resources->environmentSampler, uv).xyz)
                    : textureSlot == 4u
                        ? float3(sceneSet->textures->get(0u, 4u)->sample(resources->environmentSampler, uv).xyz)
                        : float3(sceneSet->textures->get(0u, 5u)->sample(resources->environmentSampler, uv).xyz);
}

/** Draws both spheres with the RenderSet indexed-indirect path. */
class WebglMaterialsCubemapMipmapsScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and keeps the opaque comparison depth-tested. */
    constructor(RenderSet<WebglMaterialsCubemapMipmapsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsCubemapMipmapsSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects the sphere and reconstructs its world-space normal. */
    ThreeBasicVertexOutput vertex(
        ThreeBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const ThreeBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + instanceData.reserved;
        ThreeBasicVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
        outputValue.worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies the MeshBasic environment lookup and sRGB output conversion. */
    WebglMaterialsCubemapMipmapsFrameBuffer fragment(ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 incident = normalize(inputValue.worldPosition - objectData.cameraPosition.xyz);
        const float3 direction = reflect(incident, normalize(inputValue.worldNormal));
        const float facing = saturate(dot(normalize(inputValue.worldNormal), -incident));
        const float mipLevel = inputValue.entityID == 0u
            ? (0.35f + (1.0f - facing) * 1.6f)
            : (2.0f + (1.0f - facing) * 2.0f);
        const float3 environment = webglMaterialsCubemapMipmapsSampleEnvironment(
            sceneSet, resources, inputValue.entityID, direction, mipLevel);
        const float3 linearColor = saturate(environment * materialData.baseColor.xyz);
        WebglMaterialsCubemapMipmapsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMaterialsCubemapMipmapsLinearToSrgb(linearColor.x)),
            half(webglMaterialsCubemapMipmapsLinearToSrgb(linearColor.y)),
            half(webglMaterialsCubemapMipmapsLinearToSrgb(linearColor.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated cubemap RenderSet and explicit single-sample output. */
class WebglMaterialsCubemapMipmapsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsCubemapMipmapsSceneRenderSet> sceneSet;
    Sampler environmentSampler;
    BindGroup<WebglMaterialsCubemapMipmapsSamplerResources> resources;
    RenderClass<WebglMaterialsCubemapMipmapsScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the dedicated RenderSet, sampler, and scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMaterialsCubemapMipmapsSceneRenderSet>();
        environmentSampler = device->createSampler({
            .label = "WebglMaterialsCubemapMipmapsSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 16.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebglMaterialsCubemapMipmapsSamplerResources>(environmentSampler);
        scenePass = device->createRenderClass<WebglMaterialsCubemapMipmapsScenePass>(sceneSet, resources);
    }

    /** Recreates the explicit single-sample capture attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("WebglMaterialsCubemapMipmapsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMaterialsCubemapMipmapsDepth", width, height, 1u);
    }

    /** Submits one Scene pass through RenderSet indexed-indirect drawing. */
    void render() override
    {
        sceneSet->update();
        WebglMaterialsCubemapMipmapsFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass("WebglMaterialsCubemapMipmapsScene", frameBuffer, scenePass())
            ->renderToSwapchain(swapchain->queryNextTexture(), outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 readback target. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the RenderSet and explicit output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglMaterialsCubemapMipmapsRenderer
#undef WebglMaterialsCubemapMipmapsFrameBuffer
#undef WebglMaterialsCubemapMipmapsScenePass
#undef WebglMaterialsCubemapMipmapsSceneRenderSet

#endif
