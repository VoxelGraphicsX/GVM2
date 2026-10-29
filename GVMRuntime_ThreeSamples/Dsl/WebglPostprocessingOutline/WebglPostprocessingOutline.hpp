#ifndef GVM_THREE_WEBGL_POSTPROCESSING_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebglPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebglPostprocessingOutlineObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
};

/** Stores the mandatory identity instance payload. */
struct WebglPostprocessingOutlineInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebglPostprocessingOutlineMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores selection and outline phase flags for the multi-pass redraw. */
struct WebglPostprocessingOutlineRenderFlags
{
    uint4 values;
};

/** Stores the runtime DotScreen uniforms to preserve WebGL shader evaluation. */
struct WebglPostprocessingOutlineEffectParameters
{
    float4 angleScaleAndTextureSize;
};

/** Defines the unique Scene RenderSet containing 100 ordinary Mesh entities. */
struct WebglPostprocessingOutlineSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebglPostprocessingOutlineVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingOutlineObjectData> objects,
        BufferComponent<WebglPostprocessingOutlineInstanceData> instances,
        BufferComponent<WebglPostprocessingOutlineMaterialData> materials,
        BufferComponent<WebglPostprocessingOutlineRenderFlags> renderFlags)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebglPostprocessingOutlineScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebglPostprocessingOutlineEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebglPostprocessingOutlineSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
};

/** Carries fullscreen coordinates. */
struct WebglPostprocessingOutlineScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebglPostprocessingOutlineLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebglPostprocessingOutlineLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebglPostprocessingOutlineOutputFrameBuffer final : public IFrameBuffer
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
WebglPostprocessingOutlineScreenOutput webglPostprocessingFullscreen(uint vertexID)
{
    const float2 positionUv = float2(
        (vertexID << 1u) & 2u,
        vertexID & 2u);
    WebglPostprocessingOutlineScreenOutput outputValue;
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
class WebglPostprocessingOutlineShadowPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingOutlineMainFrontPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingOutlineMainDoublePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingOutlineNonselectedDepthPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingOutlineSelectedMaskPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebglPostprocessingOutlineMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingOutlineSceneOutput vertex(
        WebglPostprocessingOutlineVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingOutlineObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingOutlineInstanceData instanceData =
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
        WebglPostprocessingOutlineSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebglPostprocessingOutlineLinearFrameBuffer fragment(
        WebglPostprocessingOutlineSceneOutput inputValue)
    {
        float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(-1.0f, -1.0f, -1.0f));
        const float dotNormalLight = saturate(dot(normal, lightDirection));
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalHalf =
            saturate(dot(normal, halfDirection));
        const float dotViewHalf =
            saturate(dot(viewDirection, halfDirection));
        const float inversePi = 0.3183098861837907f;
        const float3 directIrradiance =
            float3(dotNormalLight) * float3(3.0f);
        const float3 directDiffuse =
            directIrradiance * float3(inversePi);
        const float3 indirectDiffuse =
            float3(0.60382736f) * float3(inversePi);
        const float distribution =
            inversePi * 16.0f * pow(dotNormalHalf, 30.0f);
        const float3 directSpecular =
            directIrradiance *
            webglPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        float3 linearColor =
            directDiffuse + directSpecular + indirectDiffuse;
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= 1.0f - fogFactor;
        WebglPostprocessingOutlineLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy EffectComposer DotScreenShader at scale four. */
class WebglPostprocessingOutlineDotScreenPass final : public IRenderClass
{
public:
    /** Binds the Scene texture without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingOutlineScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingOutlineScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Computes a luma edge mask for the outline postprocess. */
    WebglPostprocessingOutlineLinearColorFrameBuffer fragment(
        WebglPostprocessingOutlineScreenOutput inputValue)
    {
        const float2 texel = float2(
            1.0f / max(resources->parameters->angleScaleAndTextureSize.z, 1.0f),
            1.0f / max(resources->parameters->angleScaleAndTextureSize.w, 1.0f));
        const float lumaCenter = dot(float3(resources->source->sample(
            resources->sourceSampler, inputValue.uv).rgb),
            float3(0.2126f, 0.7152f, 0.0722f));
        const float lumaX = dot(float3(resources->source->sample(
            resources->sourceSampler, inputValue.uv + float2(texel.x, 0.0f)).rgb),
            float3(0.2126f, 0.7152f, 0.0722f)) - dot(float3(resources->source->sample(
            resources->sourceSampler, inputValue.uv - float2(texel.x, 0.0f)).rgb),
            float3(0.2126f, 0.7152f, 0.0722f));
        const float lumaY = dot(float3(resources->source->sample(
            resources->sourceSampler, inputValue.uv + float2(0.0f, texel.y)).rgb),
            float3(0.2126f, 0.7152f, 0.0722f)) - dot(float3(resources->source->sample(
            resources->sourceSampler, inputValue.uv - float2(0.0f, texel.y)).rgb),
            float3(0.2126f, 0.7152f, 0.0722f));
        const float edge = saturate(length(float2(lumaX, lumaY)) * 8.0f);
        WebglPostprocessingOutlineLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(lerp(float3(lumaCenter), float3(0.0f), edge)), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebglPostprocessingOutlineRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingOutlineScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingOutlineScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebglPostprocessingOutlineLinearColorFrameBuffer fragment(
        WebglPostprocessingOutlineScreenOutput inputValue)
    {
        const float2 offset = float2(0.0015f, 0.0f);
        const float4 redSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset));
        const float4 centerSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float4 blueSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset));
        WebglPostprocessingOutlineLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(redSample.r), half(centerSample.g),
            half(blueSample.b), half(centerSample.a));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebglPostprocessingOutlineOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingOutlineScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingOutlineScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebglPostprocessingOutlineOutputFrameBuffer fragment(
        WebglPostprocessingOutlineScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebglPostprocessingOutlineOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingLinearToSrgb(sourceColor.r)),
            half(webglPostprocessingLinearToSrgb(sourceColor.g)),
            half(webglPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebglPostprocessingOutlineRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingOutlineSceneRenderSet> sceneSet;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
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
    Buffer<WebglPostprocessingOutlineEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebglPostprocessingOutlineScreenResources> sceneScreenResources;
    BindGroup<WebglPostprocessingOutlineScreenResources> dotScreenResources;
    BindGroup<WebglPostprocessingOutlineScreenResources> shiftedScreenResources;
    RenderClass<WebglPostprocessingOutlineShadowPass> shadowPass;
    RenderClass<WebglPostprocessingOutlineMainFrontPass> mainFrontPass;
    RenderClass<WebglPostprocessingOutlineMainDoublePass> mainDoublePass;
    RenderClass<WebglPostprocessingOutlineNonselectedDepthPass> nonselectedDepthPass;
    RenderClass<WebglPostprocessingOutlineSelectedMaskPass> selectedMaskPass;
    RenderClass<WebglPostprocessingOutlineDotScreenPass> dotPass;
    RenderClass<WebglPostprocessingOutlineRgbShiftPass> rgbShiftPass;
    RenderClass<WebglPostprocessingOutlineOutputPass> outputPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene RenderSet and its Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglPostprocessingOutlineSceneRenderSet>();
        shadowPass = device->createRenderClass<WebglPostprocessingOutlineShadowPass>(
            sceneSet);
        mainFrontPass = device->createRenderClass<WebglPostprocessingOutlineMainFrontPass>(
            sceneSet);
        mainDoublePass = device->createRenderClass<WebglPostprocessingOutlineMainDoublePass>(
            sceneSet);
        nonselectedDepthPass = device->createRenderClass<WebglPostprocessingOutlineNonselectedDepthPass>(
            sceneSet);
        selectedMaskPass = device->createRenderClass<WebglPostprocessingOutlineSelectedMaskPass>(
            sceneSet);
        linearSampler = device->createSampler({
            .label = "WebglPostprocessingOutlineLinearSampler",
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
            "WebglPostprocessingOutlineEffectParameters", 1u);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebglPostprocessingOutlineScene", width, height, 1u);
        dotTexture = device->createTexture(
            "WebglPostprocessingOutlineDot", width, height, 1u);
        shiftedTexture = device->createTexture(
            "WebglPostprocessingOutlineShifted", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglPostprocessingOutlineDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingOutlineOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebglPostprocessingOutlineScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebglPostprocessingOutlineScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        shiftedScreenResources = device->createBindGroup<
            WebglPostprocessingOutlineScreenResources>(
                shiftedTexture->createView(), linearSampler,
                effectParameterBuffer);
        WebglPostprocessingOutlineEffectParameters parameters;
        // DotScreenShader retains its default tSize uniform; EffectComposer
        // only replaces tDiffuse when the pass is rendered.
        parameters.angleScaleAndTextureSize =
            float4(1.57f, 4.0f, float(width), float(height));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
        dotPass = device->createRenderClass<
            WebglPostprocessingOutlineDotScreenPass>(sceneScreenResources);
        rgbShiftPass = device->createRenderClass<
            WebglPostprocessingOutlineRgbShiftPass>(dotScreenResources);
        outputPass = device->createRenderClass<
            WebglPostprocessingOutlineOutputPass>(shiftedScreenResources);
    }

    /** Renders the Set once, then DotScreen, RGBShift, output, and Present. */
    void render() override
    {
        sceneSet->update();
        WebglPostprocessingOutlineLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingOutlineLinearFrameBuffer sceneLoadFrame;
        sceneLoadFrame.color = sceneTexture->createView();
        sceneLoadFrame.color.loadOp = LoadOp::Load;
        sceneLoadFrame.color.storeOp = StoreOp::Store;
        sceneLoadFrame.depth = sceneDepth->createView();
        sceneLoadFrame.depth.depthLoadOp = LoadOp::Load;
        sceneLoadFrame.depth.depthStoreOp = StoreOp::Store;
        WebglPostprocessingOutlineLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingOutlineLinearColorFrameBuffer shiftedFrame;
        shiftedFrame.color = shiftedTexture->createView();
        shiftedFrame.color.loadOp = LoadOp::Clear;
        shiftedFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingOutlineOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglPostprocessingOutlineShadow", sceneFrame, shadowPass())
            ->renderPass("WebglPostprocessingOutlineMainFront", sceneLoadFrame,
                mainFrontPass())
            ->renderPass("WebglPostprocessingOutlineMainDouble", sceneLoadFrame,
                mainDoublePass())
            ->renderPass("WebglPostprocessingOutlineNonselectedDepth", sceneLoadFrame,
                nonselectedDepthPass())
            ->renderPass("WebglPostprocessingOutlineSelectedMask", sceneLoadFrame,
                selectedMaskPass())
            ->renderPass(
                "WebglPostprocessingOutlineDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglPostprocessingOutlineRgbShift", shiftedFrame,
                rgbShiftPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglPostprocessingOutlineOutput", outputFrame,
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
        device->freeTexture(shiftedTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
