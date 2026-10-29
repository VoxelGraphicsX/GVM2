#ifndef GVM_THREE_WEBGLMATERIALSTOON_HPP
#define GVM_THREE_WEBGLMATERIALSTOON_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglMaterialsToonTextureCapacity = 2u;
static const float WebglMaterialsToonGradientAtlasWidth = 27.0f;

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebglMaterialsToonVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform, light state, and material phase. */
struct WebglMaterialsToonObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 baseColorAndFlags;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebglMaterialsToonInstanceData
{
    float4 reserved;
};

/** Stores one material color and wireframe phase. */
struct WebglMaterialsToonMaterialData
{
    float4 baseColorAndFlags;
    float4 gradientAndOutline;
    float4 gradientParams;
};

/** Supplies nearest sampling for each entity's one-row toon gradient texture. */
struct WebglMaterialsToonSamplerResources final : public IBindGroup
{
    /** Declares the sampler used by the DataTexture gradient maps. */
    constructor(Sampler gradientSampler [[Binding0]])
    {
    }
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebglMaterialsToonSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebglMaterialsToonVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMaterialsToonObjectData> objects,
        BufferComponent<WebglMaterialsToonInstanceData> instances,
        BufferComponent<WebglMaterialsToonMaterialData> materials,
        (TextureComponent<half4, WebglMaterialsToonTextureCapacity> textures))
    {
    }
};

/** Carries transformed position, normal, barycentric coordinates, and entity id. */
struct WebglMaterialsToonVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
    float3 localPosition [[Attribute3]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebglMaterialsToonFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webglMaterialsToonLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
        return clamped <= 0.0031308f
            ? clamped * 12.92f
            : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Draws opaque cone and target objects through the unique Scene RenderSet. */
class WebglMaterialsToonMainPass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet and gradient sampler for the toon phase. */
    constructor(RenderSet<WebglMaterialsToonSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglMaterialsToonSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity model-view transform and forwards the edge attributes. */
    WebglMaterialsToonVertexOutput vertex(
        WebglMaterialsToonVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMaterialsToonObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMaterialsToonInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMaterialsToonVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float3 localNormal = float3(inputValue.normalAndFlags.xyz);
        const float4 transformedNormal = mul(
            objectData.normalMatrix,
            float4(localNormal, 0.0f));
        outputValue.viewNormal = normalize(float3(transformedNormal.xyz));
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.localPosition = localPosition.xyz;
        return outputValue;
    }

    /** Shades MeshNormal and MeshBasic entities while rejecting wireframe geometry. */
    WebglMaterialsToonFrameBuffer fragment(
        WebglMaterialsToonVertexOutput inputValue)
    {
        const WebglMaterialsToonObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglMaterialsToonMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.baseColorAndFlags.w > 0.5f)
        {
            WebglMaterialsToonFrameBuffer basicFrame;
            basicFrame.color = half4(half3(materialData.baseColorAndFlags.xyz), half(1.0f));
            return basicFrame;
        }
        const float3 baseColor = materialData.baseColorAndFlags.xyz;
        // WebGL MeshToonMaterial evaluates the normalized view-space normal
        // against the view-space point-light direction.
        const float3 lightPositionView = materialData.gradientAndOutline.yzw;
        const float3 viewPosition = mul(
            objectData.modelView,
            float4(inputValue.localPosition, 1.0f)).xyz;
        const float3 toLight = lightPositionView - viewPosition;
        const float distanceToLight = max(length(toLight), 0.0001f);
        const float3 lightDirection = toLight / distanceToLight;
        // The locked reference uses the Toon point-light range attenuation.
        const float cutoff = clamp(
            1.0f - pow(distanceToLight / 800.0f, 4.0f), 0.0f, 1.0f);
        // The example explicitly uses PointLight.decay = 0.  Three's
        // getDistanceAttenuation therefore keeps only the finite-distance
        // cutoff term and does not apply inverse-square falloff.
        const float attenuation = cutoff * cutoff;
        const float dotNormalLight = dot(
            normalize(inputValue.viewNormal),
            lightDirection);
        const float gradientCoordinate = dotNormalLight * 0.5f + 0.5f;
        // Sample the per-entity RedFormat DataTexture with the same nearest
        // sampler contract used by the upstream MeshToonNodeMaterial.
        const float atlasOffset = materialData.gradientParams.x;
        const float gradientWidth = materialData.gradientParams.z;
        const float atlasCoordinate = (
            atlasOffset + min(clamp(gradientCoordinate, 0.0f, 1.0f), 0.999999f) * gradientWidth) /
            WebglMaterialsToonGradientAtlasWidth;
        const float gradient = float(sceneSet->textures->get(
            inputValue.entityID, 0u)->sample(
                samplerResources->gradientSampler,
                float2(atlasCoordinate, 0.5f)).x);
        // PointLight.color is already multiplied by its intensity in the
        // Three.js lighting node.  The Lambert term supplies the sole 1/pi
        // factor; do not apply an additional display-space calibration here.
        const float directScale = materialData.gradientParams.y * gradient * attenuation /
            3.14159265358979323846f;
        // AmbientLight(0xc1c1c1, 3) contributes through ToonLightingModel's
        // indirect Lambert path.  The host stores the linear irradiance in x.
        const float indirectScale = materialData.gradientAndOutline.x /
            3.14159265358979323846f;
        const float3 normalColor = baseColor * (directScale + indirectScale);
        const float3 srgb = float3(
            webglMaterialsToonLinearToSrgb(normalColor.x),
            webglMaterialsToonLinearToSrgb(normalColor.y),
            webglMaterialsToonLinearToSrgb(normalColor.z));
        WebglMaterialsToonFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the transparent wireframe control sphere from the same RenderSet. */
class WebglMaterialsToonOutlinePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and preserves opaque color/depth. */
    constructor(RenderSet<WebglMaterialsToonSceneRenderSet> sceneSet [[Slot0]])
    {
        // toonOutlinePass expands the back-facing sphere along its projected
        // normal.  The pass remains single-sample and reuses the Scene Set.
        setCullMode(CullMode::Front);
        // The upstream BackSide outline material keeps the default depth write
        // enabled.  This lets the subsequent front-side toon pass depth-test
        // over the shell while preserving correct occlusion between objects.
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reuses the exact object/instance transform path of the opaque pass. */
    WebglMaterialsToonVertexOutput vertex(
        WebglMaterialsToonVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMaterialsToonObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglMaterialsToonInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglMaterialsToonVertexOutput outputValue;
        float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
        const float3 localNormal = float3(inputValue.normalAndFlags.xyz);
        const float3 normalizedLocalNormal = normalize(localNormal);
        float4 clipNormalPosition = mul(objectData.modelViewProjection,
            localPosition - float4(normalizedLocalNormal, 0.0f));
        clipPosition.y = -clipPosition.y;
        clipNormalPosition.y = -clipNormalPosition.y;
        const float4 clipDelta = clipPosition - clipNormalPosition;
        const float4 outlineDirection = normalize(clipDelta);
        outputValue.position = clipPosition + outlineDirection * (0.003f * clipPosition.w);
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = float3(0.0f);
        outputValue.entityID = renderEntityID;
        outputValue.localPosition = float3(0.0f);
        return outputValue;
    }

    /** Emits the opaque black silhouette used for every Scene mesh. */
    WebglMaterialsToonFrameBuffer fragment(
        WebglMaterialsToonVertexOutput inputValue)
    {
        // OutlineEffect replaces every visible source material, including the
        // four MeshBasic text labels and the point-light marker.
        WebglMaterialsToonFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
class WebglMaterialsToonRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsToonSceneRenderSet> sceneSet;
    Sampler gradientSampler;
    BindGroup<WebglMaterialsToonSamplerResources> samplerResources;
    RenderClass<WebglMaterialsToonMainPass> mainPass;
    RenderClass<WebglMaterialsToonOutlinePass> outlinePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates this example's unique RenderSet and both generated RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMaterialsToonSceneRenderSet>();
        gradientSampler = device->createSampler({
            .label = "WebglMaterialsToonGradientSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglMaterialsToonSamplerResources>(gradientSampler);
        mainPass = device->createRenderClass<WebglMaterialsToonMainPass>(sceneSet, samplerResources);
        outlinePass = device->createRenderClass<WebglMaterialsToonOutlinePass>(sceneSet);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMaterialsToonColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMaterialsToonDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebglMaterialsToonFrameBuffer outlineFrame;
        outlineFrame.color = outputColor->createView();
        outlineFrame.color.loadOp = LoadOp::Clear;
        outlineFrame.color.storeOp = StoreOp::Store;
        outlineFrame.color.clearValue = {0.2666667f, 0.2666667f, 0.5333333f, 1.0f};
        outlineFrame.depth = outputDepth->createView();
        outlineFrame.depth.depthLoadOp = LoadOp::Clear;
        outlineFrame.depth.depthStoreOp = StoreOp::Store;
        outlineFrame.depth.depthClearValue = 1.0f;
        WebglMaterialsToonFrameBuffer mainFrame;
        mainFrame.color = outputColor->createView();
        mainFrame.color.loadOp = LoadOp::Load;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.depth = outputDepth->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Load;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            // OutlineEffect renders the back-side shell first; the
            // original toon material then fills the front-facing surface and
            // depth-tests over the shell.  Keep the pass order identical to
            // Three.js so the outline is visible only at the silhouette.
            ->renderPass("WebglMaterialsToonOutline", outlineFrame, outlinePass())
            ->renderPass("WebglMaterialsToonMain", mainFrame, mainPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used by deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and single-sample targets. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
