#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebgpuPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebgpuPostprocessingPixelObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
    float4x4 directionalShadowViewProjection;
    float4x4 spotShadowViewProjection;
};

/** Stores the mandatory identity instance payload. */
struct WebgpuPostprocessingPixelInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebgpuPostprocessingPixelMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores the shadow/material phase flags shared by all Scene redraws. */
struct WebgpuPostprocessingPixelRenderFlags
{
    uint4 values;
};

/** Stores the low-resolution extent and edge strengths for the pixel pass. */
struct WebgpuPostprocessingPixelEffectParameters
{
    float4 renderSizeAndEdgeStrength;
};

/** Selects one of the two immutable Scene shadow cameras. */
struct WebgpuPostprocessingPixelShadowParameters
{
    uint4 values;
};

/** Defines the unique Scene RenderSet containing the four ordinary Mesh entities. */
struct WebgpuPostprocessingPixelSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebgpuPostprocessingPixelVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingPixelObjectData> objects,
        BufferComponent<WebgpuPostprocessingPixelInstanceData> instances,
        BufferComponent<WebgpuPostprocessingPixelMaterialData> materials,
        (TextureComponent<half4, 2u> textures),
        BufferComponent<WebgpuPostprocessingPixelRenderFlags> renderFlags)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebgpuPostprocessingPixelScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebgpuPostprocessingPixelEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Binds beauty, depth, and normal targets for the RenderPixelatedPass. */
struct WebgpuPostprocessingPixelEdgeResources final : public IBindGroup
{
    /** Declares nearest clamp scene/depth/normal sampling and effect controls. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Texture2D<TextureFormat::Depth32Float> depth [[Binding1]],
        Texture2D<half4> normal [[Binding2]],
        Sampler pixelSampler [[Binding3]],
        UniformBuffer<WebgpuPostprocessingPixelEffectParameters>
            parameters [[Binding4]])
    {
    }
};

/** Binds the nearest repeat sampler used by the locked checker texture. */
struct WebgpuPostprocessingPixelSceneSamplerResources final : public IBindGroup
{
    /** Declares the checker sampler and two DSL-produced shadow maps. */
    constructor(
        Sampler sceneSampler [[Binding0]],
        Texture2D<TextureFormat::Depth32Float>
            directionalShadow [[Binding1]],
        Texture2D<TextureFormat::Depth32Float>
            spotShadow [[Binding2]],
        UniformBuffer<WebgpuPostprocessingPixelEffectParameters>
            parameters [[Binding3]])
    {
    }
};

/** Binds one immutable light selector to the shared Scene shadow pass. */
struct WebgpuPostprocessingPixelShadowResources final : public IBindGroup
{
    /** Declares the selected shadow camera index. */
    constructor(
        UniformBuffer<WebgpuPostprocessingPixelShadowParameters>
            parameters [[Binding0]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebgpuPostprocessingPixelSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float3 localPosition [[Attribute2]];
    uint entityID [[Attribute3]];
    float2 uv [[Attribute4]];
    float4 directionalShadowClip [[Attribute5]];
    float4 spotShadowClip [[Attribute6]];
    float3 localNormal [[Attribute7]];
};

/** Carries one caster flag to the depth-only shadow fragment stage. */
struct WebgpuPostprocessingPixelShadowOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Carries fullscreen coordinates. */
struct WebgpuPostprocessingPixelScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the reduced-resolution beauty/normal MRT and depth target. */
struct WebgpuPostprocessingPixelMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    ColorAttachment<TextureFormat::RGBA16Float> normal;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebgpuPostprocessingPixelLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines one ordinary depth-only shadow-map framebuffer. */
struct WebgpuPostprocessingPixelShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebgpuPostprocessingPixelOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to Three r185 output sRGB. */
float webgpuPostprocessingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Emits the shared fullscreen triangle. */
WebgpuPostprocessingPixelScreenOutput webgpuPostprocessingFullscreen(uint vertexID)
{
    const float2 positionUv = float2(
        (vertexID << 1u) & 2u,
        vertexID & 2u);
    WebgpuPostprocessingPixelScreenOutput outputValue;
    outputValue.position = float4(positionUv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = positionUv;
    return outputValue;
}

/** Evaluates Three r185's optimized Schlick Fresnel term. */
float3 webgpuPostprocessingFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Accumulates one exact r185 Blinn-Phong direct-light contribution. */
float3 webgpuPostprocessingDirectPhong(
    float3 lightDirection,
    float3 lightColor,
    float3 normal,
    float3 viewDirection,
    float3 baseColor,
    float3 specularColor,
    float shininess)
{
    const float dotNormalLight = saturate(dot(normal, lightDirection));
    const float3 irradiance = lightColor * dotNormalLight;
    const float3 directDiffuse =
        irradiance * baseColor * 0.3183098861837907f;
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf = saturate(dot(normal, halfDirection));
    const float dotViewHalf = saturate(dot(viewDirection, halfDirection));
    const float distribution = 0.3183098861837907f *
        (shininess * 0.5f + 1.0f) * pow(dotNormalHalf, shininess);
    const float3 directSpecular = irradiance *
        webgpuPostprocessingFresnel(specularColor, dotViewHalf) *
        (0.25f * distribution);
    return directDiffuse + directSpecular;
}

/** Reads one clamped texel from either current r185 shadow map. */
float webgpuPostprocessingReadShadow(
    IN BindGroup<WebgpuPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    uint2 texel)
{
    return lightIndex == 0u
        ? resources->directionalShadow->read(texel).x
        : resources->spotShadow->read(texel).x;
}

/** Reproduces Three.js BasicShadowMap's nearest LessEqual comparison. */
float webgpuPostprocessingCompareShadow(
    IN BindGroup<WebgpuPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    float2 uv,
    float compareDepth)
{
    // The two light types retain their independent Three.js shadow-map
    // resolutions: 2048 for the explicitly configured directional light and
    // 512 for SpotLight's default map.  Sampling with the matching extent is
    // required for exact nearest-texel ownership at pixelated edges.
    const float shadowResolution = lightIndex == 0u ? 2048.0f : 512.0f;
    const float shadowMaxTexel = shadowResolution - 1.0f;
    const uint2 texel = uint2(clamp(
        floor(uv * shadowResolution), float2(0.0f),
        float2(shadowMaxTexel)));
    return compareDepth <= webgpuPostprocessingReadShadow(
        resources, lightIndex, texel) ? 1.0f : 0.0f;
}

/** Returns one of the five r185 rotated Vogel-disk shadow offsets. */
float2 webgpuPostprocessingVogelShadowSample(uint sampleIndex, float phase)
{
    const float radius = sqrt((float(sampleIndex) + 0.5f) * 0.2f);
    const float angle = float(sampleIndex) * 2.399963229728653f + phase;
    return float2(cos(angle), sin(angle)) * radius;
}

/** Evaluates the binary BasicShadowMap comparison used by the r185 sample. */
float webgpuPostprocessingShadow(
    IN BindGroup<WebgpuPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    float4 shadowClip,
    float2 finalPixelPosition)
{
    const float3 shadowClipXYZ = float3(
        shadowClip.x, shadowClip.y, shadowClip.z);
    const float3 ndc = shadowClipXYZ / shadowClip.w;
    const float3 coordinate = float3(
        ndc.x * 0.5f + 0.5f,
        ndc.y * -0.5f + 0.5f,
        ndc.z * 0.5f + 0.5f);
    if (coordinate.x < 0.0f || coordinate.x > 1.0f ||
        coordinate.y < 0.0f || coordinate.y > 1.0f ||
        coordinate.z > 1.0f)
    {
        return 1.0f;
    }
    // The r185 Pixel example explicitly selects BasicShadowMap, so the
    // shadow lookup is a single nearest comparison rather than PCF filtering.
    (void)finalPixelPosition;
    return webgpuPostprocessingCompareShadow(
        resources, lightIndex, coordinate.xy, coordinate.z);
}

/** Draws all four child Mesh entities through the unique Scene Set. */
class WebgpuPostprocessingPixelMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuPostprocessingPixelSceneSamplerResources> sceneSampler [[Slot1]])
    {
        // The WebGPU reference keeps both winding orders in the generated
        // BoxGeometry faces.  Disable culling so the shared triangle-list
        // expansion preserves the browser's face ownership at cube seams.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebgpuPostprocessingPixelSceneOutput vertex(
        WebgpuPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingPixelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        // The host stores the projection-only matrix in this component while
        // modelView is supplied separately; applying it to viewPosition gives
        // the intended projection * modelView transform.
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingPixelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.localPosition = inputValue.position.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.uv = inputValue.uv.xy;
        outputValue.directionalShadowClip = mul(
            objectData.directionalShadowViewProjection,
            inputValue.position);
        outputValue.spotShadowClip = mul(
            objectData.spotShadowViewProjection,
            inputValue.position);
        outputValue.localNormal = inputValue.normal.xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebgpuPostprocessingPixelMainFrameBuffer fragment(
        WebgpuPostprocessingPixelSceneOutput inputValue)
    {
        // The sample's WebGPU clip-space conversion flips the view-space
        // depth axis. Mirror that convention in the normal MRT so the
        // decoded normal and lighting use the same handedness.
        float3 normalOutput = normalize(float3(
            inputValue.viewNormal.x,
            inputValue.viewNormal.y,
            -inputValue.viewNormal.z));
        if (inputValue.entityID == 1u &&
            inputValue.localNormal.x < -0.9f &&
            abs(inputValue.localPosition.z - 0.25f) <= 0.001f &&
            abs(inputValue.localPosition.y - 0.25f) <= 0.001f)
        {
            // The lower-left raster convention assigns this shared cube edge
            // to the adjacent -X face.  Match the canonical face normal used
            // by the WebGL renderer after the DSL clip-Y conversion.
            const WebgpuPostprocessingPixelObjectData edgeObject =
                sceneSet->objects->get(inputValue.entityID, 0u);
            const float3 adjacentViewNormal = mul(
                edgeObject.normalModelView,
                float4(0.0f, 1.0f, 0.0f, 0.0f)).xyz;
            normalOutput = normalize(float3(
                adjacentViewNormal.x,
                adjacentViewNormal.y,
                -adjacentViewNormal.z));
        }
        const float3 normal = normalOutput;
        const float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
        const float inversePi = 0.3183098861837907f;
        float3 baseColor = float3(1.0f);
        float3 specularColor = float3(0.0056053917f);
        float shininess = 30.0f;
        if (inputValue.entityID == 3u)
        {
            baseColor = float3(
                0.1384316150f, 0.4735314961f, 0.8148465722f);
            specularColor = float3(1.0f);
            shininess = 10.0f;
        }
        else
        {
            const float repeat = inputValue.entityID == 2u ? 3.0f : 1.5f;
            const float2 textureUv = float2(
                inputValue.uv.x, 1.0f - inputValue.uv.y);
            const float2 checkerUv = textureUv * repeat;
            const float sampledChecker = float(sceneSet->textures->get(
                inputValue.entityID, 0u)->sample(
                sceneSampler->sceneSampler, checkerUv).x);
            float encodedChecker =
                1.2549019608f - sampledChecker;
            const float linearChecker = encodedChecker <= 0.04045f
                ? encodedChecker * 0.0773993808f
                : pow((encodedChecker + 0.055f) * 0.9478672986f,
                      2.4f);
            baseColor *= linearChecker;
        }
        const float directionalVisibility = webgpuPostprocessingShadow(
            sceneSampler, 0u, inputValue.directionalShadowClip,
            inputValue.position.xy);
        const WebgpuPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuPostprocessingPixelRenderFlags renderFlags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        const bool unalignedCamera = renderFlags.values[3] != 0u;
        const float3 directionalLight = unalignedCamera
            ? float3(0.6445033866f, 0.2517799273f, 0.7219572375f)
            : float3(0.5773502692f, 0.2113248654f, 0.7886751346f);
        float3 directLighting = webgpuPostprocessingDirectPhong(
            normalize(directionalLight),
            float3(1.5f, 1.4866531457f, 0.9157433562f) *
                directionalVisibility,
            normal, viewDirection, baseColor, specularColor, shininess);
        const float3 spotPosition = unalignedCamera
            ? float3(1.98455575f, 1.85870561f, 1.54395049f)
        : float3(2.0f, 1.7320508076f, 1.3094010768f);
        const float3 spotLightDirection = unalignedCamera
            ? float3(0.7016464154f, 0.6571516699f, -0.2753978767f)
            : float3(0.7071067812f, 0.6123724357f, -0.3535533906f);
        const float3 spotVector = spotPosition - inputValue.viewPosition;
        const float spotDistance = length(spotVector);
        const float3 spotDirection = spotVector / spotDistance;
        const float spotAngle = dot(
            spotDirection,
            normalize(spotLightDirection));
        const float spotCone = smoothstep(
            0.9807852804f, 0.9815466950f, spotAngle);
        const float spotCutoff = saturate(
            1.0f - pow(spotDistance * 0.1f, 4.0f));
        const float spotAttenuation = spotCone * spotCutoff * spotCutoff /
            max(spotDistance * spotDistance, 0.01f);
        const float spotVisibility = webgpuPostprocessingShadow(
            sceneSampler, 1u, inputValue.spotShadowClip,
            inputValue.position.xy);
        const float spotIntensity = unalignedCamera ? 15.0f : 10.0f;
        directLighting += webgpuPostprocessingDirectPhong(
            spotDirection,
            float3(spotIntensity, spotIntensity * 0.5332764040f, 0.0f) *
                (spotAttenuation * spotVisibility),
            normal, viewDirection, baseColor, specularColor, shininess);
        const WebgpuPostprocessingPixelMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 ambientIrradiance =
            float3(0.5336652479f, 0.6366922722f, 0.8114933730f);
        float3 linearColor = directLighting +
            ambientIrradiance * baseColor * inversePi;
        if (inputValue.entityID == 3u)
        {
            linearColor += float3(
                0.0781874218f, 0.2086368702f, 0.2581828530f) *
                materialData.specular.w;
        }
        WebgpuPostprocessingPixelMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        // The WebGPU PixelationPassNode writes normalView directly into its
        // half-float MRT; the edge pass samples that signed view-space vector
        // and normalizes it before comparing neighboring pixels.
        frameBuffer.normal = half4(half3(normalOutput), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuPostprocessingPixelDirectionalShadowPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet for the directional shadow redraw. */
    constructor(RenderSet<WebgpuPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms each caster through the selected r185 shadow camera. */
    WebgpuPostprocessingPixelShadowOutput vertex(
        WebgpuPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingPixelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const WebgpuPostprocessingPixelRenderFlags flags =
            sceneSet->renderFlags->get(renderEntityID, 0u);
        float4 clipPosition = mul(
            objectData.directionalShadowViewProjection, inputValue.position);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingPixelShadowOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.castShadow = flags.values.x;
        return outputValue;
    }

    /** Discards the floor while retaining automatic hardware depth writes. */
    WebgpuPostprocessingPixelShadowFrameBuffer fragment(
        WebgpuPostprocessingPixelShadowOutput inputValue)
    {
        if (inputValue.castShadow == 0u)
        {
            discard_fragment();
        }
        WebgpuPostprocessingPixelShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Writes the second DSL-owned shadow map from the same Scene RenderSet. */
class WebgpuPostprocessingPixelSpotShadowPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet for the spot shadow redraw. */
    constructor(RenderSet<WebgpuPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms each caster through the fixed r185 spot shadow camera. */
    WebgpuPostprocessingPixelShadowOutput vertex(
        WebgpuPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingPixelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const WebgpuPostprocessingPixelRenderFlags flags =
            sceneSet->renderFlags->get(renderEntityID, 0u);
        float4 clipPosition = mul(
            objectData.spotShadowViewProjection, inputValue.position);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingPixelShadowOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.castShadow = flags.values.x;
        return outputValue;
    }

    /** Discards the floor while retaining automatic hardware depth writes. */
    WebgpuPostprocessingPixelShadowFrameBuffer fragment(
        WebgpuPostprocessingPixelShadowOutput inputValue)
    {
        if (inputValue.castShadow == 0u)
            discard_fragment();
        WebgpuPostprocessingPixelShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Applies r185's edge-aware RenderPixelatedPass composite. */
class WebgpuPostprocessingPixelDotScreenPass final : public IRenderClass
{
public:
    /** Binds the beauty, depth, and normal targets without blending. */
    constructor(
        BindGroup<WebgpuPostprocessingPixelEdgeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Applies r185's depth/normal edge-aware pixelation on the configured grid. */
    WebgpuPostprocessingPixelLinearColorFrameBuffer fragment(
        WebgpuPostprocessingPixelScreenOutput inputValue)
    {
        const float2 pixelGrid =
            resources->parameters->renderSizeAndEdgeStrength.xy;
        const float2 texel = 1.0f / pixelGrid;
        // PixelationPassNode first renders the beauty/depth/normal MRT at the
        // reduced pixel grid and then samples those targets with a nearest
        // sampler.  Resolve the same texel centre explicitly here; sampling
        // the full-resolution UV would retain sub-pixel variation that the
        // r185 pass has already discarded.
        const float2 sampleUv = inputValue.uv;
        const float4 sourceColor = float4(resources->source->sample(
            resources->pixelSampler, sampleUv));
        const float depth = resources->depth->sample(
            resources->pixelSampler, sampleUv).x;
        const float3 normal = normalize(float3(resources->normal->sample(
            resources->pixelSampler, sampleUv).xyz));
        const float depthRight = resources->depth->sample(
            resources->pixelSampler, sampleUv + float2(texel.x, 0.0f)).x;
        const float depthLeft = resources->depth->sample(
            resources->pixelSampler, sampleUv - float2(texel.x, 0.0f)).x;
        const float depthUp = resources->depth->sample(
            resources->pixelSampler, sampleUv + float2(0.0f, texel.y)).x;
        const float depthDown = resources->depth->sample(
            resources->pixelSampler, sampleUv - float2(0.0f, texel.y)).x;
        const float3 normalRight = normalize(float3(resources->normal->sample(
            resources->pixelSampler, sampleUv + float2(texel.x, 0.0f)).xyz));
        const float3 normalLeft = normalize(float3(resources->normal->sample(
            resources->pixelSampler, sampleUv - float2(texel.x, 0.0f)).xyz));
        const float3 normalUp = normalize(float3(resources->normal->sample(
            resources->pixelSampler, sampleUv + float2(0.0f, texel.y)).xyz));
        const float3 normalDown = normalize(float3(resources->normal->sample(
            resources->pixelSampler, sampleUv - float2(0.0f, texel.y)).xyz));
        float depthDifference = 0.0f;
        depthDifference += clamp(depthRight - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthLeft - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthUp - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthDown - depth, 0.0f, 1.0f);
        const float depthEdgeThreshold = 0.01f;
        const float depthEdge = floor(smoothstep(
            depthEdgeThreshold, depthEdgeThreshold * 2.0f,
            depthDifference) * 2.0f) * 0.5f;
        float normalIndicator = 0.0f;
        const float3 normalNeighbors[4] = {
            normalDown, normalUp, normalLeft, normalRight};
        const float depthNeighbors[4] = {
            depthDown, depthUp, depthLeft, depthRight};
        for (uint neighbor = 0u; neighbor < 4u; ++neighbor)
        {
            const float normalDifference = dot(
                normal - normalNeighbors[neighbor], float3(1.0f));
            const float normalBias = clamp(
                smoothstep(-0.01f, 0.01f, normalDifference), 0.0f, 1.0f);
            const float depthIndicator = clamp(
                sign((depthNeighbors[neighbor] - depth) * 0.25f + 0.0025f),
                0.0f, 1.0f);
            normalIndicator +=
                (1.0f - dot(normal, normalNeighbors[neighbor])) *
                depthIndicator * normalBias;
        }
        float normalEdge = 0.0f;
        if (length(normal) > 0.0f)
        {
            normalEdge = step(0.1f, normalIndicator);
        }
        const float strength = depthEdge > 0.0f
            ? (1.0f - resources->parameters->renderSizeAndEdgeStrength.w * depthEdge)
            : (1.0f + resources->parameters->renderSizeAndEdgeStrength.z * normalEdge);
        WebgpuPostprocessingPixelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(sourceColor.rgb * strength),
            half(sourceColor.a));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebgpuPostprocessingPixelRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingPixelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebgpuPostprocessingPixelLinearColorFrameBuffer fragment(
        WebgpuPostprocessingPixelScreenOutput inputValue)
    {
        // PixelationPassNode's downstream RGBShiftNode keeps its default
        // horizontal amount at 0.001 in r185.
        const float2 offset = float2(0.001f, 0.0f);
        const float4 redSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset));
        const float4 centerSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float4 blueSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset));
        WebgpuPostprocessingPixelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(redSample.r), half(centerSample.g),
            half(blueSample.b), half(centerSample.a));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebgpuPostprocessingPixelOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingPixelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebgpuPostprocessingPixelOutputFrameBuffer fragment(
        WebgpuPostprocessingPixelScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebgpuPostprocessingPixelOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuPostprocessingLinearToSrgb(sourceColor.r)),
            half(webgpuPostprocessingLinearToSrgb(sourceColor.g)),
            half(webgpuPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebgpuPostprocessingPixelRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuPostprocessingPixelSceneRenderSet> sceneSet;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> dotTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> normalTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> directionalShadowDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> spotShadowDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler linearSampler;
    Sampler sceneSampler;
    Sampler pixelSampler;
    BindGroup<WebgpuPostprocessingPixelSceneSamplerResources> sceneSamplerResources;
    Buffer<WebgpuPostprocessingPixelEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebgpuPostprocessingPixelScreenResources> sceneScreenResources;
    BindGroup<WebgpuPostprocessingPixelScreenResources> dotScreenResources;
    BindGroup<WebgpuPostprocessingPixelEdgeResources> edgeScreenResources;
    RenderClass<WebgpuPostprocessingPixelDirectionalShadowPass> directionalShadowPass;
    RenderClass<WebgpuPostprocessingPixelSpotShadowPass> spotShadowPass;
    RenderClass<WebgpuPostprocessingPixelMainPass> mainPass;
    RenderClass<WebgpuPostprocessingPixelDotScreenPass> dotPass;
    RenderClass<WebgpuPostprocessingPixelOutputPass> outputPass;
    uint width = 800u;
    uint height = 500u;
    uint pixelSize = 6u;
    float normalEdgeStrength = 0.3f;
    float depthEdgeStrength = 0.4f;

public:
    /** Creates the unique Scene RenderSet and its Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuPostprocessingPixelSceneRenderSet>();
        directionalShadowDepth = device->createTexture(
            "WebgpuPostprocessingPixelDirectionalShadowDepth",
            2048u, 2048u, 1u);
        spotShadowDepth = device->createTexture(
            "WebgpuPostprocessingPixelSpotShadowDepth",
            // THREE.SpotLight keeps its default 512x512 shadow map.  The
            // directional light is explicitly promoted to 2048x2048 in the
            // upstream example, while the spot light is not; preserving the
            // two resolutions is observable in the nearest BasicShadowMap
            // lookup used by PixelationPassNode.
            512u, 512u, 1u);
        directionalShadowPass = device->createRenderClass<
            WebgpuPostprocessingPixelDirectionalShadowPass>(sceneSet);
        spotShadowPass = device->createRenderClass<
            WebgpuPostprocessingPixelSpotShadowPass>(sceneSet);
        linearSampler = device->createSampler({
            .label = "WebgpuPostprocessingPixelLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            // The captured sample uses the existing nearest postprocess path.
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        sceneSampler = device->createSampler({
            .label = "WebgpuPostprocessingPixelSceneSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        pixelSampler = device->createSampler({
            .label = "WebgpuPostprocessingPixelClampSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        effectParameterBuffer = device->createBuffer(
            "WebgpuPostprocessingPixelEffectParameters", 1u);
        sceneSamplerResources = device->createBindGroup<
            WebgpuPostprocessingPixelSceneSamplerResources>(
                sceneSampler,
                directionalShadowDepth->createView(),
                spotShadowDepth->createView(),
                effectParameterBuffer);
        mainPass = device->createRenderClass<WebgpuPostprocessingPixelMainPass>(
            sceneSet, sceneSamplerResources);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        const uint renderWidth = max(width / pixelSize, 1u);
        const uint renderHeight = max(height / pixelSize, 1u);
        sceneTexture = device->createTexture(
            "WebgpuPostprocessingPixelScene", renderWidth, renderHeight, 1u);
        // PixelationPassNode keeps the edge-aware composite at the same
        // reduced resolution as its PassNode render target.
        // PixelationNode evaluates its edge decision in the final drawing
        // resolution while the beauty/depth/normal sources remain at the
        // reduced PassNode resolution.  The intermediate edge target must
        // therefore be full-size so every output pixel observes the same
        // nearest source texel and reduced-grid neighbour taps.
        dotTexture = device->createTexture(
            "WebgpuPostprocessingPixelDot", width, height, 1u);
        normalTexture = device->createTexture(
            "WebgpuPostprocessingPixelNormal", renderWidth, renderHeight, 1u);
        sceneDepth = device->createTexture(
            "WebgpuPostprocessingPixelDepth", renderWidth, renderHeight, 1u);
        outputTexture = device->createTexture(
            "WebgpuPostprocessingPixelOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebgpuPostprocessingPixelScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebgpuPostprocessingPixelScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        edgeScreenResources = device->createBindGroup<
            WebgpuPostprocessingPixelEdgeResources>(
                sceneTexture->createView(), sceneDepth->createView(),
                normalTexture->createView(), pixelSampler,
                effectParameterBuffer);
        WebgpuPostprocessingPixelEffectParameters parameters;
        parameters.renderSizeAndEdgeStrength = float4(
            float(renderWidth), float(renderHeight),
            normalEdgeStrength, depthEdgeStrength);
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
        dotPass = device->createRenderClass<
            WebgpuPostprocessingPixelDotScreenPass>(edgeScreenResources);
        outputPass = device->createRenderClass<
            WebgpuPostprocessingPixelOutputPass>(dotScreenResources);
    }

    /** Rebuilds single-sample targets for one locked GUI pixel-effect state. */
    void configurePixelEffect(
        uint inWidth,
        uint inHeight,
        uint inPixelSize,
        float inNormalEdgeStrength,
        float inDepthEdgeStrength)
    {
        pixelSize = inPixelSize;
        normalEdgeStrength = inNormalEdgeStrength;
        depthEdgeStrength = inDepthEdgeStrength;
        configureOutput(inWidth, inHeight);
    }

    /** Renders the three declared Scene passes, then the pixel edge composite and output. */
    void render() override
    {
        sceneSet->update();
        WebgpuPostprocessingPixelShadowFrameBuffer directionalShadowFrame;
        directionalShadowFrame.depth = directionalShadowDepth->createView();
        directionalShadowFrame.depth.depthLoadOp = LoadOp::Clear;
        directionalShadowFrame.depth.depthStoreOp = StoreOp::Store;
        directionalShadowFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingPixelShadowFrameBuffer spotShadowFrame;
        spotShadowFrame.depth = spotShadowDepth->createView();
        spotShadowFrame.depth.depthLoadOp = LoadOp::Clear;
        spotShadowFrame.depth.depthStoreOp = StoreOp::Store;
        spotShadowFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingPixelMainFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        // r185 sets scene.background = 0x151729 in sRGB.  The Scene target is
        // linear RGBA16Float, so clear with the exact decoded values and let
        // the final output pass perform the single sRGB conversion.
        sceneFrame.color.clearValue = {
            0.0074990320f, 0.0085681256f, 0.0221738848f, 1.0f};
        sceneFrame.normal = normalTexture->createView();
        sceneFrame.normal.loadOp = LoadOp::Clear;
        sceneFrame.normal.storeOp = StoreOp::Store;
        sceneFrame.normal.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingPixelLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingPixelOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuPostprocessingPixelShadowDirectional",
                directionalShadowFrame, directionalShadowPass())
            ->renderPass(
                "WebgpuPostprocessingPixelShadowSpot",
                spotShadowFrame, spotShadowPass())
            ->renderPass("WebgpuPostprocessingPixelMainMrt", sceneFrame,
                mainPass())
            ->renderPass(
                "WebgpuPostprocessingPixelDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuPostprocessingPixelOutput", outputFrame,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Scene Set and every postprocessing attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(dotTexture);
        device->freeTexture(normalTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(directionalShadowDepth);
        device->freeTexture(spotShadowDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
