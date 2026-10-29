#ifndef GVM_THREE_WEBGPU_SKINNING_HPP
#define GVM_THREE_WEBGPU_SKINNING_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuSkinningMaxTextures = 4u;

/** Stores the complete Michelle vertex attribute union. */
struct WebgpuSkinningVertex
{
    float4 clipPosition [[Attribute0]];
    float4 worldPosition [[Attribute1]];
    float4 worldNormal [[Attribute2]];
    float4 uv [[Attribute3]];
};

/** Stores the frozen camera position and linear exposure. */
struct WebgpuSkinningObjectData
{
    float4 cameraPositionAndExposure;
};

/** Stores the mandatory non-instanced entity component. */
struct WebgpuSkinningInstanceData
{
    float4 reserved;
};

/** Stores the pinned glTF physical-material and light parameters. */
struct WebgpuSkinningMaterialData
{
    float4 ambientColorAndPointIntensity;
    float4 pointPositionAndIor;
    float4 metallicDistanceAndReserved;
};

/** Defines the unique RenderSet owned by the Michelle Scene. */
struct WebgpuSkinningSceneRenderSet : public IRenderSet
{
    /** Declares geometry, dynamic vertices, material, and four texture slots. */
    constructor(
        BufferComponent<WebgpuSkinningVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuSkinningObjectData> objects,
        BufferComponent<WebgpuSkinningInstanceData> instances,
        BufferComponent<WebgpuSkinningMaterialData> materials,
        (TextureComponent<half4, WebgpuSkinningMaxTextures> textures))
    {
    }
};

/** Binds the repeat sampler used by Michelle's four material textures. */
struct WebgpuSkinningSamplerResources final : public IBindGroup
{
    /** Declares the immutable trilinear repeat sampler. */
    constructor(Sampler materialSampler [[Binding0]])
    {
    }
};

/** Carries the skinned world attributes into physical shading. */
struct WebgpuSkinningVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the single-sample Michelle color and depth attachments. */
struct WebgpuSkinningSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the gradient-only background attachment. */
struct WebgpuSkinningBackgroundFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries one fullscreen background position. */
struct WebgpuSkinningBackgroundVertexOutput
{
    float4 position [[Position]];
};

/** Converts one linear-light channel to the browser output transfer. */
float webgpuSkinningLinearToSrgb(float value)
{
    const float bounded = clamp(value, 0.0f, 1.0f);
    return bounded <= 0.0031308f
        ? bounded * 12.92f
        : pow(bounded, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webgpuSkinningFresnel(
    float3 f0,
    float viewDotHalf)
{
    const float fresnel = exp2(
        (-5.55473f * viewDotHalf - 6.98316f) * viewDotHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Evaluates the r185 direct-light GGX BRDF. */
float3 webgpuSkinningGgx(
    float3 normal,
    float3 lightDirection,
    float3 viewDirection,
    float3 f0,
    float roughness)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float normalDotHalf = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float viewDotHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    const float distribution =
        alphaSquared /
        max(3.14159265359f * denominator * denominator, 0.000001f);
    const float visibilityLight =
        normalDotLight * sqrt(alphaSquared +
            (1.0f - alphaSquared) * normalDotView * normalDotView);
    const float visibilityView =
        normalDotView * sqrt(alphaSquared +
            (1.0f - alphaSquared) * normalDotLight * normalDotLight);
    const float visibility =
        0.5f / max(visibilityLight + visibilityView, 0.000001f);
    return webgpuSkinningFresnel(f0, viewDotHalf) *
        distribution * visibility;
}

/** Applies the frozen camera transform to CPU-evaluated skin vertices. */
WebgpuSkinningVertexOutput webgpuSkinningTransform(
    IN RenderSet<WebgpuSkinningSceneRenderSet> sceneSet,
    WebgpuSkinningVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebgpuSkinningInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    WebgpuSkinningVertexOutput outputValue;
    outputValue.position = inputValue.clipPosition +
        float4(instanceData.reserved.xyz, 0.0f);
    outputValue.worldPosition = inputValue.worldPosition.xyz;
    outputValue.worldNormal = normalize(float3(inputValue.worldNormal.xyz));
    outputValue.uv = inputValue.uv.xy;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Shades one Michelle face with her four glTF material textures. */
half4 webgpuSkinningShade(
    IN RenderSet<WebgpuSkinningSceneRenderSet> sceneSet,
    IN BindGroup<WebgpuSkinningSamplerResources> resources,
    WebgpuSkinningVertexOutput inputValue,
    float faceSign)
{
    const WebgpuSkinningObjectData objectData =
        sceneSet->objects->get(inputValue.entityID, 0u);
    const WebgpuSkinningMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    const float2 textureUv = float2(inputValue.uv.x, 1.0f - inputValue.uv.y);
    const float4 baseTexel = float4(sceneSet->textures
        ->get(inputValue.entityID, 2u)
        ->sample(resources->materialSampler, textureUv));
    const float4 normalTexel = float4(sceneSet->textures
        ->get(inputValue.entityID, 1u)
        ->sample(resources->materialSampler, textureUv));
    const float4 metallicRoughness = float4(sceneSet->textures
        ->get(inputValue.entityID, 3u)
        ->sample(resources->materialSampler, textureUv));
    const float3 specularTexture = float3(sceneSet->textures
        ->get(inputValue.entityID, 0u)
        ->sample(resources->materialSampler, textureUv).xyz);
    const float3 geometryNormal = normalize(inputValue.worldNormal) * faceSign;
    const float3 positionDx = ddx(inputValue.worldPosition);
    const float3 positionDy = ddy(inputValue.worldPosition);
    const float2 uvDx = ddx(textureUv);
    const float2 uvDy = ddy(textureUv);
    const float3 tangent = normalize(positionDx * uvDy.y - positionDy * uvDx.y);
    const float3 bitangent = normalize(-positionDx * uvDy.x + positionDy * uvDx.x);
    const float3 sampledNormal = float3(normalTexel.xyz) * 2.0f - float3(1.0f);
    const float3 normal = normalize(
        tangent * sampledNormal.x +
        bitangent * sampledNormal.y +
        geometryNormal * sampledNormal.z);
    const float metallic = clamp(
        materialData.metallicDistanceAndReserved.x * metallicRoughness.z,
        0.0f, 1.0f);
    const float roughness = clamp(metallicRoughness.y, 0.0525f, 1.0f);
    const float3 diffuseColor = float3(baseTexel.xyz) * (1.0f - metallic);
    const float ior = materialData.pointPositionAndIor.w;
    const float dielectric =
        (ior - 1.0f) * (ior - 1.0f) /
        ((ior + 1.0f) * (ior + 1.0f));
    const float3 f0 = lerp(
        dielectric * specularTexture,
        float3(baseTexel.xyz),
        metallic);
    const float3 lightVector =
        materialData.pointPositionAndIor.xyz - inputValue.worldPosition;
    const float lightDistanceSquared = max(dot(lightVector, lightVector), 0.0001f);
    const float lightDistance = sqrt(lightDistanceSquared);
    const float3 lightDirection = lightVector / lightDistance;
    const float3 viewDirection = normalize(
        objectData.cameraPositionAndExposure.xyz - inputValue.worldPosition);
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float cutoff = clamp(
        1.0f - pow(lightDistance /
            materialData.metallicDistanceAndReserved.y, 4.0f),
        0.0f, 1.0f);
    const float pointIrradiance =
        materialData.ambientColorAndPointIntensity.w /
        lightDistanceSquared * cutoff * cutoff;
    const float3 directDiffuse =
        diffuseColor * pointIrradiance * normalDotLight * 0.31830988618f;
    const float3 directSpecular =
        pointIrradiance * normalDotLight * webgpuSkinningGgx(
            normal, lightDirection, viewDirection, f0, roughness);
    const float3 ambientDiffuse =
        diffuseColor * materialData.ambientColorAndPointIntensity.xyz;
    const float3 linearColor =
        (directDiffuse + directSpecular + ambientDiffuse) *
        objectData.cameraPositionAndExposure.w;
    return half4(
        half(webgpuSkinningLinearToSrgb(linearColor.x)),
        half(webgpuSkinningLinearToSrgb(linearColor.y)),
        half(webgpuSkinningLinearToSrgb(linearColor.z)),
        half(1.0f));
}

/** Renders the first back-facing phase of Michelle's double-sided material. */
class WebgpuSkinningBackPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and configures the back-face phase. */
    constructor(
        RenderSet<WebgpuSkinningSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSkinningSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads the CPU-evaluated pose from the RenderSet vertex component. */
    WebgpuSkinningVertexOutput vertex(
        WebgpuSkinningVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuSkinningTransform(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Shades one back-facing fragment with its reversed geometric normal. */
    WebgpuSkinningSceneFrameBuffer fragment(
        WebgpuSkinningVertexOutput inputValue)
    {
        WebgpuSkinningSceneFrameBuffer frameBuffer;
        frameBuffer.color = webgpuSkinningShade(
            sceneSet, resources, inputValue, -1.0f);
        return frameBuffer;
    }
};

/** Renders the second front-facing phase of Michelle's material. */
class WebgpuSkinningFrontPass final : public IRenderClass
{
public:
    /** Binds the same unique Scene Set and preserves its depth attachment. */
    constructor(
        RenderSet<WebgpuSkinningSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSkinningSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads the CPU-evaluated pose from the RenderSet vertex component. */
    WebgpuSkinningVertexOutput vertex(
        WebgpuSkinningVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuSkinningTransform(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Shades one front-facing fragment with its preserved normal. */
    WebgpuSkinningSceneFrameBuffer fragment(
        WebgpuSkinningVertexOutput inputValue)
    {
        WebgpuSkinningSceneFrameBuffer frameBuffer;
        frameBuffer.color = webgpuSkinningShade(
            sceneSet, resources, inputValue, 1.0f);
        return frameBuffer;
    }
};

/** Draws the exact linear-tone-mapped blue screen gradient. */
class WebgpuSkinningBackgroundPass final : public IRenderClass
{
public:
    /** Configures one geometry-free fullscreen triangle. */
    constructor()
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuSkinningBackgroundVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuSkinningBackgroundVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Evaluates the r185 screenUV color mix and exposure 0.4. */
    WebgpuSkinningBackgroundFrameBuffer fragment(
        WebgpuSkinningBackgroundVertexOutput inputValue)
    {
        const float ratio = clamp(inputValue.position.y / 499.0f, 0.0f, 1.0f);
        const float3 topLinear = float3(
            0.13286832f, 0.496933f, 1.0f);
        const float3 bottomLinear = float3(
            0.05780543f, 0.13286832f, 1.0f);
        const float3 linearColor = lerp(topLinear, bottomLinear, ratio) * 0.4f;
        WebgpuSkinningBackgroundFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuSkinningLinearToSrgb(linearColor.x)),
            half(webgpuSkinningLinearToSrgb(linearColor.y)),
            half(webgpuSkinningLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Michelle Set and single-sample output. */
class WebgpuSkinningRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSkinningSceneRenderSet> sceneSet;
    RenderClass<WebgpuSkinningBackPass> backPass;
    RenderClass<WebgpuSkinningFrontPass> frontPass;
    RenderClass<WebgpuSkinningBackgroundPass> backgroundPass;
    Sampler materialSampler;
    BindGroup<WebgpuSkinningSamplerResources> samplerResources;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;

public:
    /** Creates the unique Set, sampler, and all dedicated passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuSkinningSceneRenderSet>();
        materialSampler = device->createSampler({
            .label = "WebgpuSkinningMaterialSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 9.0f,
            .maxAnisotropy = 1u,
        });
        samplerResources = device->createBindGroup<WebgpuSkinningSamplerResources>(
            materialSampler);
        backPass = device->createRenderClass<WebgpuSkinningBackPass>(
            sceneSet, samplerResources);
        frontPass = device->createRenderClass<WebgpuSkinningFrontPass>(
            sceneSet, samplerResources);
        backgroundPass = device->createRenderClass<WebgpuSkinningBackgroundPass>();
    }

    /** Allocates the fixed single-sample output attachments. */
    void configureOutput(uint width, uint height)
    {
        outputColor = device->createTexture(
            "WebgpuSkinningColor", width, height, 1u);
        outputDepth = device->createTexture(
            "WebgpuSkinningDepth", width, height, 1u);
    }

    /** Updates the Scene Set, draws both face phases, and presents RGBA8. */
    void render() override
    {
        sceneSet->update();
        WebgpuSkinningBackgroundFrameBuffer backgroundFrame;
        backgroundFrame.color = outputColor->createView();
        backgroundFrame.color.loadOp = LoadOp::Clear;
        backgroundFrame.color.storeOp = StoreOp::Store;
        WebgpuSkinningSceneFrameBuffer firstSceneFrame;
        firstSceneFrame.color = outputColor->createView();
        firstSceneFrame.color.loadOp = LoadOp::Load;
        firstSceneFrame.color.storeOp = StoreOp::Store;
        firstSceneFrame.depth = outputDepth->createView();
        firstSceneFrame.depth.depthLoadOp = LoadOp::Clear;
        firstSceneFrame.depth.depthStoreOp = StoreOp::Store;
        firstSceneFrame.depth.depthClearValue = 1.0f;
        WebgpuSkinningSceneFrameBuffer secondSceneFrame;
        secondSceneFrame.color = outputColor->createView();
        secondSceneFrame.color.loadOp = LoadOp::Load;
        secondSceneFrame.color.storeOp = StoreOp::Store;
        secondSceneFrame.depth = outputDepth->createView();
        secondSceneFrame.depth.depthLoadOp = LoadOp::Load;
        secondSceneFrame.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuSkinningBackground", backgroundFrame,
                         backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuSkinningBack", firstSceneFrame, backPass())
            ->renderPass("WebgpuSkinningFront", secondSceneFrame, frontPass())
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final texture for host readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the fixed readback width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the fixed readback height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases the Set and dedicated output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
