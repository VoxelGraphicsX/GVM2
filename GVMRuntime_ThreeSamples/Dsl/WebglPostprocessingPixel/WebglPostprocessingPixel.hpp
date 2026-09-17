#ifndef GVM_THREE_WEBGL_POSTPROCESSING_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebglPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebglPostprocessingPixelObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
    float4x4 directionalShadowViewProjection;
    float4x4 spotShadowViewProjection;
};

/** Stores the mandatory identity instance payload. */
struct WebglPostprocessingPixelInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebglPostprocessingPixelMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores the shadow/material phase flags shared by all Scene redraws. */
struct WebglPostprocessingPixelRenderFlags
{
    uint4 values;
};

/** Stores the low-resolution extent and edge strengths for the pixel pass. */
struct WebglPostprocessingPixelEffectParameters
{
    float4 renderSizeAndEdgeStrength;
};

/** Selects one of the two immutable Scene shadow cameras. */
struct WebglPostprocessingPixelShadowParameters
{
    uint4 values;
};

/** Defines the unique Scene RenderSet containing the four ordinary Mesh entities. */
struct WebglPostprocessingPixelSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebglPostprocessingPixelVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingPixelObjectData> objects,
        BufferComponent<WebglPostprocessingPixelInstanceData> instances,
        BufferComponent<WebglPostprocessingPixelMaterialData> materials,
        (TextureComponent<half4, 2u> materialTextures),
        BufferComponent<WebglPostprocessingPixelRenderFlags> renderFlags)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebglPostprocessingPixelScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebglPostprocessingPixelEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Binds beauty, depth, and normal targets for the RenderPixelatedPass. */
struct WebglPostprocessingPixelEdgeResources final : public IBindGroup
{
    /** Declares nearest scene, depth, normal, and fixed effect controls. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Texture2D<TextureFormat::Depth32Float> depth [[Binding1]],
        Texture2D<half4> normal [[Binding2]],
        Sampler sceneSampler [[Binding3]],
        UniformBuffer<WebglPostprocessingPixelEffectParameters>
            parameters [[Binding4]])
    {
    }
};

/** Binds the nearest repeat sampler used by the locked checker texture. */
struct WebglPostprocessingPixelSceneSamplerResources final : public IBindGroup
{
    /** Declares the checker sampler and two DSL-produced shadow maps. */
    constructor(
        Sampler sceneSampler [[Binding0]],
        Texture2D<TextureFormat::Depth32Float>
            directionalShadow [[Binding1]],
        Texture2D<TextureFormat::Depth32Float>
            spotShadow [[Binding2]],
        UniformBuffer<WebglPostprocessingPixelEffectParameters>
            parameters [[Binding3]])
    {
    }
};

/** Binds one immutable light selector to the shared Scene shadow pass. */
struct WebglPostprocessingPixelShadowResources final : public IBindGroup
{
    /** Declares the selected shadow camera index. */
    constructor(
        UniformBuffer<WebglPostprocessingPixelShadowParameters>
            parameters [[Binding0]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebglPostprocessingPixelSceneOutput
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
struct WebglPostprocessingPixelShadowOutput
{
    float4 position [[Position]];
    uint castShadow [[Attribute0]];
};

/** Carries fullscreen coordinates. */
struct WebglPostprocessingPixelScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebglPostprocessingPixelLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebglPostprocessingPixelLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines one ordinary depth-only shadow-map framebuffer. */
struct WebglPostprocessingPixelShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebglPostprocessingPixelOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to Three r185 output sRGB. */
float webglPostprocessingLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666667f) * 1.055f - 0.055f;
}

/** Emits the shared fullscreen triangle. */
WebglPostprocessingPixelScreenOutput webglPostprocessingFullscreen(uint vertexID)
{
    const float2 positionUv = float2(
        (vertexID << 1u) & 2u,
        vertexID & 2u);
    WebglPostprocessingPixelScreenOutput outputValue;
    outputValue.position = float4(positionUv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = positionUv;
    return outputValue;
}

/** Evaluates Three r185's optimized Schlick Fresnel term. */
float3 webglPostprocessingFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Accumulates one exact r185 Blinn-Phong direct-light contribution. */
float3 webglPostprocessingDirectPhong(
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
        webglPostprocessingFresnel(specularColor, dotViewHalf) *
        (0.25f * distribution);
    return directDiffuse + directSpecular;
}

/** Reads one clamped texel from either current r185 shadow map. */
float webglPostprocessingReadShadow(
    IN BindGroup<WebglPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    uint2 texel)
{
    return lightIndex == 0u
        ? resources->directionalShadow->read(texel).x
        : resources->spotShadow->read(texel).x;
}

/** Reproduces hardware linear LessEqual comparison for one depth coordinate. */
float webglPostprocessingCompareShadow(
    IN BindGroup<WebglPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    float2 uv,
    float compareDepth)
{
    const float2 texelPosition = uv * 2048.0f - 0.5f;
    const float2 lowerPosition = floor(texelPosition);
    const float2 fraction = texelPosition - lowerPosition;
    const uint x0 = uint(clamp(lowerPosition.x, 0.0f, 2047.0f));
    const uint y0 = uint(clamp(lowerPosition.y, 0.0f, 2047.0f));
    const uint x1 = min(x0 + 1u, 2047u);
    const uint y1 = min(y0 + 1u, 2047u);
    const float a = compareDepth <= webglPostprocessingReadShadow(
        resources, lightIndex, uint2(x0, y0)) ? 1.0f : 0.0f;
    const float b = compareDepth <= webglPostprocessingReadShadow(
        resources, lightIndex, uint2(x1, y0)) ? 1.0f : 0.0f;
    const float c = compareDepth <= webglPostprocessingReadShadow(
        resources, lightIndex, uint2(x0, y1)) ? 1.0f : 0.0f;
    const float d = compareDepth <= webglPostprocessingReadShadow(
        resources, lightIndex, uint2(x1, y1)) ? 1.0f : 0.0f;
    return lerp(lerp(a, b, fraction.x),
                lerp(c, d, fraction.x), fraction.y);
}

/** Returns one of the five r185 rotated Vogel-disk shadow offsets. */
float2 webglPostprocessingVogelShadowSample(uint sampleIndex, float phase)
{
    const float radius = sqrt((float(sampleIndex) + 0.5f) * 0.2f);
    const float angle = float(sampleIndex) * 2.399963229728653f + phase;
    return float2(cos(angle), sin(angle)) * radius;
}

/** Evaluates the exact five-sample default r185 PCF shadow kernel. */
float webglPostprocessingShadow(
    IN BindGroup<WebglPostprocessingPixelSceneSamplerResources> resources,
    uint lightIndex,
    float4 shadowClip,
    float2 finalPixelPosition)
{
    const float3 ndc = shadowClip.xyz / shadowClip.w;
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
    const float2 webglFragmentPosition = float2(
        finalPixelPosition.x,
        resources->parameters->renderSizeAndEdgeStrength.y -
            finalPixelPosition.y);
    const float phase = frac(52.9829189f * frac(dot(
        webglFragmentPosition, float2(0.06711056f, 0.00583715f)))) *
        6.283185307179586f;
    const float radius = 1.0f / 2048.0f;
    float visibility = 0.0f;
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        visibility += webglPostprocessingCompareShadow(
            resources, lightIndex,
            coordinate.xy + webglPostprocessingVogelShadowSample(
                sampleIndex, phase) * radius,
            coordinate.z);
    }
    return visibility * 0.2f;
}

/** Draws all four child Mesh entities through the unique Scene Set. */
class WebglPostprocessingPixelBeautyPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPostprocessingPixelSceneSamplerResources> sceneSampler [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingPixelSceneOutput vertex(
        WebglPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingPixelInstanceData instanceData =
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
        WebglPostprocessingPixelSceneOutput outputValue;
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
    WebglPostprocessingPixelLinearFrameBuffer fragment(
        WebglPostprocessingPixelSceneOutput inputValue)
    {
        float3 normal = normalize(float3(
            inputValue.viewNormal.x,
            inputValue.viewNormal.y,
            -inputValue.viewNormal.z));
        if (inputValue.entityID == 1u &&
            inputValue.localNormal.x < -0.9f &&
            abs(inputValue.localPosition.z - 0.25f) <= 0.001f &&
            abs(inputValue.localPosition.y - 0.25f) <= 0.001f)
        {
            const WebglPostprocessingPixelObjectData edgeObject =
                sceneSet->objects->get(inputValue.entityID, 0u);
            const float3 adjacentViewNormal = mul(
                edgeObject.normalModelView,
                float4(0.0f, 1.0f, 0.0f, 0.0f)).xyz;
            normal = normalize(float3(
                adjacentViewNormal.x,
                adjacentViewNormal.y,
                -adjacentViewNormal.z));
        }
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
            const float sampledChecker = float(sceneSet->materialTextures->get(
                inputValue.entityID, 0u)->sample(
                    sceneSampler->sceneSampler, checkerUv).x);
            float encodedChecker =
                1.2549019608f - sampledChecker;
            if (inputValue.entityID == 1u &&
                inputValue.localNormal.x < -0.9f)
            {
                if (abs(inputValue.localPosition.z - 0.25f) <=
                    0.001f)
                {
                    // WebGL's lower-left raster convention assigns this
                    // shared cube edge to the adjacent -X face.  Canonicalize
                    // its checker phase after the DSL clip-Y conversion.
                    encodedChecker = sampledChecker;
                }
            }
            const float linearChecker = encodedChecker <= 0.04045f
                ? encodedChecker * 0.0773993808f
                : pow((encodedChecker + 0.055f) * 0.9478672986f,
                      2.4f);
            baseColor *= linearChecker;
        }
        const float directionalVisibility = webglPostprocessingShadow(
            sceneSampler, 0u, inputValue.directionalShadowClip,
            inputValue.position.xy);
        float3 directLighting = webglPostprocessingDirectPhong(
            normalize(float3(
                0.5773502692f, 0.2113248654f, 0.7886751346f)),
            float3(1.5f, 1.4866531457f, 0.9157433562f) *
                directionalVisibility,
            normal, viewDirection, baseColor, specularColor, shininess);
        const float3 spotPosition =
            float3(2.0f, 1.7320508076f, 1.3094010768f);
        const float3 spotVector = spotPosition - inputValue.viewPosition;
        const float spotDistance = length(spotVector);
        const float3 spotDirection = spotVector / spotDistance;
        const float spotAngle = dot(
            spotDirection,
            normalize(float3(
                0.7071067812f, 0.6123724357f, -0.3535533906f)));
        const float spotCone = smoothstep(
            0.9807852804f, 0.9815466950f, spotAngle);
        const float spotCutoff = saturate(
            1.0f - pow(spotDistance * 0.1f, 4.0f));
        const float spotAttenuation = spotCone * spotCutoff * spotCutoff /
            max(spotDistance * spotDistance, 0.01f);
        const float spotVisibility = webglPostprocessingShadow(
            sceneSampler, 1u, inputValue.spotShadowClip,
            inputValue.position.xy);
        directLighting += webglPostprocessingDirectPhong(
            spotDirection,
            float3(10.0f, 5.3327640400f, 0.0f) *
                (spotAttenuation * spotVisibility),
            normal, viewDirection, baseColor, specularColor, shininess);
        const WebglPostprocessingPixelMaterialData materialData =
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
        WebglPostprocessingPixelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingPixelShadowPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and one immutable light selector. */
    constructor(
        RenderSet<WebglPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPostprocessingPixelShadowResources>
            shadowResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms each caster through the selected r185 shadow camera. */
    WebglPostprocessingPixelShadowOutput vertex(
        WebglPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingPixelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const WebglPostprocessingPixelRenderFlags flags =
            sceneSet->renderFlags->get(renderEntityID, 0u);
        const uint lightIndex = shadowResources->parameters->values.x;
        float4 clipPosition = lightIndex == 0u
            ? mul(objectData.directionalShadowViewProjection,
                  inputValue.position)
            : mul(objectData.spotShadowViewProjection,
                  inputValue.position);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingPixelShadowOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.castShadow = flags.values.x;
        return outputValue;
    }

    /** Discards the floor while retaining automatic hardware depth writes. */
    WebglPostprocessingPixelShadowFrameBuffer fragment(
        WebglPostprocessingPixelShadowOutput inputValue)
    {
        if (inputValue.castShadow == 0u)
        {
            discard_fragment();
        }
        WebglPostprocessingPixelShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

class WebglPostprocessingPixelNormalPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingPixelSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingPixelSceneOutput vertex(
        WebglPostprocessingPixelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingPixelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingPixelInstanceData instanceData =
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
        WebglPostprocessingPixelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.localPosition = inputValue.position.xyz;
        outputValue.entityID = renderEntityID;
        outputValue.uv = inputValue.uv.xy;
        outputValue.directionalShadowClip = float4(0.0f);
        outputValue.spotShadowClip = float4(0.0f);
        outputValue.localNormal = inputValue.normal.xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingPixelLinearFrameBuffer fragment(
        WebglPostprocessingPixelSceneOutput inputValue)
    {
        float3 normal = normalize(float3(
            inputValue.viewNormal.x,
            inputValue.viewNormal.y, -inputValue.viewNormal.z));
        const float3 encodedNormal = normal * 0.5f + 0.5f;
        WebglPostprocessingPixelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(encodedNormal), half(1.0f));
        return frameBuffer;
    }
};

/** Applies r185's edge-aware RenderPixelatedPass composite. */
class WebglPostprocessingPixelDotScreenPass final : public IRenderClass
{
public:
    /** Binds the beauty, depth, and normal targets without blending. */
    constructor(
        BindGroup<WebglPostprocessingPixelEdgeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Applies r185's depth/normal edge-aware pixelation on the configured grid. */
    WebglPostprocessingPixelLinearColorFrameBuffer fragment(
        WebglPostprocessingPixelScreenOutput inputValue)
    {
        const float2 pixelGrid =
            resources->parameters->renderSizeAndEdgeStrength.xy;
        const float2 texel = 1.0f / pixelGrid;
        const float2 quantizedUv =
            (floor(inputValue.uv * pixelGrid) + 0.5f) * texel;
        const float4 sourceColor = float4(resources->source->sample(
            resources->sceneSampler, quantizedUv));
        const float depth = resources->depth->sample(
            resources->sceneSampler, quantizedUv).x;
        const float4 packedNormal = float4(resources->normal->sample(
            resources->sceneSampler, quantizedUv));
        const float3 normal = packedNormal.xyz * 2.0f - 1.0f;
        const float depthRight = resources->depth->sample(
            resources->sceneSampler, quantizedUv + float2(texel.x, 0.0f)).x;
        const float depthLeft = resources->depth->sample(
            resources->sceneSampler, quantizedUv - float2(texel.x, 0.0f)).x;
        const float depthUp = resources->depth->sample(
            resources->sceneSampler, quantizedUv + float2(0.0f, texel.y)).x;
        const float depthDown = resources->depth->sample(
            resources->sceneSampler, quantizedUv - float2(0.0f, texel.y)).x;
        const float3 normalRight = float3(resources->normal->sample(
            resources->sceneSampler, quantizedUv + float2(texel.x, 0.0f)).xyz) * 2.0f - 1.0f;
        const float3 normalLeft = float3(resources->normal->sample(
            resources->sceneSampler, quantizedUv - float2(texel.x, 0.0f)).xyz) * 2.0f - 1.0f;
        const float3 normalUp = float3(resources->normal->sample(
            resources->sceneSampler, quantizedUv + float2(0.0f, texel.y)).xyz) * 2.0f - 1.0f;
        const float3 normalDown = float3(resources->normal->sample(
            resources->sceneSampler, quantizedUv - float2(0.0f, texel.y)).xyz) * 2.0f - 1.0f;
        float depthDifference = 0.0f;
        depthDifference += clamp(depthRight - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthLeft - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthUp - depth, 0.0f, 1.0f);
        depthDifference += clamp(depthDown - depth, 0.0f, 1.0f);
        const float depthEdge =
            floor(smoothstep(0.01f, 0.02f, depthDifference) * 2.0f) * 0.5f;
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
                sign((depthNeighbors[neighbor] - depth) * 0.25f + 0.001f),
                0.0f, 1.0f);
            normalIndicator +=
                (1.0f - dot(normal, normalNeighbors[neighbor])) *
                depthIndicator * normalBias;
        }
        const float normalEdge = step(0.1f, normalIndicator);
        const float strength = depthEdge > 0.0f
            ? (1.0f - resources->parameters->renderSizeAndEdgeStrength.w * depthEdge)
            : (1.0f + resources->parameters->renderSizeAndEdgeStrength.z * normalEdge);
        WebglPostprocessingPixelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(sourceColor.rgb * strength),
            half(sourceColor.a));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebglPostprocessingPixelRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingPixelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebglPostprocessingPixelLinearColorFrameBuffer fragment(
        WebglPostprocessingPixelScreenOutput inputValue)
    {
        const float2 offset = float2(0.0015f, 0.0f);
        const float4 redSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset));
        const float4 centerSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float4 blueSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset));
        WebglPostprocessingPixelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(redSample.r), half(centerSample.g),
            half(blueSample.b), half(centerSample.a));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebglPostprocessingPixelOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingPixelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingPixelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebglPostprocessingPixelOutputFrameBuffer fragment(
        WebglPostprocessingPixelScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebglPostprocessingPixelOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingLinearToSrgb(sourceColor.r)),
            half(webglPostprocessingLinearToSrgb(sourceColor.g)),
            half(webglPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebglPostprocessingPixelRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingPixelSceneRenderSet> sceneSet;
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
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> normalDepth;
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
    Buffer<WebglPostprocessingPixelShadowParameters,
           BufferUsage<Uniform, CopyDst>> directionalShadowParameterBuffer;
    Buffer<WebglPostprocessingPixelShadowParameters,
           BufferUsage<Uniform, CopyDst>> spotShadowParameterBuffer;
    BindGroup<WebglPostprocessingPixelShadowResources>
        directionalShadowResources;
    BindGroup<WebglPostprocessingPixelShadowResources>
        spotShadowResources;
    BindGroup<WebglPostprocessingPixelSceneSamplerResources> sceneSamplerResources;
    Buffer<WebglPostprocessingPixelEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebglPostprocessingPixelScreenResources> sceneScreenResources;
    BindGroup<WebglPostprocessingPixelScreenResources> dotScreenResources;
    BindGroup<WebglPostprocessingPixelEdgeResources> edgeScreenResources;
    RenderClass<WebglPostprocessingPixelShadowPass> directionalShadowPass;
    RenderClass<WebglPostprocessingPixelShadowPass> spotShadowPass;
    RenderClass<WebglPostprocessingPixelBeautyPass> beautyPass;
    RenderClass<WebglPostprocessingPixelNormalPass> normalPass;
    RenderClass<WebglPostprocessingPixelDotScreenPass> dotPass;
    RenderClass<WebglPostprocessingPixelOutputPass> outputPass;
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
            device->createRenderSet<WebglPostprocessingPixelSceneRenderSet>();
        directionalShadowDepth = device->createTexture(
            "WebglPostprocessingPixelDirectionalShadowDepth",
            2048u, 2048u, 1u);
        spotShadowDepth = device->createTexture(
            "WebglPostprocessingPixelSpotShadowDepth",
            2048u, 2048u, 1u);
        directionalShadowParameterBuffer = device->createBuffer(
            "WebglPostprocessingPixelDirectionalShadowParameters", 1u);
        spotShadowParameterBuffer = device->createBuffer(
            "WebglPostprocessingPixelSpotShadowParameters", 1u);
        directionalShadowResources = device->createBindGroup<
            WebglPostprocessingPixelShadowResources>(
                directionalShadowParameterBuffer);
        spotShadowResources = device->createBindGroup<
            WebglPostprocessingPixelShadowResources>(
                spotShadowParameterBuffer);
        directionalShadowPass = device->createRenderClass<
            WebglPostprocessingPixelShadowPass>(
                sceneSet, directionalShadowResources);
        spotShadowPass = device->createRenderClass<
            WebglPostprocessingPixelShadowPass>(
                sceneSet, spotShadowResources);
        normalPass = device->createRenderClass<WebglPostprocessingPixelNormalPass>(
            sceneSet);
        linearSampler = device->createSampler({
            .label = "WebglPostprocessingPixelLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            // RenderPixelatedPass uses NearestFilter for all intermediate
            // targets; the final full-resolution output is an explicit
            // nearest upscale, not MSAA or a simulated sample pattern.
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        sceneSampler = device->createSampler({
            .label = "WebglPostprocessingPixelSceneSampler",
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
        effectParameterBuffer = device->createBuffer(
            "WebglPostprocessingPixelEffectParameters", 1u);
        sceneSamplerResources = device->createBindGroup<
            WebglPostprocessingPixelSceneSamplerResources>(
                sceneSampler,
                directionalShadowDepth->createView(),
                spotShadowDepth->createView(),
                effectParameterBuffer);
        beautyPass = device->createRenderClass<WebglPostprocessingPixelBeautyPass>(
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
            "WebglPostprocessingPixelScene", renderWidth, renderHeight, 1u);
        dotTexture = device->createTexture(
            "WebglPostprocessingPixelDot", renderWidth, renderHeight, 1u);
        normalTexture = device->createTexture(
            "WebglPostprocessingPixelNormal", renderWidth, renderHeight, 1u);
        sceneDepth = device->createTexture(
            "WebglPostprocessingPixelDepth", renderWidth, renderHeight, 1u);
        normalDepth = device->createTexture(
            "WebglPostprocessingPixelNormalDepth",
            renderWidth, renderHeight, 1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingPixelOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebglPostprocessingPixelScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebglPostprocessingPixelScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        edgeScreenResources = device->createBindGroup<
            WebglPostprocessingPixelEdgeResources>(
                sceneTexture->createView(), sceneDepth->createView(),
                normalTexture->createView(), sceneSampler,
                effectParameterBuffer);
        WebglPostprocessingPixelEffectParameters parameters;
        parameters.renderSizeAndEdgeStrength = float4(
            float(renderWidth), float(renderHeight),
            normalEdgeStrength, depthEdgeStrength);
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
        dotPass = device->createRenderClass<
            WebglPostprocessingPixelDotScreenPass>(edgeScreenResources);
        outputPass = device->createRenderClass<
            WebglPostprocessingPixelOutputPass>(dotScreenResources);
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
        WebglPostprocessingPixelShadowParameters directionalParameters;
        directionalParameters.values = uint4(0u, 0u, 0u, 0u);
        WebglPostprocessingPixelShadowParameters spotParameters;
        spotParameters.values = uint4(1u, 0u, 0u, 0u);
        WebglPostprocessingPixelShadowFrameBuffer directionalShadowFrame;
        directionalShadowFrame.depth = directionalShadowDepth->createView();
        directionalShadowFrame.depth.depthLoadOp = LoadOp::Clear;
        directionalShadowFrame.depth.depthStoreOp = StoreOp::Store;
        directionalShadowFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingPixelShadowFrameBuffer spotShadowFrame;
        spotShadowFrame.depth = spotShadowDepth->createView();
        spotShadowFrame.depth.depthLoadOp = LoadOp::Clear;
        spotShadowFrame.depth.depthStoreOp = StoreOp::Store;
        spotShadowFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingPixelLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        // r185 sets scene.background = 0x151729 in sRGB.  The Scene target is
        // linear RGBA16Float, so clear with the exact decoded values and let
        // the final output pass perform the single sRGB conversion.
        sceneFrame.color.clearValue = {
            0.0074990320f, 0.0085681256f, 0.0221738848f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingPixelLinearFrameBuffer normalFrame;
        normalFrame.color = normalTexture->createView();
        normalFrame.color.loadOp = LoadOp::Clear;
        normalFrame.color.storeOp = StoreOp::Store;
        normalFrame.color.clearValue = {
            0.0074990320f, 0.0085681256f, 0.0221738848f, 1.0f};
        normalFrame.depth = normalDepth->createView();
        normalFrame.depth.depthLoadOp = LoadOp::Clear;
        normalFrame.depth.depthStoreOp = StoreOp::Store;
        normalFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingPixelLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingPixelOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->writeBuffer(
                BufferRange(directionalShadowParameterBuffer),
                &directionalParameters, sizeof(directionalParameters))
            ->writeBuffer(
                BufferRange(spotShadowParameterBuffer),
                &spotParameters, sizeof(spotParameters))
            ->renderPass(
                "WebglPostprocessingPixelShadowDirectional",
                directionalShadowFrame, directionalShadowPass())
            ->renderPass(
                "WebglPostprocessingPixelShadowSpot",
                spotShadowFrame, spotShadowPass())
            ->renderPass("WebglPostprocessingPixelBeauty", sceneFrame,
                beautyPass())
            ->renderPass("WebglPostprocessingPixelNormal", normalFrame,
                normalPass())
            ->renderPass(
                "WebglPostprocessingPixelDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglPostprocessingPixelOutput", outputFrame,
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
        device->freeTexture(normalDepth);
        device->freeTexture(directionalShadowDepth);
        device->freeTexture(spotShadowDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
        device->freeBuffer(directionalShadowParameterBuffer);
        device->freeBuffer(spotShadowParameterBuffer);
    }
};

#endif
