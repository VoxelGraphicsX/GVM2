#ifndef GVM_THREE_WEBGL_INSTANCING_RAYCAST_HPP
#define GVM_THREE_WEBGL_INSTANCING_RAYCAST_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one smooth-normal triangle vertex from Three r185 IcosahedronGeometry. */
struct WebglInstancingRaycastVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the shared camera, hemisphere direction, and material selection for the mesh entity. */
struct WebglInstancingRaycastObjectData
{
    float4x4 viewProjection;
    float4 hemisphereDirection;
    uint4 materialAndFlags;
};

/** Stores one instance translation and mutable linear-light instance color. */
struct WebglInstancingRaycastInstanceData
{
    float4 translation;
    float4 color;
};

/** Stores the private Phong material inputs and Three r185 hemisphere irradiance colors. */
struct WebglInstancingRaycastMaterialData
{
    float4 baseColor;
    float4 skyIrradiance;
    float4 groundIrradiance;
};

/** Defines the unique Scene RenderSet used by every geometry submission in this sample. */
struct WebglInstancingRaycastSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and object, instance, and material components. */
    constructor(BufferComponent<WebglInstancingRaycastVertex> vertices [[RenderSetVertexBuffer]], BufferComponent<uint> indices [[RenderSetIndexBuffer]], BufferComponent<WebglInstancingRaycastObjectData> objects, BufferComponent<WebglInstancingRaycastInstanceData> instances, BufferComponent<WebglInstancingRaycastMaterialData> materials)
    {
    }
};

/** Carries RenderSet-resolved Phong inputs from the vertex stage to the fragment stage. */
struct WebglInstancingRaycastVertexOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    float3 hemisphereDirection [[Attribute1]];
    float4 instanceColor [[Attribute2]];
    uint2 entityAndMaterial [[Attribute3]];
};

/** Defines the deterministic single-sample color and depth targets used for readback. */
struct WebglInstancingRaycastFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's exact output transfer constants. */
float webglInstancingRaycastLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the single instanced mesh entity through RenderSet indexed-indirect metadata only. */
class WebglInstancingRaycastMainPass final : public IRenderClass
{
public:
    /** Binds the Scene's only RenderSet and the fixed Three MeshPhongMaterial depth state. */
    constructor(RenderSet<WebglInstancingRaycastSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance components through both RenderEntity builtins. */
    WebglInstancingRaycastVertexOutput vertex(WebglInstancingRaycastVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]], uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingRaycastObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglInstancingRaycastInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float3 worldPosition = inputValue.position.xyz + instanceData.translation.xyz;

        WebglInstancingRaycastVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, float4(worldPosition, 1.0f));
        outputValue.worldNormal = inputValue.normal.xyz;
        outputValue.hemisphereDirection = objectData.hemisphereDirection.xyz;
        outputValue.instanceColor = instanceData.color;
        outputValue.entityAndMaterial = uint2(renderEntityID, objectData.materialAndFlags.x);
        return outputValue;
    }

    /** Evaluates Three's hemisphere-only Phong indirect diffuse term and sRGB output. */
    WebglInstancingRaycastFrameBuffer fragment(WebglInstancingRaycastVertexOutput inputValue)
    {
        const WebglInstancingRaycastMaterialData materialData = sceneSet->materials->get(inputValue.entityAndMaterial.x, inputValue.entityAndMaterial.y);
        const float hemisphereWeight = dot(normalize(inputValue.worldNormal), normalize(inputValue.hemisphereDirection)) * 0.5f + 0.5f;
        const float3 irradiance = materialData.groundIrradiance.xyz * (1.0f - hemisphereWeight) + materialData.skyIrradiance.xyz * hemisphereWeight;
        const float inversePi = 0.3183098861837907f;
        const float3 linearColor = irradiance * materialData.baseColor.xyz * inputValue.instanceColor.xyz * inversePi;

        WebglInstancingRaycastFrameBuffer frameBuffer;
        frameBuffer.color = half4(webglInstancingRaycastLinearToSrgb(linearColor.x), webglInstancingRaycastLinearToSrgb(linearColor.y), webglInstancingRaycastLinearToSrgb(linearColor.z), 1.0f);
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and all GPU work for webgl_instancing_raycast. */
class WebglInstancingRaycastRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInstancingRaycastSceneRenderSet> sceneSet;
    RenderClass<WebglInstancingRaycastMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the one Scene RenderSet and its parameterless indexed-indirect RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInstancingRaycastSceneRenderSet>();
        scenePass = device->createRenderClass<WebglInstancingRaycastMainPass>(sceneSet);
    }

    /** Allocates deterministic RGBA8 and native-depth capture targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglInstancingRaycastOutputRGBA8", width, height, 1u);
        depthTexture = device->createTexture("WebglInstancingRaycastDepth32", width, height, 1u);
    }

    /** Applies pending entity updates and issues exactly one parameterless Scene draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        WebglInstancingRaycastFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue->renderPass("WebglInstancingRaycastScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 output used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene RenderSet and explicit capture targets. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
