#ifndef GVM_THREE_WEBGL_LOADER_COLLADA_HPP
#define GVM_THREE_WEBGL_LOADER_COLLADA_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglLoaderColladaTextureCapacity = 4u;

/** Stores the expanded Elf position, normal, UV, and group material index. */
struct WebglLoaderColladaVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uvAndMaterial [[Attribute2]];
};

/** Stores the camera, hierarchy transform, normal transform, and lighting state. */
struct WebglLoaderColladaObjectData
{
    float4x4 model;
    float4x4 viewProjection;
    float4x4 normalTransform;
    float4 cameraPosition;
    float4 directionalLight;
};

/** Stores the mandatory single-instance component entry. */
struct WebglLoaderColladaInstanceData
{
    float4 reserved;
};

/** Stores one Phong material's specular color and shininess. */
struct WebglLoaderColladaMaterialData
{
    float4 specularAndShininess;
};

/** Defines the only RenderSet owned by the webgl_loader_collada Scene. */
struct WebglLoaderColladaSceneRenderSet : public IRenderSet
{
    /** Declares the packed mesh, object state, four materials, and four textures. */
    constructor(
        BufferComponent<WebglLoaderColladaVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderColladaObjectData> objects,
        BufferComponent<WebglLoaderColladaInstanceData> instances,
        BufferComponent<WebglLoaderColladaMaterialData> materials,
        (TextureComponent<half4, WebglLoaderColladaTextureCapacity> textures))
    {
    }
};

/** Binds the trilinear repeat sampler shared by the four Elf materials. */
struct WebglLoaderColladaTextureResources final : public IBindGroup
{
    /** Declares the immutable texture sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries resolved hierarchy and Phong inputs to the fragment stage. */
struct WebglLoaderColladaVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint materialIndex [[Attribute3]];
    uint entityID [[Attribute4]];
};

/** Defines the ordinary single-sample COLLADA output attachments. */
struct WebglLoaderColladaFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear-light channel for the browser canvas. */
float webglLoaderColladaLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the exact one-mesh four-group Elf through the unique Scene Set. */
class WebglLoaderColladaMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and its shared trilinear sampler. */
    constructor(RenderSet<WebglLoaderColladaSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglLoaderColladaTextureResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the one entity while retaining RenderSet entity semantics. */
    WebglLoaderColladaVertexOutput vertex(
        WebglLoaderColladaVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLoaderColladaObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderColladaInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 worldPosition = mul(
            objectData.model,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        WebglLoaderColladaVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = normalize(float3(
            mul(objectData.normalTransform, float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.uv = inputValue.uvAndMaterial.xy;
        outputValue.materialIndex = uint(inputValue.uvAndMaterial.z);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the matching texture slot and evaluates r185 Blinn-Phong lighting. */
    WebglLoaderColladaFrameBuffer fragment(
        WebglLoaderColladaVertexOutput inputValue)
    {
        const WebglLoaderColladaObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglLoaderColladaMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, inputValue.materialIndex);
        auto diffuseTexture = sceneSet->textures->get(
            inputValue.entityID, inputValue.materialIndex);
        const float3 albedo = float3(diffuseTexture->sample(
            resources->textureSampler, inputValue.uv).xyz);
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 lightDirection = normalize(
            float3(objectData.directionalLight.xyz));
        const float3 viewDirection = normalize(
            objectData.cameraPosition.xyz - inputValue.worldPosition);
        const float directWeight = max(dot(normal, lightDirection), 0.0f);
        float3 linearColor = albedo * 0.31830988618f *
            (1.0f + objectData.directionalLight.w * directWeight);
        if (directWeight > 0.0f)
        {
            const float3 halfDirection = normalize(lightDirection + viewDirection);
            const float dotNH = max(dot(normal, halfDirection), 0.0f);
            const float dotVH = max(dot(viewDirection, halfDirection), 0.0f);
            const float fresnelWeight = exp2(
                (-5.55473f * dotVH - 6.98316f) * dotVH);
            const float3 fresnel =
                materialData.specularAndShininess.xyz *
                    (1.0f - fresnelWeight) +
                float3(fresnelWeight);
            const float distribution =
                (materialData.specularAndShininess.w * 0.5f + 1.0f) *
                0.31830988618f * pow(
                dotNH,
                materialData.specularAndShininess.w);
            linearColor += fresnel * 0.25f * distribution * directWeight *
                objectData.directionalLight.w;
        }
        const float3 displayColor = float3(
            webglLoaderColladaLinearToSrgb(linearColor.x),
            webglLoaderColladaLinearToSrgb(linearColor.y),
            webglLoaderColladaLinearToSrgb(linearColor.z));
        WebglLoaderColladaFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated COLLADA Scene Set and single-sample output. */
class WebglLoaderColladaRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderColladaSceneRenderSet> sceneSet;
    BindGroup<WebglLoaderColladaTextureResources> textureResources;
    RenderClass<WebglLoaderColladaMainPass> mainPass;
    Sampler textureSampler;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set, sampler, and dedicated render class. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderColladaSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglLoaderColladaTextureSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 16.0f,
            .maxAnisotropy = 1u,
        });
        textureResources = device->createBindGroup<
            WebglLoaderColladaTextureResources>(textureSampler);
        mainPass = device->createRenderClass<WebglLoaderColladaMainPass>(
            sceneSet, textureResources);
    }

    /** Allocates single-sample fixed-size output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglLoaderColladaOutput", width, height, 1u);
        outputDepth = device->createTexture(
            "WebglLoaderColladaDepth", width, height, 1u);
    }

    /** Preserves the common host adapter hook; this example has no inspector. */
    void configureInspector(float enabled)
    {
        (void)enabled;
    }

    /** Updates the Set and renders the one automatic indexed-indirect command. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderColladaFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderColladaMain", frameBuffer, mainPass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 readback texture. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and private attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
