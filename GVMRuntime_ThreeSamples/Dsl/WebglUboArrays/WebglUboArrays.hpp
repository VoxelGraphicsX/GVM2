#ifndef GVM_THREE_WEBGL_UBO_ARRAYS_HPP
#define GVM_THREE_WEBGL_UBO_ARRAYS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one tetrahedron or box vertex in the unified Scene layout. */
struct WebglUboArraysVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores the per-entity model and normal transforms plus material phase. */
struct WebglUboArraysObjectData
{
    float4x4 model;
    float4x4 modelView;
    float4x4 normalTransform;
    uint4 materialPhase;
};

/** Stores the mandatory identity instance record for each ordinary entity. */
struct WebglUboArraysInstanceData
{
    float4 reserved;
};

/** Stores the per-entity untextured color used by alternating tetrahedra. */
struct WebglUboArraysMaterialData
{
    float4 baseColor;
};

/** Defines the unique 101-entity Scene RenderSet for the UBO-array example. */
struct WebglUboArraysSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry, object data, and entity-local crate maps. */
    constructor(
        BufferComponent<WebglUboArraysVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglUboArraysObjectData> objects,
        BufferComponent<WebglUboArraysInstanceData> instances,
        BufferComponent<WebglUboArraysMaterialData> materials)
    {
    }
};

/** Stores the camera and lighting values shared by both material phases. */
struct WebglUboArraysSharedData
{
    float4x4 projection;
    float4 lightPosition[300];
    float4 lightColor[300];
    float4 pointLightsCountAndPadding;
};

/** Binds the shared UBO used by the Scene pass. */
struct WebglUboArraysResources final : public IBindGroup
{
    /** Declares the fixed-size light-array uniform block. */
    constructor(UniformBuffer<WebglUboArraysSharedData> sharedData [[Binding0]])
    {
    }
};

/** Carries view-space attributes and entity identity to the fragment stage. */
struct WebglUboArraysVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    uint2 entityAndPhase [[Attribute3]];
};

/** Defines the single-sample color/depth output used by all 200 entities. */
struct WebglUboArraysFrameBuffer final : public IFrameBuffer
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
class WebglUboArraysScenePass final : public IRenderClass
{
public:
    /** Binds the only Scene Set and shared camera/lighting UBO. */
    constructor(
        RenderSet<WebglUboArraysSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglUboArraysResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity transforms while preserving the shared UBO contract. */
    WebglUboArraysVertexOutput vertex(
        WebglUboArraysVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        const WebglUboArraysObjectData objectData =
            sceneSet->objects->get(entityID, 0u);
        const WebglUboArraysInstanceData instanceData =
            sceneSet->instances->get(entityID, instanceID);
        const float4 viewPosition = mul(
            objectData.modelView,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 transformedNormal = mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f));
        WebglUboArraysVertexOutput outputValue;
        outputValue.position = mul(resources->sharedData->projection, viewPosition);
        outputValue.position.y = -outputValue.position.y;
        // The raw r185 shader intentionally names this varying vPositionEye
        // but writes model-space/world-space coordinates, because the light
        // array is authored in the same world basis.  Preserve that contract
        // while using modelView only for clip-space projection.
        outputValue.viewPosition = mul(
            objectData.model,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f)).xyz;
        outputValue.viewNormal = inputValue.normal.xyz;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.entityAndPhase = uint2(entityID, objectData.materialPhase.x);
        return outputValue;
    }

    /** Evaluates the exact shared Phong equation and alternating base source. */
    WebglUboArraysFrameBuffer fragment(WebglUboArraysVertexOutput inputValue)
    {
        float3 linearColor = float3(0.0f);
        const uint lightCount = min(
            uint(max(resources->sharedData->pointLightsCountAndPadding.x, 0.0f)), 300u);
        for (uint lightIndex = 0u; lightIndex < 300u; ++lightIndex)
        {
            if (lightIndex >= lightCount)
                break;
            const float3 offset = resources->sharedData->lightPosition[lightIndex].xyz -
                inputValue.viewPosition;
            const float distanceToLight = length(offset);
            const float distanceFalloff = 1.0f /
                max(pow(distanceToLight, 0.7f), 0.01f);
            const float cutoff = saturate(
                1.0f - pow(distanceToLight / 4.0f, 4.0f));
            const float attenuation = distanceFalloff * cutoff * cutoff;
            linearColor += resources->sharedData->lightColor[lightIndex].xyz *
                attenuation;
        }
        WebglUboArraysFrameBuffer outputValue;
        outputValue.color = half4(
            webglUboLinearToSrgb(linearColor.x),
            webglUboLinearToSrgb(linearColor.y),
            webglUboLinearToSrgb(linearColor.z),
            1.0f);
        return outputValue;
    }
};

/** Owns the UBO Scene Set, shared uniform buffer, and single-sample output. */
class WebglUboArraysRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglUboArraysSceneRenderSet> sceneSet;
    Buffer<WebglUboArraysSharedData, BufferUsage<Uniform, CopyDst>> sharedBuffer;
        BindGroup<WebglUboArraysResources> resources;
    RenderClass<WebglUboArraysScenePass> scenePass;
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
        sceneSet = device->createRenderSet<WebglUboArraysSceneRenderSet>();
        sharedBuffer = device->createBuffer("WebglUboArraysSharedData", 1u);
    }

    /** Allocates the ordinary single-sample output and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglUboArraysOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglUboArraysDepth", width, height, 1u);
    }

    /** Uploads the fixed light arrays and creates the sole Scene pass. */
    void configureShared(
        float4x4 projection,
        float4 lightPositions[300],
        float4 lightColors[300],
        float4 pointLightsCountAndPadding)
    {
        WebglUboArraysSharedData sharedData;
        sharedData.projection = projection;
        for (uint lightIndex = 0u; lightIndex < 300u; ++lightIndex)
        {
            sharedData.lightPosition[lightIndex] = lightPositions[lightIndex];
            sharedData.lightColor[lightIndex] = lightColors[lightIndex];
        }
        sharedData.pointLightsCountAndPadding = pointLightsCountAndPadding;
        graphicsQueue->writeBuffer(
            BufferRange(sharedBuffer), &sharedData, sizeof(sharedData))->submit();
        resources = device->createBindGroup<WebglUboArraysResources>(sharedBuffer);
        scenePass = device->createRenderClass<WebglUboArraysScenePass>(
            sceneSet, resources);
    }

    /** Draws all entities from Set-owned indexed-indirect metadata. */
    void render() override
    {
        sceneSet->update();
        WebglUboArraysFrameBuffer frameBuffer;
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
            ->renderPass("WebglUboArraysMain", frameBuffer, scenePass())
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
