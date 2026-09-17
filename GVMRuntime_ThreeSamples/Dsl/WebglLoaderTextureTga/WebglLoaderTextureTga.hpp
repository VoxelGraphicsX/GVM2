#ifndef GVM_THREE_WEBGL_LOADER_TEXTURE_TGA_HPP
#define GVM_THREE_WEBGL_LOADER_TEXTURE_TGA_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderTextureTgaTextureCapacity = 4u;

/** Stores one r185 BoxGeometry vertex with a flat face normal and UV. */
struct WebglLoaderTextureTgaVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one box model-view transform, projection, and directional light. */
struct WebglLoaderTextureTgaObjectData
{
    float4x4 modelView;
    float4x4 normalTransform;
    float4x4 projection;
    float4 lightDirectionAndIntensity;
    float4 ambientIntensityAndReserved;
};

/** Stores the mandatory ordinary one-instance component. */
struct WebglLoaderTextureTgaInstanceData
{
    float4 reserved;
};

/** Stores MeshPhong color, specular color, and shininess. */
struct WebglLoaderTextureTgaMaterialData
{
    float4 diffuseColor;
    float4 specularColorAndShininess;
};

/** Defines the one two-entity Scene RenderSet for both TGA materials. */
struct WebglLoaderTextureTgaSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and entity-local decoded texture components. */
    constructor(
        BufferComponent<WebglLoaderTextureTgaVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderTextureTgaObjectData> objects,
        BufferComponent<WebglLoaderTextureTgaInstanceData> instances,
        BufferComponent<WebglLoaderTextureTgaMaterialData> materials,
        (TextureComponent<half4, WebglLoaderTextureTgaTextureCapacity> textures))
    {
    }
};

/** Binds the explicit trilinear sampler shared by both TGA entities. */
struct WebglLoaderTextureTgaResources final : public IBindGroup
{
    /** Declares the clamp sampler used by DataTexture flipY semantics. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries view-space Phong data and entity identity into the fragment stage. */
struct WebglLoaderTextureTgaVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglLoaderTextureTgaFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a nonnegative linear-light channel to the canvas sRGB encoding. */
float webglLoaderTextureTgaLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three r185's normalized Blinn-Phong direct specular term. */
float3 webglLoaderTextureTgaBlinnPhong(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal,
    float3 specularColor,
    float shininess,
    float lightIntensity)
{
    const float dotNormalLight = max(dot(normal, lightDirection), 0.0f);
    const float dotNormalHalf = max(
        dot(normal, normalize(lightDirection + viewDirection)), 0.0f);
    return specularColor * lightIntensity * dotNormalLight * 0.25f *
        (shininess * 0.5f + 1.0f) * 0.3183098861837907f *
        pow(dotNormalHalf, shininess);
}

/** Draws both TGA boxes through one Scene RenderSet indexed-indirect pass. */
class WebglLoaderTextureTgaPhongPass final : public IRenderClass
{
public:
    /** Binds the unique Set and enables opaque back-face culling and depth. */
    constructor(
        RenderSet<WebglLoaderTextureTgaSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglLoaderTextureTgaResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves one entity's transform and preserves the authored TGA UV. */
    WebglLoaderTextureTgaVertexOutput vertex(
        WebglLoaderTextureTgaVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLoaderTextureTgaObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderTextureTgaInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 viewPosition = mul(
            objectData.modelView,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        WebglLoaderTextureTgaVertexOutput outputValue;
        outputValue.position = mul(objectData.projection, viewPosition);
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(float3(mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the decoded TGA and evaluates ambient plus directional Phong. */
    WebglLoaderTextureTgaFrameBuffer fragment(
        WebglLoaderTextureTgaVertexOutput inputValue)
    {
        const WebglLoaderTextureTgaObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglLoaderTextureTgaMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float4 sampled = float4(
                sceneSet->textures->get(inputValue.entityID, 0u)->sample(
                resources->textureSampler,
                float2(1.0f - inputValue.textureCoordinate.x,
                       1.0f - inputValue.textureCoordinate.y)));
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection = normalize(
            float3(objectData.lightDirectionAndIntensity.xyz));
        const float lightIntensity =
            objectData.lightDirectionAndIntensity.w;
        const float diffuseWeight = max(dot(normal, lightDirection), 0.0f);
        const float3 diffuse = sampled.xyz * materialData.diffuseColor.xyz *
            (objectData.ambientIntensityAndReserved.x +
             lightIntensity * diffuseWeight) *
            0.3183098861837907f;
        const float3 specular = webglLoaderTextureTgaBlinnPhong(
            lightDirection,
            viewDirection,
            normal,
            materialData.specularColorAndShininess.xyz,
            materialData.specularColorAndShininess.w,
            lightIntensity);
        const float3 linearColor = diffuse + specular;
        WebglLoaderTextureTgaFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglLoaderTextureTgaLinearToSrgb(linearColor.x)),
            half(webglLoaderTextureTgaLinearToSrgb(linearColor.y)),
            half(webglLoaderTextureTgaLinearToSrgb(linearColor.z)),
            half(1.0f));
        return outputValue;
    }
};

/** Owns the TGA Scene Set, sampler, and deterministic single-sample targets. */
class WebglLoaderTextureTgaRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderTextureTgaSceneRenderSet> sceneSet;
    Sampler textureSampler;
    BindGroup<WebglLoaderTextureTgaResources> resources;
    RenderClass<WebglLoaderTextureTgaPhongPass> phongPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the one RenderSet and sampler-backed Phong pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderTextureTgaSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderTextureTgaSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebglLoaderTextureTgaResources>(
            textureSampler);
        phongPass = device->createRenderClass<WebglLoaderTextureTgaPhongPass>(
            sceneSet, resources);
    }

    /** Allocates explicit single-sample color and depth attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglLoaderTextureTgaOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglLoaderTextureTgaDepth", width, height, 1u);
    }

    /** Submits the sole Scene Set draw through indexed indirect metadata. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderTextureTgaFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglLoaderTextureTgaPhong",
                frameBuffer,
                phongPass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 output used by formal readback. */
    auto getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the RenderSet and explicit attachments after capture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
