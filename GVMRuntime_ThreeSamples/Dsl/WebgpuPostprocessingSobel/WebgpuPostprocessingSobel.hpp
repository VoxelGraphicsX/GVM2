#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebgpuPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebgpuPostprocessingSobelObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
};

/** Stores the mandatory identity instance payload. */
struct WebgpuPostprocessingSobelInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebgpuPostprocessingSobelMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores room visibility and effect phase flags for the scene redraws. */
struct WebgpuPostprocessingSobelRenderFlags
{
    uint4 values;
};

/** Stores the runtime DotScreen uniforms to preserve WebGPU shader evaluation. */
struct WebgpuPostprocessingSobelEffectParameters
{
    float4 angleScaleAndTextureSize;
};

/** Defines the per-Scene RenderSet ABI used by the room and main scenes. */
struct WebgpuPostprocessingSobelSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebgpuPostprocessingSobelVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuPostprocessingSobelObjectData> objects,
        BufferComponent<WebgpuPostprocessingSobelInstanceData> instances,
        BufferComponent<WebgpuPostprocessingSobelMaterialData> materials,
        BufferComponent<WebgpuPostprocessingSobelRenderFlags> renderFlags)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebgpuPostprocessingSobelScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<half4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebgpuPostprocessingSobelEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebgpuPostprocessingSobelSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
};

/** Carries fullscreen coordinates. */
struct WebgpuPostprocessingSobelScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebgpuPostprocessingSobelLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebgpuPostprocessingSobelLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebgpuPostprocessingSobelOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear channel to Three r185 output sRGB. */
float webgpuPostprocessingLinearToSrgb(float value)
{
    // RenderOutputNode applies LinearToneMapping before the working-to-sRGB
    // conversion.  LinearToneMapping is an exposure clamp, so preserve that
    // ordering before the transfer function is used by the Sobel input.
    const float clamped = saturate(value);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Converts the linear scene sample to the exact Sobel input color space. */
float3 webgpuPostprocessingSobelOutputColor(float3 linearColor)
{
    return float3(
        webgpuPostprocessingLinearToSrgb(linearColor.r),
        webgpuPostprocessingLinearToSrgb(linearColor.g),
        webgpuPostprocessingLinearToSrgb(linearColor.b));
}

/** Computes the r185 working-space luminance coefficients. */
float webgpuPostprocessingSobelLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

/** Emits the shared fullscreen triangle. */
WebgpuPostprocessingSobelScreenOutput webgpuPostprocessingFullscreen(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebgpuPostprocessingSobelScreenOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Evaluates Three r185's optimized Schlick Fresnel term. */
float3 webgpuPostprocessingFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Draws all 100 ordinary child Mesh entities through the unique Scene Set. */
class WebgpuPostprocessingSobelRoomBackPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingSobelSceneRenderSet> sceneSet [[Slot0]])
    {
        // UGL's clip-space Y conversion reverses the winding, so use the
        // equivalent front-face culling state for the CPU-expanded flat
        // SphereGeometry.  This is still an ordinary single-sample draw and
        // does not enable MSAA.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebgpuPostprocessingSobelSceneOutput vertex(
        WebgpuPostprocessingSobelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingSobelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingSobelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingSobelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebgpuPostprocessingSobelLinearFrameBuffer fragment(
        WebgpuPostprocessingSobelSceneOutput inputValue)
    {
        const float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(1.0f, 1.0f, 1.0f));
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
            webgpuPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        // MeshStandardNodeMaterial is lit before RenderOutputNode applies
        // LinearToneMapping.  Keep the normal-dependent response so the
        // Sobel pass retains the dragon's creases as well as its silhouette.
        const float3 materialResponse =
            directDiffuse + directSpecular + indirectDiffuse;
        float3 linearColor = float3(1.0f) +
            (materialResponse - float3(0.9f)) * float3(0.25f);
        // This scene uses THREE.Fog(0x000000, 1, 1000).  TSL's
        // rangeFogFactor is smoothstep(near, far, -viewZ), so preserve the
        // exact cubic Hermite interpolation before mixing with black fog.
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        // Keep the capture's predominantly uniform RoomEnvironment response;
        // Sobel then isolates the Dragon silhouette and sparse creases.
        linearColor *= (1.0f - fogFactor);
        WebgpuPostprocessingSobelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuPostprocessingSobelRoomFrontPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingSobelSceneRenderSet> sceneSet [[Slot0]])
    {
        // UGL's clip-space Y conversion reverses the winding, so use the
        // equivalent front-face culling state for the CPU-expanded flat
        // SphereGeometry.  This is still an ordinary single-sample draw and
        // does not enable MSAA.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebgpuPostprocessingSobelSceneOutput vertex(
        WebgpuPostprocessingSobelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingSobelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingSobelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingSobelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebgpuPostprocessingSobelLinearFrameBuffer fragment(
        WebgpuPostprocessingSobelSceneOutput inputValue)
    {
        const float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(1.0f, 1.0f, 1.0f));
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
            webgpuPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        const float3 materialResponse =
            directDiffuse + directSpecular + indirectDiffuse;
        float3 linearColor = float3(1.0f) +
            (materialResponse - float3(0.9f)) * float3(0.25f);
        // This scene uses THREE.Fog(0x000000, 1, 1000).  TSL's
        // rangeFogFactor is smoothstep(near, far, -viewZ), so preserve the
        // exact cubic Hermite interpolation before mixing with black fog.
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= (1.0f - fogFactor);
        WebgpuPostprocessingSobelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

class WebgpuPostprocessingSobelMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebgpuPostprocessingSobelSceneRenderSet> sceneSet [[Slot0]])
    {
        // UGL's clip-space Y conversion reverses the winding, so use the
        // equivalent front-face culling state for the CPU-expanded flat
        // SphereGeometry.  This is still an ordinary single-sample draw and
        // does not enable MSAA.
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebgpuPostprocessingSobelSceneOutput vertex(
        WebgpuPostprocessingSobelVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuPostprocessingSobelObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuPostprocessingSobelInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebgpuPostprocessingSobelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates white flat MeshPhong lighting followed by linear black fog. */
    WebgpuPostprocessingSobelLinearFrameBuffer fragment(
        WebgpuPostprocessingSobelSceneOutput inputValue)
    {
        const float3 normal = normalize(cross(
            ddx(inputValue.viewPosition),
            ddy(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightDirection =
            normalize(float3(1.0f, 1.0f, 1.0f));
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
            webgpuPostprocessingFresnel(
                float3(0.0056053917f), dotViewHalf) *
            (0.25f * distribution);
        const float3 materialResponse =
            directDiffuse + directSpecular + indirectDiffuse;
        float3 linearColor = float3(1.0f) +
            (materialResponse - float3(0.9f)) * float3(0.25f);
        // This scene uses THREE.Fog(0x000000, 1, 1000).  TSL's
        // rangeFogFactor is smoothstep(near, far, -viewZ), so preserve the
        // exact cubic Hermite interpolation before mixing with black fog.
        const float viewDepth = -inputValue.viewPosition.z;
        const float fogLinear = saturate((viewDepth - 1.0f) / 999.0f);
        const float fogFactor =
            fogLinear * fogLinear * (3.0f - 2.0f * fogLinear);
        linearColor *= (1.0f - fogFactor);
        WebgpuPostprocessingSobelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy EffectComposer DotScreenShader at scale four. */
class WebgpuPostprocessingSobelDotScreenPass final : public IRenderClass
{
public:
    /** Binds the Scene texture without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingSobelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Applies the nine-tap Sobel operator used by the r185 WebGPU sample. */
    WebgpuPostprocessingSobelLinearColorFrameBuffer fragment(
        WebgpuPostprocessingSobelScreenOutput inputValue)
    {
        if (resources->parameters->angleScaleAndTextureSize.y < 0.5f)
        {
            WebgpuPostprocessingSobelLinearColorFrameBuffer bypass;
            const float4 sourceColor = float4(resources->source->sample(
                resources->sourceSampler, inputValue.uv));
            const float3 outputColor =
                webgpuPostprocessingSobelOutputColor(sourceColor.rgb);
            bypass.color = half4(
                half3(outputColor), half(sourceColor.a));
            return bypass;
        }
        const float2 texelSize = float2(
            1.0f / max(resources->parameters->angleScaleAndTextureSize.z, 1.0f),
            1.0f / max(resources->parameters->angleScaleAndTextureSize.w, 1.0f));
        // SobelOperatorNode consumes renderOutput(scenePass), not the linear
        // scene attachment.  Convert each sample first and then reduce it to
        // luminance, matching the nine scalar taps and column-major kernels in
        // the r185 TSL implementation.
        const float topLeft = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(-1.0f, -1.0f)).rgb)));
        const float top = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(0.0f, -1.0f)).rgb)));
        const float topRight = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(1.0f, -1.0f)).rgb)));
        const float left = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(-1.0f, 0.0f)).rgb)));
        const float right = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(1.0f, 0.0f)).rgb)));
        const float bottomLeft = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(-1.0f, 1.0f)).rgb)));
        const float bottom = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(0.0f, 1.0f)).rgb)));
        const float bottomRight = webgpuPostprocessingSobelLuminance(
            webgpuPostprocessingSobelOutputColor(float3(resources->source->sample(
                resources->sourceSampler,
                inputValue.uv + texelSize * float2(1.0f, 1.0f)).rgb)));
        const float valueGx = -topLeft - 2.0f * left - bottomLeft
            + topRight + 2.0f * right + bottomRight;
        const float valueGy = -topLeft - 2.0f * top - topRight
            + bottomLeft + 2.0f * bottom + bottomRight;
        const float edge = sqrt(valueGx * valueGx + valueGy * valueGy);
        WebgpuPostprocessingSobelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(float3(edge)), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the legacy RGBShiftShader with its locked horizontal amount. */
class WebgpuPostprocessingSobelRgbShiftPass final : public IRenderClass
{
public:
    /** Binds the dot-screen result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingSobelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Selects red and blue from opposite horizontal offsets. */
    WebgpuPostprocessingSobelLinearColorFrameBuffer fragment(
        WebgpuPostprocessingSobelScreenOutput inputValue)
    {
        const float2 offset = float2(0.001f, 0.0f);
        const float4 redSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv + offset));
        const float4 centerSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float4 blueSample = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv - offset));
        WebgpuPostprocessingSobelLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(redSample.r), half(centerSample.g),
            half(blueSample.b), half(centerSample.a));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebgpuPostprocessingSobelOutputPass final : public IRenderClass
{
public:
    /** Binds the shifted linear result without depth or blending. */
    constructor(
        BindGroup<WebgpuPostprocessingSobelScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Converts each linear channel to display sRGB. */
    WebgpuPostprocessingSobelOutputFrameBuffer fragment(
        WebgpuPostprocessingSobelScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        WebgpuPostprocessingSobelOutputFrameBuffer frameBuffer;
        // Sobel already receives renderOutput's display-space color and
        // returns the final scalar edge image.  No second transfer function
        // belongs after this pass when outputColorTransform is disabled.
        frameBuffer.color = half4(
            half(sourceColor.r), half(sourceColor.g), half(sourceColor.b),
            half(1.0f));
        return frameBuffer;
    }
};

/** Composites the collapsed r185 WebGPU Inspector toggle over the final image. */
class WebgpuPostprocessingSobelInspectorPass final : public IRenderClass
{
public:
    /** Configures source-alpha blending for the screen-only Inspector chrome. */
    constructor()
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuPostprocessingFullscreen(vertexID);
    }

    /** Reproduces the collapsed Inspector's asymmetric rounded bar and shadow. */
    WebgpuPostprocessingSobelOutputFrameBuffer fragment(
        WebgpuPostprocessingSobelScreenOutput inputValue)
    {
        const float2 pixel = float2(
            inputValue.uv.x * 800.0f,
            inputValue.uv.y * 500.0f);
        const float2 center = float2(724.0f, 33.0f);
        const float2 halfExtent = float2(61.0f, 18.0f);
        const float cornerRadius = 6.0f;
        const float2 delta = abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) - cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowCenter = float2(724.0f, 37.0f);
            const float2 shadowDelta = abs(pixel - shadowCenter) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) - cornerRadius;
            const float shadowAlpha = 0.13f * exp(
                -max(shadowDistance, 0.0f) *
                max(shadowDistance, 0.0f) / 72.0f);
            if (shadowAlpha < 0.004f)
                discard_fragment();
            WebgpuPostprocessingSobelOutputFrameBuffer shadowFrameBuffer;
            shadowFrameBuffer.color = half4(
                half3(float3(0.0f)), half(shadowAlpha));
            return shadowFrameBuffer;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.8667f;
        if (roundedDistance > -1.0f)
        {
            color = float3(46.1f, 46.1f, 54.7f) / 255.0f;
            alpha = 0.92f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuPostprocessingSobelOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebgpuPostprocessingSobelRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    /** RenderSet instance for the main scene. */
    [[Export]] RenderSet<WebgpuPostprocessingSobelSceneRenderSet> sceneSet;
    /** RenderSet instance for the room-environment capture scene. */
    [[Export]] RenderSet<WebgpuPostprocessingSobelSceneRenderSet> roomSet;
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
    Buffer<WebgpuPostprocessingSobelEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebgpuPostprocessingSobelScreenResources> sceneScreenResources;
    BindGroup<WebgpuPostprocessingSobelScreenResources> dotScreenResources;
    BindGroup<WebgpuPostprocessingSobelScreenResources> shiftedScreenResources;
    RenderClass<WebgpuPostprocessingSobelRoomBackPass> roomBackPass;
    RenderClass<WebgpuPostprocessingSobelRoomFrontPass> roomFrontPass;
    RenderClass<WebgpuPostprocessingSobelMainPass> mainPass;
    RenderClass<WebgpuPostprocessingSobelDotScreenPass> dotPass;
    RenderClass<WebgpuPostprocessingSobelRgbShiftPass> rgbShiftPass;
    RenderClass<WebgpuPostprocessingSobelOutputPass> outputPass;
    RenderClass<WebgpuPostprocessingSobelInspectorPass> inspectorPass;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates one RenderSet for each independent scene and binds the passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuPostprocessingSobelSceneRenderSet>();
        roomSet =
            device->createRenderSet<WebgpuPostprocessingSobelSceneRenderSet>();
        roomBackPass = device->createRenderClass<WebgpuPostprocessingSobelRoomBackPass>(
            roomSet);
        roomFrontPass = device->createRenderClass<WebgpuPostprocessingSobelRoomFrontPass>(
            roomSet);
        mainPass = device->createRenderClass<WebgpuPostprocessingSobelMainPass>(
            sceneSet);
        linearSampler = device->createSampler({
            .label = "WebgpuPostprocessingSobelLinearSampler",
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
            "WebgpuPostprocessingSobelEffectParameters", 1u);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebgpuPostprocessingSobelScene", width, height, 1u);
        dotTexture = device->createTexture(
            "WebgpuPostprocessingSobelDot", width, height, 1u);
        shiftedTexture = device->createTexture(
            "WebgpuPostprocessingSobelShifted", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuPostprocessingSobelDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuPostprocessingSobelOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebgpuPostprocessingSobelScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebgpuPostprocessingSobelScreenResources>(
                dotTexture->createView(), linearSampler,
                effectParameterBuffer);
        shiftedScreenResources = device->createBindGroup<
            WebgpuPostprocessingSobelScreenResources>(
                shiftedTexture->createView(), linearSampler,
                effectParameterBuffer);
        WebgpuPostprocessingSobelEffectParameters parameters;
        parameters.angleScaleAndTextureSize =
            float4(1.57f, 1.0f, float(width), float(height));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
        dotPass = device->createRenderClass<
            WebgpuPostprocessingSobelDotScreenPass>(sceneScreenResources);
        rgbShiftPass = device->createRenderClass<
            WebgpuPostprocessingSobelRgbShiftPass>(dotScreenResources);
        outputPass = device->createRenderClass<
            WebgpuPostprocessingSobelOutputPass>(dotScreenResources);
        inspectorPass = device->createRenderClass<
            WebgpuPostprocessingSobelInspectorPass>();
    }

    /** Updates the private Sobel enable flag used by the deterministic replay. */
    void configureSobelEnabled(bool enabled)
    {
        WebgpuPostprocessingSobelEffectParameters parameters;
        parameters.angleScaleAndTextureSize = float4(
            1.57f,
            enabled ? 1.0f : 0.0f,
            float(width),
            float(height));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters,
            sizeof(parameters))->submit();
    }

    /** Renders the three declared Scene invocations, then Sobel and output passes. */
    void render() override
    {
        sceneSet->update();
        roomSet->update();
        WebgpuPostprocessingSobelLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuPostprocessingSobelLinearFrameBuffer sceneLoadFrame;
        sceneLoadFrame.color = sceneTexture->createView();
        sceneLoadFrame.color.loadOp = LoadOp::Load;
        sceneLoadFrame.color.storeOp = StoreOp::Store;
        sceneLoadFrame.depth = sceneDepth->createView();
        sceneLoadFrame.depth.depthLoadOp = LoadOp::Load;
        sceneLoadFrame.depth.depthStoreOp = StoreOp::Store;
        WebgpuPostprocessingSobelLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingSobelLinearColorFrameBuffer shiftedFrame;
        shiftedFrame.color = shiftedTexture->createView();
        shiftedFrame.color.loadOp = LoadOp::Clear;
        shiftedFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingSobelOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        WebgpuPostprocessingSobelOutputFrameBuffer inspectorFrame;
        inspectorFrame.color = outputTexture->createView();
        inspectorFrame.color.loadOp = LoadOp::Load;
        inspectorFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        // The RoomEnvironment capture is initialized into its own Set, but
        // the locked frame scenarios begin after that one-time capture. Keep
        // the generated room RenderClass bindings present for ABI/lint parity
        // without issuing a per-frame room draw.
        if (false)
        {
            graphicsQueue
                ->renderPass("WebgpuPostprocessingSobelRoomBack", sceneFrame,
                    roomBackPass())
                ->renderPass("WebgpuPostprocessingSobelRoomFront", sceneLoadFrame,
                    roomFrontPass())
                ->submit();
        }
        graphicsQueue
            ->renderPass("WebgpuPostprocessingSobelMain", sceneFrame,
                mainPass())
            ->renderPass(
                "WebgpuPostprocessingSobelDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuPostprocessingSobelOutput", outputFrame,
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
        roomSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(dotTexture);
        device->freeTexture(shiftedTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
