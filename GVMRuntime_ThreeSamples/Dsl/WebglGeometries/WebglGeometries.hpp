#ifndef GVM_THREE_WEBGL_GEOMETRIES_HPP
#define GVM_THREE_WEBGL_GEOMETRIES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometriesMaxTextures = 16u;

/** Stores the shared position, normal, and UV layout for all sixteen geometries. */
struct WebglGeometriesVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores one entity transform plus the camera-relative point-light state. */
struct WebglGeometriesObjectData
{
    float4x4 modelView;
    float4x4 projection;
    float4 ambientPointIntensity;
};

/** Stores the mandatory one-entry instance component. */
struct WebglGeometriesInstanceData
{
    float4 reserved;
};

/** Stores the shared Phong material constants. */
struct WebglGeometriesMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Defines the one RenderSet owned by the sixteen-object Scene. */
struct WebglGeometriesSceneRenderSet : public IRenderSet
{
    /** Declares the packed geometry, entity components, and fixed UV-grid texture slots. */
    constructor(
        BufferComponent<WebglGeometriesVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometriesObjectData> objects,
        BufferComponent<WebglGeometriesInstanceData> instances,
        BufferComponent<WebglGeometriesMaterialData> materials,
        (TextureComponent<half4, WebglGeometriesMaxTextures> textures))
    {
    }
};

/** Binds the repeat, trilinear, anisotropic sampler shared by all entities. */
struct WebglGeometriesSamplerResources final : public IBindGroup
{
    /** Declares the one immutable UV-grid sampler. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries double-sided Phong inputs into one of the two material passes. */
struct WebglGeometriesVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglGeometriesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear-light channel for the browser canvas. */
float webglGeometriesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Resolves entity and instance data and applies the exact object transform. */
WebglGeometriesVertexOutput webglGeometriesTransformVertex(
    IN RenderSet<WebglGeometriesSceneRenderSet> sceneSet,
    WebglGeometriesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID,
    float normalSign)
{
    const WebglGeometriesObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGeometriesInstanceData instanceData =
        sceneSet->instances->get(
            renderEntityID,
            renderEntityInstanceID);
    const float4 localPosition =
        inputValue.position +
        float4(instanceData.reserved.xyz, 0.0f);
    const float4 viewPosition =
        mul(objectData.modelView, localPosition);
    float4 clipPosition =
        mul(objectData.projection, viewPosition);
    clipPosition.y = -clipPosition.y;
    clipPosition.z =
        (clipPosition.z + clipPosition.w) * 0.5f;
    WebglGeometriesVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewPosition.xyz;
    outputValue.viewNormal =
        normalize(
            float3(
                mul(
                    objectData.modelView,
                    float4(
                        inputValue.normal.xyz * normalSign,
                        0.0f))
                    .xyz));
    outputValue.uv = inputValue.uv.xy;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates the shared r185 MeshPhong material in linear working space. */
float4 webglGeometriesShade(
    IN RenderSet<WebglGeometriesSceneRenderSet> sceneSet,
    BindGroup<WebglGeometriesSamplerResources> samplerResources,
    WebglGeometriesVertexOutput inputValue)
{
    const WebglGeometriesObjectData objectData =
        sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglGeometriesMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    const float3 normal =
        normalize(inputValue.viewNormal);
    const float3 lightDirection =
        normalize(-inputValue.viewPosition);
    const float3 viewDirection =
        lightDirection;
    const float3 halfDirection =
        normalize(lightDirection + viewDirection);
    const float dotNormalLight =
        max(dot(normal, lightDirection), 0.0f);
    const float dotNormalHalf =
        max(dot(normal, halfDirection), 0.0f);
    const float3 texel =
        float3(
            sceneSet->textures
                ->get(inputValue.entityID, 0u)
                ->sample(
                    samplerResources->textureSampler,
                    inputValue.uv)
                .xyz);
    const float inversePi = 0.3183098861837907f;
    const float3 diffuse =
        texel *
        materialData.diffuseAndShininess.xyz *
        (objectData.ambientPointIntensity.x +
         objectData.ambientPointIntensity.y *
             dotNormalLight) *
        inversePi;
    const float3 specular =
        materialData.specular.xyz *
        objectData.ambientPointIntensity.y *
        dotNormalLight *
        0.25f *
        (materialData.diffuseAndShininess.w *
             0.5f +
         1.0f) *
        inversePi *
        pow(
            dotNormalHalf,
            materialData.diffuseAndShininess.w);
    const float3 linearColor =
        diffuse + specular;
    return float4(
        webglGeometriesLinearToSrgb(linearColor.x),
        webglGeometriesLinearToSrgb(linearColor.y),
        webglGeometriesLinearToSrgb(linearColor.z),
        1.0f);
}

/** Draws the back-facing half of every double-sided MeshPhong entity. */
class WebglGeometriesBackPass final : public IRenderClass
{
public:
    /** Configures the first opaque double-sided phase. */
    constructor(
        RenderSet<WebglGeometriesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometriesSamplerResources>
            samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::Less);
    }

private:
    /** Transforms back faces and reverses their shading normal. */
    WebglGeometriesVertexOutput vertex(
        WebglGeometriesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometriesTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID,
            -1.0f);
    }

    /** Evaluates the shared Phong material for back faces. */
    WebglGeometriesFrameBuffer fragment(
        WebglGeometriesVertexOutput inputValue)
    {
        const float4 shaded =
            webglGeometriesShade(
                sceneSet,
                samplerResources,
                inputValue);
        WebglGeometriesFrameBuffer frameBuffer;
        frameBuffer.color = half4(shaded);
        return frameBuffer;
    }
};

/** Draws the front-facing half of every double-sided MeshPhong entity. */
class WebglGeometriesFrontPass final : public IRenderClass
{
public:
    /** Configures the second opaque double-sided phase. */
    constructor(
        RenderSet<WebglGeometriesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometriesSamplerResources>
            samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::Less);
    }

private:
    /** Transforms front faces with their original shading normal. */
    WebglGeometriesVertexOutput vertex(
        WebglGeometriesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometriesTransformVertex(
            sceneSet,
            inputValue,
            renderEntityID,
            renderEntityInstanceID,
            1.0f);
    }

    /** Evaluates the shared Phong material for front faces. */
    WebglGeometriesFrameBuffer fragment(
        WebglGeometriesVertexOutput inputValue)
    {
        const float4 shaded =
            webglGeometriesShade(
                sceneSet,
                samplerResources,
                inputValue);
        WebglGeometriesFrameBuffer frameBuffer;
        frameBuffer.color = half4(shaded);
        return frameBuffer;
    }
};

/** Owns the dedicated sixteen-entity Scene RenderSet and two geometry passes. */
class WebglGeometriesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometriesSceneRenderSet> sceneSet;
    Sampler textureSampler;
    BindGroup<WebglGeometriesSamplerResources> samplerResources;
    RenderClass<WebglGeometriesBackPass> backPass;
    RenderClass<WebglGeometriesFrontPass> frontPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates only the frozen DSL resources required by this example. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglGeometriesSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglGeometriesUvGridSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 16,
        });
        samplerResources =
            device->createBindGroup<WebglGeometriesSamplerResources>(
                textureSampler);
        backPass =
            device->createRenderClass<WebglGeometriesBackPass>(
                sceneSet,
                samplerResources);
        frontPass =
            device->createRenderClass<WebglGeometriesFrontPass>(
                sceneSet,
                samplerResources);
    }

    /** Allocates the ordinary 800 by 500 single-sample attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglGeometriesColor",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglGeometriesDepth",
            width,
            height,
            1u);
    }

    /** Renders both double-sided phases through the same Scene Set. */
    void render() override
    {
        sceneSet->update();
        WebglGeometriesFrameBuffer backFrame;
        backFrame.color = outputTexture->createView();
        backFrame.color.loadOp = LoadOp::Clear;
        backFrame.color.storeOp = StoreOp::Store;
        backFrame.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        backFrame.depth = depthTexture->createView();
        backFrame.depth.depthLoadOp = LoadOp::Clear;
        backFrame.depth.depthStoreOp = StoreOp::Store;
        backFrame.depth.depthClearValue = 1.0f;
        WebglGeometriesFrameBuffer frontFrame;
        frontFrame.color = outputTexture->createView();
        frontFrame.color.loadOp = LoadOp::Load;
        frontFrame.color.storeOp = StoreOp::Store;
        frontFrame.depth = depthTexture->createView();
        frontFrame.depth.depthLoadOp = LoadOp::Load;
        frontFrame.depth.depthStoreOp = StoreOp::Store;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometriesBack",
                backFrame,
                backPass())
            ->renderPass(
                "WebglGeometriesFront",
                frontFrame,
                frontPass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Exposes the final RGBA8 texture to the deterministic host. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Scene Set and the ordinary single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
