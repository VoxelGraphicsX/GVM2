#ifndef GVM_THREE_WEBGPULIGHTSSELECTIVE_HPP
#define GVM_THREE_WEBGPULIGHTSSELECTIVE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the selected light mask and intensity independent of material color. */
struct WebgpuLightsSelectiveLightData
{
    float4 maskAndIntensity;
};

/** Stores visibility and selection phase flags for the Scene redraws. */
struct WebgpuLightsSelectiveRenderFlags
{
    uint4 values;
};

/** Stores the union of position, normal, and barycentric edge attributes. */
struct WebgpuLightsSelectiveVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores one entity camera transform and the four animated point lights. */
struct WebgpuLightsSelectiveObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4x4 normalMatrix;
    float4 cameraPosition;
    float4x4 viewNormalMatrix;
    // xyz is the light position in view space; w is the PointLight intensity in candela.
    float4 lightPositionPower0;
    float4 lightPositionPower1;
    float4 lightPositionPower2;
    float4 lightPositionPower3;
    float4 lightColor0;
    float4 lightColor1;
    float4 lightColor2;
    float4 lightColor3;
};

/** Stores the mandatory one-entry instance component for each object. */
struct WebgpuLightsSelectiveInstanceData
{
    float4 reserved;
};

/** Stores one StandardNodeMaterial tint and selective-light controls. */
struct WebgpuLightsSelectiveMaterialData
{
    float4 baseColorAndFlags;
    float4 lightMaskRoughnessMetalnessAndPhase;
};

/** Binds the repeatable normal/roughness maps through one linear sampler. */
struct WebgpuLightsSelectiveSamplerResources final : public IBindGroup
{
    /** Declares the material and exact r185 DFG samplers. */
    constructor(
        Sampler materialSampler [[Binding0]],
        Sampler dfgSampler [[Binding1]])
    {
    }
};

/** Defines the only RenderSet used by the orientation-transform Scene. */
struct WebgpuLightsSelectiveSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity transform/material components. */
    constructor(
        BufferComponent<WebgpuLightsSelectiveVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuLightsSelectiveObjectData> objects,
        BufferComponent<WebgpuLightsSelectiveInstanceData> instances,
        BufferComponent<WebgpuLightsSelectiveMaterialData> materials,
        BufferComponent<WebgpuLightsSelectiveLightData> lightData,
        BufferComponent<WebgpuLightsSelectiveRenderFlags> renderFlags,
        (TextureComponent<half4, 3u> textures))
    {
    }
};

/** Carries view-space PBR inputs and entity identity to the fragment stage. */
struct WebgpuLightsSelectiveVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
    float viewDepth [[Attribute4]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct WebgpuLightsSelectiveFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the Three canvas sRGB transfer function. */
float webgpuLightsSelectiveLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Evaluates the direct GGX term used by the selective Standard materials. */
float3 webgpuLightsSelectiveGgx(
    float3 normal,
    float3 lightDirection,
    float3 viewDirection,
    float3 f0,
    float roughness)
{
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float normalDotView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float normalDotHalf = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float viewDotHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared /
        max(3.14159265359f * denominator * denominator, 0.000001f);
    const float visibilityLight = normalDotLight * sqrt(
        alphaSquared + (1.0f - alphaSquared) * normalDotView * normalDotView);
    const float visibilityView = normalDotView * sqrt(
        alphaSquared + (1.0f - alphaSquared) * normalDotLight * normalDotLight);
    const float visibility = 0.5f /
        max(visibilityLight + visibilityView, 0.000001f);
    const float fresnelWeight = exp2(
        (-5.55473f * viewDotHalf - 6.98316f) * viewDotHalf);
    const float3 fresnel = f0 * (1.0f - fresnelWeight) +
        float3(fresnelWeight);
    return fresnel * visibility * distribution;
}

/** Adds one selected point-light contribution to a Standard material. */
float3 webgpuLightsSelectivePointLight(
    float3 viewPosition,
    float3 normal,
    float3 viewDirection,
    float3 diffuseColor,
    float3 f0,
    float roughness,
    float2 dfgView,
    float2 dfgLight,
    uint selectedMask,
    uint bit,
    float4 lightPositionPower,
    float4 lightColor)
{
    if ((selectedMask & bit) == 0u)
    {
        return float3(0.0f);
    }
    const float3 lightVector = lightPositionPower.xyz - viewPosition;
    const float distanceSquared = max(dot(lightVector, lightVector), 0.0001f);
    const float distanceValue = sqrt(distanceSquared);
    const float3 lightDirection = lightVector / distanceValue;
    const float normalDotLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    // PointLight( ..., 1, 100 ) is assigned power=1700 by the example. Three
    // stores that as intensity=power/(4*pi), while the cutoff remains 100.
    const float cutoff = clamp(
        1.0f - pow(distanceValue / 100.0f, 4.0f), 0.0f, 1.0f);
    const float attenuation = cutoff * cutoff / distanceSquared;
    const float3 irradiance = lightColor.xyz *
        (lightPositionPower.w * attenuation * normalDotLight);
    const float3 singleScatter = webgpuLightsSelectiveGgx(
        normal, lightDirection, viewDirection, f0, roughness);
    const float3 fssEssView = f0 * dfgView.x + float3(dfgView.y);
    const float3 fssEssLight = f0 * dfgLight.x + float3(dfgLight.y);
    const float emsView = 1.0f - dfgView.x - dfgView.y;
    const float emsLight = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel = f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multipleScatter = fssEssView * fssEssLight * averageFresnel /
        (float3(1.0f) - emsView * emsLight * averageFresnel * averageFresnel +
            float3(0.000001f)) * (emsView * emsLight);
    return irradiance * (diffuseColor * 0.3183098862f +
        singleScatter + multipleScatter);
}

/** Draws the three teapots and four light markers through the unique Scene RenderSet. */
class WebgpuLightsSelectiveMainPass final : public IRenderClass
{
public:
    /** Binds the Scene RenderSet and the locked normal/roughness sampler. */
    constructor(RenderSet<WebgpuLightsSelectiveSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebgpuLightsSelectiveSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity transform and forwards view-space PBR attributes. */
    WebgpuLightsSelectiveVertexOutput vertex(
        WebgpuLightsSelectiveVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuLightsSelectiveObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuLightsSelectiveInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebgpuLightsSelectiveVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        const float4 viewPosition = mul(objectData.modelView, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float3 transformedNormal = mul(objectData.viewNormalMatrix, float4(inputValue.normalAndFlags.xyz, 0.0f)).xyz;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.uv = inputValue.barycentric.xy;
        outputValue.entityID = renderEntityID;
        // Three.js range fog uses positive camera-space depth, not Euclidean
        // world distance. Preserve that semantic before interpolation.
        outputValue.viewDepth = -viewPosition.z;
        return outputValue;
    }

    /** Shades selective Standard materials, marker meshes, and magenta range fog. */
    WebgpuLightsSelectiveFrameBuffer fragment(
        WebgpuLightsSelectiveVertexOutput inputValue)
    {
        const WebgpuLightsSelectiveObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuLightsSelectiveMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 baseColor = materialData.baseColorAndFlags.xyz;
        float3 linearColor;
        if (inputValue.entityID >= 3u)
        {
            linearColor = baseColor;
        }
        else
        {
            const float3 geometryNormal = inputValue.viewNormal;
            // Match Three.js TBNViewMatrix's derivative-frame construction for
            // tangent-space normal maps without requiring a tangent attribute.
            const float2 normalMapUv = inputValue.uv;
            const float3 q0 = ddx(inputValue.viewPosition);
            const float3 q1 = ddy(inputValue.viewPosition);
            const float2 st0 = ddx(normalMapUv);
            const float2 st1 = ddy(normalMapUv);
            const float3 q1Perpendicular = cross(q1, geometryNormal);
            const float3 q0Perpendicular = cross(geometryNormal, q0);
            const float3 tangentFrame = q1Perpendicular * st0.x +
                q0Perpendicular * st1.x;
            const float3 bitangentFrame = q1Perpendicular * st0.y +
                q0Perpendicular * st1.y;
            const float frameDeterminant = max(dot(tangentFrame, tangentFrame),
                dot(bitangentFrame, bitangentFrame));
            const float frameScale = frameDeterminant > 0.0f
                ? rsqrt(frameDeterminant)
                : 0.0f;
            const float3 tangent = tangentFrame * frameScale;
            const float3 bitangent = bitangentFrame * frameScale;
            const float2 scalarMapUv = inputValue.uv;
            const float normalVariant = materialData.baseColorAndFlags.w;
            const bool explicitNormalLod = normalVariant > 7.5f;
            const bool noTextureYFlip = explicitNormalLod || normalVariant > 4.5f;
            const bool flipNormalX =
                explicitNormalLod ||
                (normalVariant > 1.5f && normalVariant < 3.5f) ||
                (normalVariant > 5.5f && normalVariant < 7.5f);
            const bool flipNormalY =
                explicitNormalLod ||
                (normalVariant > 2.5f && normalVariant < 4.5f) ||
                (normalVariant > 6.5f);
            const float2 materialUv = noTextureYFlip
                ? inputValue.uv
                : float2(inputValue.uv.x, 1.0f - inputValue.uv.y);
            const float normalLod = max(normalVariant - 8.0f, 0.0f);
            const float4 normalTexel = explicitNormalLod
                ? float4(sceneSet->textures->get(inputValue.entityID, 0u)
                    ->sampleLevel(resources->materialSampler, materialUv, normalLod))
                : float4(sceneSet->textures->get(inputValue.entityID, 0u)
                    ->sample(resources->materialSampler, materialUv));
            const float3 sampledNormal = float3(
                (flipNormalX ? 1.0f - normalTexel.x : normalTexel.x) * 2.0f - 1.0f,
                (flipNormalY ? 1.0f - normalTexel.y : normalTexel.y) * 2.0f - 1.0f,
                normalTexel.z * 2.0f - 1.0f);
            const float3 normal = materialData.lightMaskRoughnessMetalnessAndPhase.w > 0.5f
                ? normalize(tangent * sampledNormal.x +
                    bitangent * sampledNormal.y + geometryNormal * sampledNormal.z)
                : geometryNormal;
            auto scalarTexture = sceneSet->textures->get(inputValue.entityID, 1u);
            // Both StandardNodeMaterial map nodes use the implicit-derivative
            // sampler.  The right teapot binds the same texture as
            // metalness, so it must not take a separate explicit-LOD path.
            const float4 roughnessTexel = float4(scalarTexture->sample(
                resources->materialSampler, scalarMapUv));
            // The left teapot binds roughness_map as roughness, the center
            // keeps its scalar .5, and the right binds the same map as
            // metalness while retaining the scalar roughness .5.
            const float baseRoughness = inputValue.entityID == 1u ||
                    inputValue.entityID == 2u
                ? clamp(materialData.lightMaskRoughnessMetalnessAndPhase.y, 0.0525f, 1.0f)
                : clamp(roughnessTexel.x, 0.0525f, 1.0f);
            const float3 geometryNormalDerivative = max(
                abs(ddx(geometryNormal)),
                abs(ddy(geometryNormal)));
            const float geometryRoughness = max(
                max(geometryNormalDerivative.x, geometryNormalDerivative.y),
                geometryNormalDerivative.z);
            const float roughness = clamp(
                baseRoughness + geometryRoughness,
                0.0525f,
                1.0f);
            const float metalness = inputValue.entityID == 1u
                ? clamp(materialData.lightMaskRoughnessMetalnessAndPhase.z, 0.0f, 1.0f)
                : inputValue.entityID == 2u
                    ? clamp(roughnessTexel.x, 0.0f, 1.0f)
                    : 0.0f;
            const float3 viewDirection = normalize(-inputValue.viewPosition);
            const float3 f0 = lerp(float3(0.04f), baseColor, metalness);
            const float3 diffuseColor = baseColor * (1.0f - metalness);
            const uint selectedMask = uint(
                materialData.lightMaskRoughnessMetalnessAndPhase.x + 0.5f);
            const float dotViewNormal = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
            const float2 dfgView = float4(sceneSet->textures->get(inputValue.entityID, 2u)
                ->sample(resources->dfgSampler, float2(roughness, dotViewNormal))).xy;
            const float dotLight0 = clamp(dot(normal, normalize(
                objectData.lightPositionPower0.xyz - inputValue.viewPosition)), 0.0f, 1.0f);
            const float dotLight1 = clamp(dot(normal, normalize(
                objectData.lightPositionPower1.xyz - inputValue.viewPosition)), 0.0f, 1.0f);
            const float dotLight2 = clamp(dot(normal, normalize(
                objectData.lightPositionPower2.xyz - inputValue.viewPosition)), 0.0f, 1.0f);
            const float dotLight3 = clamp(dot(normal, normalize(
                objectData.lightPositionPower3.xyz - inputValue.viewPosition)), 0.0f, 1.0f);
            const float2 dfgLight0 = float4(sceneSet->textures->get(inputValue.entityID, 2u)
                ->sample(resources->dfgSampler, float2(roughness, dotLight0))).xy;
            const float2 dfgLight1 = float4(sceneSet->textures->get(inputValue.entityID, 2u)
                ->sample(resources->dfgSampler, float2(roughness, dotLight1))).xy;
            const float2 dfgLight2 = float4(sceneSet->textures->get(inputValue.entityID, 2u)
                ->sample(resources->dfgSampler, float2(roughness, dotLight2))).xy;
            const float2 dfgLight3 = float4(sceneSet->textures->get(inputValue.entityID, 2u)
                ->sample(resources->dfgSampler, float2(roughness, dotLight3))).xy;
            // MeshStandardNodeMaterial has no ambient light in this example;
            // unlit fragments remain black until a selected point light reaches them.
            linearColor = float3(0.0f);
            linearColor += webgpuLightsSelectivePointLight(inputValue.viewPosition, normal,
                viewDirection, diffuseColor, f0, roughness, dfgView, dfgLight0, selectedMask, 1u,
                objectData.lightPositionPower0, objectData.lightColor0);
            linearColor += webgpuLightsSelectivePointLight(inputValue.viewPosition, normal,
                viewDirection, diffuseColor, f0, roughness, dfgView, dfgLight1, selectedMask, 2u,
                objectData.lightPositionPower1, objectData.lightColor1);
            linearColor += webgpuLightsSelectivePointLight(inputValue.viewPosition, normal,
                viewDirection, diffuseColor, f0, roughness, dfgView, dfgLight2, selectedMask, 4u,
                objectData.lightPositionPower2, objectData.lightColor2);
            linearColor += webgpuLightsSelectivePointLight(inputValue.viewPosition, normal,
                viewDirection, diffuseColor, f0, roughness, dfgView, dfgLight3, selectedMask, 8u,
                objectData.lightPositionPower3, objectData.lightColor3);
        }
        const float fogFactor = smoothstep(12.0f, 30.0f, inputValue.viewDepth);
        linearColor = lerp(linearColor, float3(1.0f, 0.0f, 1.0f), fogFactor);
        const float3 srgb = float3(
            webgpuLightsSelectiveLinearToSrgb(max(linearColor.x, 0.0f)),
            webgpuLightsSelectiveLinearToSrgb(max(linearColor.y, 0.0f)),
            webgpuLightsSelectiveLinearToSrgb(max(linearColor.z, 0.0f)));
        WebgpuLightsSelectiveFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the two geometry passes. */
class WebgpuLightsSelectiveRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuLightsSelectiveSceneRenderSet> sceneSet;
    Sampler materialSampler;
    Sampler dfgSampler;
    BindGroup<WebgpuLightsSelectiveSamplerResources> samplerResources;
    RenderClass<WebgpuLightsSelectiveMainPass> mainPass;
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
        sceneSet = device->createRenderSet<WebgpuLightsSelectiveSceneRenderSet>();
        materialSampler = device->createSampler({
            .label = "WebgpuLightsSelectiveMaterialSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        dfgSampler = device->createSampler({
            .label = "WebgpuLightsSelectiveDfgSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebgpuLightsSelectiveSamplerResources>(
            materialSampler, dfgSampler);
        mainPass = device->createRenderClass<WebgpuLightsSelectiveMainPass>(sceneSet, samplerResources);
    }

    /** Allocates the explicit single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuLightsSelectiveColor", width, height, 1u);
        outputDepth = device->createTexture("WebgpuLightsSelectiveDepth", width, height, 1u);
    }

    /** Submits opaque then transparent geometry while reusing the same Set. */
    void render() override
    {
        sceneSet->update();
        WebgpuLightsSelectiveFrameBuffer opaqueFrame;
        opaqueFrame.color = outputColor->createView();
        opaqueFrame.color.loadOp = LoadOp::Clear;
        opaqueFrame.color.storeOp = StoreOp::Store;
        opaqueFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        opaqueFrame.depth = outputDepth->createView();
        opaqueFrame.depth.depthLoadOp = LoadOp::Clear;
        opaqueFrame.depth.depthStoreOp = StoreOp::Store;
        opaqueFrame.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuLightsSelectiveMain", opaqueFrame, mainPass())
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
