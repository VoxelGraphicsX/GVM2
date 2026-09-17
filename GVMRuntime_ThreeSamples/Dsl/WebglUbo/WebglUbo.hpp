#ifndef GVM_THREE_WEBGL_UBO_HPP
#define GVM_THREE_WEBGL_UBO_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglUboTextureCapacity = 256u;

/** Stores one tetrahedron or box vertex in the unified Scene layout. */
struct WebglUboVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores the per-entity model and normal transforms plus material phase. */
struct WebglUboObjectData
{
    float4x4 modelView;
    float4x4 normalTransform;
    uint4 materialPhase;
};

/** Stores the mandatory identity instance record for each ordinary entity. */
struct WebglUboInstanceData
{
    float4 reserved;
};

/** Stores the per-entity untextured color used by alternating tetrahedra. */
struct WebglUboMaterialData
{
    float4 baseColor;
};

/** Defines the unique 200-entity Scene RenderSet for the UBO example. */
struct WebglUboSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry, object data, and entity-local crate maps. */
    constructor(
        BufferComponent<WebglUboVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglUboObjectData> objects,
        BufferComponent<WebglUboInstanceData> instances,
        BufferComponent<WebglUboMaterialData> materials,
        (TextureComponent<half4, WebglUboTextureCapacity> textures))
    {
    }
};

/** Stores the camera and lighting values shared by both material phases. */
struct WebglUboSharedData
{
    float4x4 projection;
    float4 lightPositionAndShininess;
    float4 ambientColor;
    float4 diffuseColor;
    float4 specularColor;
};

/** Binds the shared UBO and crate sampler used by the Scene pass. */
struct WebglUboResources final : public IBindGroup
{
    /** Declares the frozen UBO and the trilinear crate sampler. */
    constructor(
        UniformBuffer<WebglUboSharedData> sharedData [[Binding0]],
        Sampler crateSampler [[Binding1]])
    {
    }
};

/** Carries RawShaderMaterial-compatible Phong inputs to the fragment stage. */
struct WebglUboVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    uint2 entityAndPhase [[Attribute3]];
};

/** Defines the single-sample color/depth output used by all 200 entities. */
struct WebglUboFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Applies the exact transfer function embedded in the r185 raw shaders. */
float webglUboLinearToSrgb(float value)
{
    return value <= 0.0031308f
        ? value * 12.92f
        : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Draws both raw-shader phases through the unique Scene RenderSet. */
class WebglUboScenePass final : public IRenderClass
{
public:
    /** Binds the only Scene Set and shared camera/lighting UBO. */
    constructor(
        RenderSet<WebglUboSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglUboResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity transforms while preserving the shared UBO contract. */
    WebglUboVertexOutput vertex(
        WebglUboVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        const WebglUboObjectData objectData =
            sceneSet->objects->get(entityID, 0u);
        const WebglUboInstanceData instanceData =
            sceneSet->instances->get(entityID, instanceID);
        const float4 viewPosition = mul(
            objectData.modelView,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 transformedNormal = mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f));
        WebglUboVertexOutput outputValue;
        outputValue.position = mul(resources->sharedData->projection, viewPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = transformedNormal.xyz;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityAndPhase = uint2(entityID, objectData.materialPhase.x);
        return outputValue;
    }

    /** Evaluates the exact shared Phong equation and alternating base source. */
    WebglUboFrameBuffer fragment(WebglUboVertexOutput inputValue)
    {
        const WebglUboMaterialData material =
            sceneSet->materials->get(inputValue.entityAndPhase.x, 0u);
        const float3 lightDirection = normalize(
            resources->sharedData->lightPositionAndShininess.xyz -
            inputValue.viewPosition);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 eyeDirection = -normalize(inputValue.viewPosition);
        const float3 reflectionDirection = normalize(reflect(-lightDirection, normal));
        const float diffuseWeight = max(dot(normal, lightDirection), 0.0f);
        float specularWeight = max(dot(reflectionDirection, eyeDirection), 0.0f);
        specularWeight = pow(
            specularWeight,
            resources->sharedData->lightPositionAndShininess.w);
        const float3 lightWeight =
            resources->sharedData->ambientColor.xyz +
            resources->sharedData->diffuseColor.xyz * diffuseWeight +
            resources->sharedData->specularColor.xyz * specularWeight;
        float3 baseColor = material.baseColor.xyz;
        if (inputValue.entityAndPhase.y == 1u)
        {
            auto crateTexture = sceneSet->textures->get(
                inputValue.entityAndPhase.x, 0u);
            const float2 sampleCoordinate = float2(
                inputValue.textureCoordinate.x,
                inputValue.textureCoordinate.y);
            baseColor = float4(crateTexture->sample(
                resources->crateSampler,
                sampleCoordinate)).xyz;
        }
        const float3 linearColor = baseColor * lightWeight;
        WebglUboFrameBuffer outputValue;
        outputValue.color = half4(
            webglUboLinearToSrgb(linearColor.x),
            webglUboLinearToSrgb(linearColor.y),
            webglUboLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Owns the UBO Scene Set, shared uniform buffer, and single-sample output. */
class WebglUboRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglUboSceneRenderSet> sceneSet;
    Buffer<WebglUboSharedData, BufferUsage<Uniform, CopyDst>> sharedBuffer;
    Sampler crateSampler;
    BindGroup<WebglUboResources> resources;
    RenderClass<WebglUboScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene Set, shared UBO storage, and crate sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglUboSceneRenderSet>();
        sharedBuffer = device->createBuffer("WebglUboSharedData", 1u);
        crateSampler = device->createSampler({
            .label = "WebglUboCrateSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the ordinary single-sample output and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglUboOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglUboDepth", width, height, 1u);
    }

    /** Uploads the shared UBO and creates the sole Scene pass. */
    void configureShared(
        float4x4 projection,
        float4 lightPositionAndShininess,
        float4 ambientColor,
        float4 diffuseColor,
        float4 specularColor)
    {
        WebglUboSharedData sharedData;
        sharedData.projection = projection;
        sharedData.lightPositionAndShininess = lightPositionAndShininess;
        sharedData.ambientColor = ambientColor;
        sharedData.diffuseColor = diffuseColor;
        sharedData.specularColor = specularColor;
        graphicsQueue->writeBuffer(
            BufferRange(sharedBuffer), &sharedData, sizeof(sharedData))->submit();
        resources = device->createBindGroup<WebglUboResources>(
            sharedBuffer, crateSampler);
        scenePass = device->createRenderClass<WebglUboScenePass>(
            sceneSet, resources);
    }

    /** Draws all entities from Set-owned indexed-indirect metadata. */
    void render() override
    {
        sceneSet->update();
        WebglUboFrameBuffer frameBuffer;
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
            ->renderPass("WebglUboMain", frameBuffer, scenePass())
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned output texture for deterministic readback. */
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
