#ifndef GVM_THREE_WEBGL_POSTPROCESSING_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebglPostprocessingData.hpp"

using namespace UGL;

/** Stores the camera transform shared by one transition Scene. */
struct WebglPostprocessingTransitionObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
};

/** Stores one deterministic InstancedMesh transform and scalar vertex color. */
struct WebglPostprocessingTransitionInstanceData
{
    float4x4 model;
    float4 color;
};

/** Stores the shared white Phong material constants. */
struct WebglPostprocessingTransitionMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores the runtime DotScreen uniforms to preserve WebGL shader evaluation. */
struct WebglPostprocessingTransitionEffectParameters
{
    float4 angleScaleAndTextureSize;
    float4 transitionThresholdUseTexture;
};

/** Defines the per-Scene RenderSet ABI for one transition source scene. */
struct WebglPostprocessingTransitionSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebglPostprocessingTransitionVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingTransitionObjectData> objects,
        BufferComponent<WebglPostprocessingTransitionInstanceData> instances,
        BufferComponent<WebglPostprocessingTransitionMaterialData> materials)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebglPostprocessingTransitionScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebglPostprocessingTransitionEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Binds both half-float source Scenes, the pinned transition texture, and controls. */
struct WebglPostprocessingTransitionMixResources final : public IBindGroup
{
    /** Declares source A/B, uploaded transition texture, sampler, and controls. */
    constructor(
        Texture2D<half4> sceneA [[Binding0]],
        Sampler sceneASampler [[Binding1]],
        Texture2D<half4> sceneB [[Binding2]],
        Sampler sceneBSampler [[Binding3]],
        Texture2D<half4> mask [[Binding4]],
        Sampler maskSampler [[Binding5]],
        UniformBuffer<WebglPostprocessingTransitionEffectParameters>
            parameters [[Binding6]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebglPostprocessingTransitionSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float3 instanceColor [[Attribute2]];
};

/** Carries fullscreen coordinates. */
struct WebglPostprocessingTransitionScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebglPostprocessingTransitionLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebglPostprocessingTransitionLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the intermediate mask produced by the transition shader. */
struct WebglPostprocessingTransitionMaskFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebglPostprocessingTransitionOutputFrameBuffer final : public IFrameBuffer
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
WebglPostprocessingTransitionScreenOutput webglPostprocessingFullscreen(uint vertexID)
{
    const float2 positionUv = float2(
        (vertexID << 1u) & 2u,
        vertexID & 2u);
    WebglPostprocessingTransitionScreenOutput outputValue;
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

/** Draws all 100 ordinary child Mesh entities through the unique Scene Set. */
class WebglPostprocessingTransitionSceneAPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingTransitionSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingTransitionSceneOutput vertex(
        WebglPostprocessingTransitionVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingTransitionObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingTransitionInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float4 localPosition = mul(instanceData.model, inputValue.position);
        const float4 viewPosition =
            mul(objectData.modelView, localPosition);
        // The host stores the projection-only matrix in this component while
        // modelView is supplied separately; applying it to viewPosition gives
        // the intended projection * modelView transform.
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingTransitionSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            mul(instanceData.model, float4(inputValue.normal.xyz, 0.0f))).xyz;
        outputValue.instanceColor = instanceData.color.xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingTransitionLinearFrameBuffer fragment(
        WebglPostprocessingTransitionSceneOutput inputValue)
    {
        if (inputValue.viewPosition.z >= -1.0f)
            discard_fragment();
        // The clip-space Y inversion used by the host reverses the screen
        // derivative winding; restore Three's front-face normal before light
        // and half-vector evaluation.
        float3 normal = -normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        if (dot(normal, viewDirection) < 0.0f)
            discard_fragment();
        const float3 lightDirection =
            normalize(float3(0.0f, 1.0f, 4.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(0.3183098861837907f);
        const float3 indirectDiffuse =
            float3(1.20593334f) * float3(0.3183098861837907f);
        const float distribution =
            0.3183098861837907f * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        const WebglPostprocessingTransitionMaterialData materialData =
            sceneSet->materials->get(0u, 0u);
        const float3 diffuseColor =
            materialData.diffuseAndShininess.xyz * inputValue.instanceColor;
        // MeshPhong keeps the white specular lobe independent from the
        // diffuse base color; multiplying the whole lighting sum by red/blue
        // incorrectly removes the neutral highlights.
        float3 linearColor =
            (directDiffuse + indirectDiffuse) * diffuseColor + directSpecular;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingTransitionLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingTransitionSceneBPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingTransitionSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingTransitionSceneOutput vertex(
        WebglPostprocessingTransitionVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingTransitionObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingTransitionInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float4 localPosition = mul(instanceData.model, inputValue.position);
        const float4 viewPosition =
            mul(objectData.modelView, localPosition);
        // The host stores the projection-only matrix in this component while
        // modelView is supplied separately; applying it to viewPosition gives
        // the intended projection * modelView transform.
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingTransitionSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            mul(instanceData.model, float4(inputValue.normal.xyz, 0.0f))).xyz;
        outputValue.instanceColor = instanceData.color.xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingTransitionLinearFrameBuffer fragment(
        WebglPostprocessingTransitionSceneOutput inputValue)
    {
        if (inputValue.viewPosition.z >= -1.0f)
            discard_fragment();
        float3 normal = -normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(0.0f, 1.0f, 4.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(0.3183098861837907f);
        const float3 indirectDiffuse =
            float3(1.20593334f) * float3(0.3183098861837907f);
        const float distribution =
            0.3183098861837907f * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        const WebglPostprocessingTransitionMaterialData materialData =
            sceneSet->materials->get(0u, 0u);
        const float3 diffuseColor =
            materialData.diffuseAndShininess.xyz * inputValue.instanceColor;
        float3 linearColor =
            (directDiffuse + indirectDiffuse) * diffuseColor + directSpecular;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingTransitionLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingTransitionMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingTransitionSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingTransitionSceneOutput vertex(
        WebglPostprocessingTransitionVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingTransitionObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingTransitionInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float4 localPosition = mul(instanceData.model, inputValue.position);
        const float4 viewPosition =
            mul(objectData.modelView, localPosition);
        // The host stores the projection-only matrix in this component while
        // modelView is supplied separately; applying it to viewPosition gives
        // the intended projection * modelView transform.
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingTransitionSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            mul(instanceData.model, float4(inputValue.normal.xyz, 0.0f))).xyz;
        outputValue.instanceColor = instanceData.color.xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingTransitionLinearFrameBuffer fragment(
        WebglPostprocessingTransitionSceneOutput inputValue)
    {
        float3 normal = -normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(0.0f, 1.0f, 4.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(0.3183098861837907f);
        const float3 indirectDiffuse =
            float3(1.20593334f) * float3(0.3183098861837907f);
        const float distribution =
            0.3183098861837907f * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        const WebglPostprocessingTransitionMaterialData materialData =
            sceneSet->materials->get(0u, 0u);
        const float3 diffuseColor =
            materialData.diffuseAndShininess.xyz * inputValue.instanceColor;
        float3 linearColor =
            (directDiffuse + indirectDiffuse) * diffuseColor + directSpecular;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingTransitionLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy EffectComposer DotScreenShader at scale four. */
class WebglPostprocessingTransitionDotScreenPass final : public IRenderClass
{
public:
    /** Binds the Scene texture without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingTransitionScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingTransitionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Reproduces DotScreenShader's rotated sine-product pattern. */
    WebglPostprocessingTransitionLinearColorFrameBuffer fragment(
        WebglPostprocessingTransitionScreenOutput inputValue)
    {
        const float2 effectUv =
            float2(inputValue.uv.x, 1.0f - inputValue.uv.y);
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float angle =
            resources->parameters->angleScaleAndTextureSize.x;
        const float sineValue = sin(angle);
        const float cosineValue = cos(angle);
        const float2 textureCoordinate = float2(
            effectUv.x *
                    resources->parameters->angleScaleAndTextureSize.z - 0.5f,
            effectUv.y *
                    resources->parameters->angleScaleAndTextureSize.w - 0.5f);
        const float2 patternCoordinate = float2(
            cosineValue * textureCoordinate.x -
                sineValue * textureCoordinate.y,
                sineValue * textureCoordinate.x +
                cosineValue * textureCoordinate.y) *
            resources->parameters->angleScaleAndTextureSize.y;
        const float pattern =
            sin(patternCoordinate.x) * sin(patternCoordinate.y) * 4.0f;
        const float average =
            (sourceColor.r + sourceColor.g + sourceColor.b) / 3.0f;
        WebglPostprocessingTransitionLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(float3(average * 10.0f - 5.0f + pattern)),
            half(sourceColor.a));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebglPostprocessingTransitionRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingTransitionScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingTransitionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebglPostprocessingTransitionLinearColorFrameBuffer fragment(
        WebglPostprocessingTransitionScreenOutput inputValue)
    {
        const float2 offset = float2(0.0015f, 0.0f);
        const float4 redSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset));
        const float4 centerSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float4 blueSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset));
        WebglPostprocessingTransitionLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(redSample.r), half(centerSample.g),
            half(blueSample.b), half(centerSample.a));
        return frameBuffer;
    }
};

/** Produces the deterministic single-sample transition mask in a fullscreen pass. */
class WebglPostprocessingTransitionMaskPass final : public IRenderClass
{
public:
    /** Uses the ordinary fullscreen triangle without depth or blending. */
    constructor()
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingTransitionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Evaluates the private deterministic mask used when the GUI enables textures. */
    WebglPostprocessingTransitionMaskFrameBuffer fragment(
        WebglPostprocessingTransitionScreenOutput inputValue)
    {
        const float2 centered = inputValue.uv - float2(0.5f, 0.5f);
        const float radial = saturate(1.0f - length(centered) * 1.41421356f);
        const float cells = sin(inputValue.uv.x * 37.0f) *
            sin(inputValue.uv.y * 29.0f);
        const float mask = saturate(radial * 0.75f + cells * 0.125f + 0.25f);
        WebglPostprocessingTransitionMaskFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(mask), half(1.0f));
        return frameBuffer;
    }
};

/** Mixes two independent Scene targets using the r185 threshold transition equation. */
class WebglPostprocessingTransitionMixPass final : public IRenderClass
{
public:
    /** Binds both source Scenes and the generated mask. */
    constructor(
        BindGroup<WebglPostprocessingTransitionMixResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingTransitionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Applies texture-threshold or linear transition mixing. */
    WebglPostprocessingTransitionLinearColorFrameBuffer fragment(
        WebglPostprocessingTransitionScreenOutput inputValue)
    {
        const float4 texelA = float4(resources->sceneA->sample(
            resources->sceneASampler, inputValue.uv));
        const float4 texelB = float4(resources->sceneB->sample(
            resources->sceneBSampler, inputValue.uv));
        const float mixRatio = saturate(
            resources->parameters->transitionThresholdUseTexture.x);
        const float threshold = max(
            resources->parameters->transitionThresholdUseTexture.y, 0.0001f);
        const bool useTexture = resources->parameters->
            transitionThresholdUseTexture.z > 0.5f;
        float4 result;
        if (useTexture)
        {
            const float mask = float4(resources->mask->sample(
                resources->maskSampler, inputValue.uv)).r;
            const float thresholdCenter =
                mixRatio * (1.0f + threshold * 2.0f) - threshold;
            const float mixFactor = saturate(
                (mask - thresholdCenter) / threshold);
            result = lerp(texelA, texelB, mixFactor);
        }
        else
        {
            result = lerp(texelB, texelA, mixRatio);
        }
        WebglPostprocessingTransitionLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(result);
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebglPostprocessingTransitionOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingTransitionScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingTransitionScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebglPostprocessingTransitionOutputFrameBuffer fragment(
        WebglPostprocessingTransitionScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebglPostprocessingTransitionOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingLinearToSrgb(sourceColor.r)),
            half(webglPostprocessingLinearToSrgb(sourceColor.g)),
            half(webglPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebglPostprocessingTransitionRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    /** RenderSet instance for the source transition scene A. */
    [[Export]] RenderSet<WebglPostprocessingTransitionSceneRenderSet> sceneSetA;
    /** RenderSet instance for the source transition scene B. */
    [[Export]] RenderSet<WebglPostprocessingTransitionSceneRenderSet> sceneSetB;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTextureB;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepthB;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> maskTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> transitionTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> mixTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> dotTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> shiftedTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler linearSampler;
    Buffer<WebglPostprocessingTransitionEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebglPostprocessingTransitionScreenResources> sceneScreenResources;
    BindGroup<WebglPostprocessingTransitionScreenResources> sceneBScreenResources;
    BindGroup<WebglPostprocessingTransitionScreenResources> dotScreenResources;
    BindGroup<WebglPostprocessingTransitionScreenResources> shiftedScreenResources;
    BindGroup<WebglPostprocessingTransitionMixResources> mixResources;
    RenderClass<WebglPostprocessingTransitionSceneAPass> sceneAPass;
    RenderClass<WebglPostprocessingTransitionSceneBPass> sceneBPass;
    RenderClass<WebglPostprocessingTransitionDotScreenPass> dotPass;
    RenderClass<WebglPostprocessingTransitionRgbShiftPass> rgbShiftPass;
    RenderClass<WebglPostprocessingTransitionMaskPass> maskPass;
    RenderClass<WebglPostprocessingTransitionMixPass> mixPass;
    RenderClass<WebglPostprocessingTransitionOutputPass> directOutputPassA;
    RenderClass<WebglPostprocessingTransitionOutputPass> directOutputPassB;
    RenderClass<WebglPostprocessingTransitionOutputPass> outputPass;
    uint width = 800u;
    uint height = 500u;
    WebglPostprocessingTransitionEffectParameters effectParameters{};

public:
    /** Creates one RenderSet and Scene pass for each independent transition scene. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSetA =
            device->createRenderSet<WebglPostprocessingTransitionSceneRenderSet>();
        sceneSetB =
            device->createRenderSet<WebglPostprocessingTransitionSceneRenderSet>();
        sceneAPass = device->createRenderClass<WebglPostprocessingTransitionSceneAPass>(
            sceneSetA);
        sceneBPass = device->createRenderClass<WebglPostprocessingTransitionSceneBPass>(
            sceneSetB);
        linearSampler = device->createSampler({
            .label = "WebglPostprocessingTransitionLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        effectParameterBuffer = device->createBuffer(
            "WebglPostprocessingTransitionEffectParameters", 1u);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebglPostprocessingTransitionScene", width, height, 1u);
        sceneTextureB = device->createTexture(
            "WebglPostprocessingTransitionSceneB", width, height, 1u);
        sceneDepthB = device->createTexture(
            "WebglPostprocessingTransitionDepthB", width, height, 1u);
        maskTexture = device->createTexture(
            "WebglPostprocessingTransitionMask", width, height, 1u);
        // TextureLoader produces a 512x512 raw RGB transition image.  Keep
        // this upload as an ordinary single-sample texture; no antialiasing
        // or synthetic MSAA path is involved.
        transitionTexture = device->createTexture(
            "WebglPostprocessingTransitionTexture", 512u, 512u, 1u);
        mixTexture = device->createTexture(
            "WebglPostprocessingTransitionMix", width, height, 1u);
        dotTexture = device->createTexture(
            "WebglPostprocessingTransitionDot", width, height, 1u);
        shiftedTexture = device->createTexture(
            "WebglPostprocessingTransitionShifted", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglPostprocessingTransitionDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingTransitionOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebglPostprocessingTransitionScreenResources>(
            sceneTexture->createView(), linearSampler,
            effectParameterBuffer);
        sceneBScreenResources = device->createBindGroup<
            WebglPostprocessingTransitionScreenResources>(
            sceneTextureB->createView(), linearSampler,
            effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebglPostprocessingTransitionScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        shiftedScreenResources = device->createBindGroup<
            WebglPostprocessingTransitionScreenResources>(
                shiftedTexture->createView(), linearSampler,
                effectParameterBuffer);
        effectParameters = {};
        // DotScreenShader retains its default tSize uniform; EffectComposer
        // only replaces tDiffuse when the pass is rendered.
        effectParameters.angleScaleAndTextureSize =
            float4(1.57f, 4.0f, float(width), float(height));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &effectParameters, sizeof(effectParameters))->submit();
        dotPass = device->createRenderClass<
            WebglPostprocessingTransitionDotScreenPass>(sceneScreenResources);
        rgbShiftPass = device->createRenderClass<
            WebglPostprocessingTransitionRgbShiftPass>(dotScreenResources);
        mixResources = device->createBindGroup<
            WebglPostprocessingTransitionMixResources>(
                sceneTexture->createView(), linearSampler,
                sceneTextureB->createView(), linearSampler,
                transitionTexture->createView(), linearSampler,
                effectParameterBuffer);
        maskPass = device->createRenderClass<
            WebglPostprocessingTransitionMaskPass>();
        mixPass = device->createRenderClass<
            WebglPostprocessingTransitionMixPass>(mixResources);
        outputPass = device->createRenderClass<
            WebglPostprocessingTransitionOutputPass>(
                device->createBindGroup<WebglPostprocessingTransitionScreenResources>(
                    mixTexture->createView(), linearSampler,
                    effectParameterBuffer));
        directOutputPassA = device->createRenderClass<
            WebglPostprocessingTransitionOutputPass>(sceneScreenResources);
        directOutputPassB = device->createRenderClass<
            WebglPostprocessingTransitionOutputPass>(sceneBScreenResources);
    }

    /** Updates the transition factor and texture-threshold toggle for one replay state. */
    void configureTransition(float mixRatio, bool useTexture)
    {
        effectParameters.transitionThresholdUseTexture = float4(
            mixRatio, 0.1f, useTexture ? 1.0f : 0.0f, 0.0f);
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &effectParameters, sizeof(effectParameters))->submit();
    }

    /** Uploads one pinned RGBA8 transition texture decoded by the host. */
    void configureTransitionTexture(
        const eastl::vector<uint8_t> &pixels,
        uint textureWidth,
        uint textureHeight)
    {
        (void)textureWidth;
        (void)textureHeight;
        graphicsQueue->writeTexture(
            transitionTexture, pixels.data(),
            uint64_t(pixels.size()), 0u)->submit();
    }

    /** Renders both declared Scene invocations, then the transition composite and output. */
    void render() override
    {
        sceneSetA->update();
        sceneSetB->update();
        WebglPostprocessingTransitionLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingTransitionLinearFrameBuffer sceneBFrame;
        sceneBFrame.color = sceneTextureB->createView();
        sceneBFrame.color.loadOp = LoadOp::Clear;
        sceneBFrame.color.storeOp = StoreOp::Store;
        sceneBFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneBFrame.depth = sceneDepthB->createView();
        sceneBFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneBFrame.depth.depthStoreOp = StoreOp::Store;
        sceneBFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingTransitionMaskFrameBuffer maskFrame;
        maskFrame.color = maskTexture->createView();
        maskFrame.color.loadOp = LoadOp::Clear;
        maskFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingTransitionLinearColorFrameBuffer mixFrame;
        mixFrame.color = mixTexture->createView();
        mixFrame.color.loadOp = LoadOp::Clear;
        mixFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingTransitionOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        const float mixRatio = effectParameters.transitionThresholdUseTexture.x;
        if (mixRatio <= 0.0001f)
        {
            // Match the upstream transition==0 composer bypass with a direct
            // Scene B target and the same output color conversion.
            graphicsQueue
                ->renderPass("WebglPostprocessingTransitionSceneB", sceneBFrame,
                    sceneBPass())
                ->renderPass(
                    "WebglPostprocessingTransitionDirectOutputB", outputFrame,
                    directOutputPassB(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture, outputTexture, RenderToSwapchainDescriptor{})
                ->submit();
        }
        else if (mixRatio >= 0.9999f)
        {
            // Match the upstream transition==1 composer bypass with a direct
            // Scene A target and the same output color conversion.
            graphicsQueue
                ->renderPass("WebglPostprocessingTransitionSceneA", sceneFrame,
                    sceneAPass())
                ->renderPass(
                    "WebglPostprocessingTransitionDirectOutputA", outputFrame,
                    directOutputPassA(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture, outputTexture, RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue
                ->renderPass("WebglPostprocessingTransitionSceneA", sceneFrame,
                    sceneAPass())
                ->renderPass("WebglPostprocessingTransitionSceneB", sceneBFrame,
                    sceneBPass())
                ->renderPass("WebglPostprocessingTransitionMask", maskFrame,
                    maskPass(3u, 1u, 0u, 0u))
                ->renderPass("WebglPostprocessingTransitionMix", mixFrame,
                    mixPass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglPostprocessingTransitionOutput", outputFrame,
                    outputPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture, outputTexture, RenderToSwapchainDescriptor{})
                ->submit();
        }
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
        sceneSetA->destroy();
        sceneSetB->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(sceneTextureB);
        device->freeTexture(sceneDepthB);
        device->freeTexture(maskTexture);
        device->freeTexture(transitionTexture);
        device->freeTexture(mixTexture);
        device->freeTexture(dotTexture);
        device->freeTexture(shiftedTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
