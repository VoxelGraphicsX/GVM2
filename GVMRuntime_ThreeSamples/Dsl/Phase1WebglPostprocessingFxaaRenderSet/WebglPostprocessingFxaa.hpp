#ifndef GVM_THREE_WEBGL_POSTPROCESSING_FXAA_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_FXAA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the tetrahedron attributes used by the FXAA comparison Scene. */
struct WebglPostprocessingFxaaVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
    float4 uvAndCorner [[Attribute3]];
    float4 morphPosition [[Attribute4]];
};

/** Stores one entity transform and feature parameters. */
struct WebglPostprocessingFxaaObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 parameters;
};

/** Stores one instance transform and color multiplier. */
struct WebglPostprocessingFxaaInstanceData
{
    float4 transformColumn0;
    float4 transformColumn1;
    float4 transformColumn2;
    float4 transformColumn3;
    float4 color;
};

/** Stores the material parameters used by the FXAA tetrahedra. */
struct WebglPostprocessingFxaaMaterialData
{
    float4 baseColor;
    float4 emissiveAndOpacity;
    float4 modeAndParameters;
};

/** Defines the unique Scene RenderSet used by the FXAA example. */
struct WebglPostprocessingFxaaSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry plus mandatory object, instance, and material components. */
    constructor(BufferComponent<WebglPostprocessingFxaaVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebglPostprocessingFxaaObjectData> objects,
                BufferComponent<WebglPostprocessingFxaaInstanceData> instances,
                BufferComponent<WebglPostprocessingFxaaMaterialData> materials)
    {
    }
};

/** Stores deterministic frame, lighting, background, and postprocess controls. */
struct WebglPostprocessingFxaaFrameData
{
    float4 lightPositionAndIntensity;
    float4 ambientAndTime;
    float4 backgroundAndCaseMode;
    float4 viewportAndInput;
};

/** Binds the frame controls used by the Scene and fullscreen passes. */
struct WebglPostprocessingFxaaFrameResources final : public IBindGroup
{
    /** Declares the immutable frame resource layout for the FXAA example. */
    constructor(UniformBuffer<WebglPostprocessingFxaaFrameData> frame [[Binding0]],
                StructuredBuffer<float4> computeState [[Binding1]])
    {
    }
};

/** Binds writable particle, bird, and instance state for the shared compute stage. */
struct WebglPostprocessingFxaaComputeResources final : public IBindGroup
{
    /** Declares the current public RWStructuredBuffer and frame controls. */
    constructor(RWStructuredBuffer<float4> computeState [[Binding0]],
                UniformBuffer<WebglPostprocessingFxaaFrameData> frame [[Binding1]])
    {
    }
};

/** Carries entity-aware surface data from the RenderSet vertex stage. */
struct WebglPostprocessingFxaaVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float4 color [[Attribute2]];
    float2 uv [[Attribute3]];
    uint entityID [[Attribute4]];
    float3 worldNormal [[Attribute5]];
};

/** Defines the FXAA Scene color and depth attachments. */
struct WebglPostprocessingFxaaSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the Scene output for deterministic postprocessing and output conversion. */
struct WebglPostprocessingFxaaScreenResources final : public IBindGroup
{
    /** Declares one sampled Scene texture and the shared frame controls. */
    constructor(Texture2D<float4> sceneColor [[Binding0]],
                Sampler sceneSampler [[Binding1]],
                UniformBuffer<WebglPostprocessingFxaaFrameData> frame [[Binding2]],
                Texture2D<float4> encodedSceneColor [[Binding3]])
    {
    }
};

/** Binds only the linear Scene source required by the output-transfer pass. */
struct WebglPostprocessingFxaaEncodeResources final : public IBindGroup
{
    /** Declares the source texture and sampler without aliasing the encoded attachment. */
    constructor(Texture2D<float4> sceneColor [[Binding0]],
                Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Defines the output-transfer intermediate sampled by the r185 FXAA pass. */
struct WebglPostprocessingFxaaEncodedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Carries fullscreen coordinates into the private postprocess family. */
struct WebglPostprocessingFxaaScreenVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the final RGBA8 readback attachment. */
struct WebglPostprocessingFxaaOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear working-space channel into the standard r185 output transfer. */
float webglPostprocessingFxaaLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Samples a linear Scene texture and applies the r185 output transfer before FXAA. */
float3 webglPostprocessingFxaaSampleFxaaColor(Texture2D<float4> textureValue,
                                 Sampler samplerValue,
                                 float2 uv)
{
    return textureValue->sample(samplerValue, uv).xyz;
}

/** Samples the exact FXAA luminance coefficients used by Three r185. */
float webglPostprocessingFxaaSampleFxaaLuminance(Texture2D<float4> textureValue,
                                    Sampler samplerValue,
                                    float2 uv)
{
    return dot(webglPostprocessingFxaaSampleFxaaColor(textureValue, samplerValue, uv),
               float3(0.3f, 0.59f, 0.11f));
}

/** Evaluates a compact deterministic sky and cloud field without external textures. */
float3 webglPostprocessingFxaaSky(float3 direction, float timeValue)
{
    const float horizon = pow(clamp(1.0f - max(direction.y, 0.0f), 0.0f, 1.0f), 2.0f);
    const float sun = pow(max(dot(normalize(direction), normalize(float3(0.2f, 0.7f, 0.4f))), 0.0f), 256.0f);
    const float cloud = smoothstep(0.55f, 0.72f,
        sin(direction.x * 19.0f + timeValue * 0.1f) *
        sin(direction.z * 23.0f - timeValue * 0.07f) * 0.5f + 0.5f);
    return lerp(float3(0.12f, 0.32f, 0.72f), float3(0.72f, 0.82f, 0.92f), horizon) +
           float3(1.0f, 0.78f, 0.42f) * sun + float3(cloud * 0.12f);
}

/** Updates deterministic structured state for compute-driven batch examples. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebglPostprocessingFxaaComputePass final : public IComputeClass
{
public:
    /** Binds one existing writable structured buffer. */
    constructor(BindGroup<WebglPostprocessingFxaaComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Writes one bounded particle/bird/instance displacement record. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= 65536u) return;
        const float timeValue = resources->frame->ambientAndTime.w;
        const float phase = float(index) * 0.0174532925f;
        resources->computeState[index] = float4(
            sin(phase + timeValue),
            cos(phase * 1.7f - timeValue * 0.6f),
            sin(phase * 0.7f + timeValue * 0.3f),
            1.0f);
    }
};

/** Draws all complex Scene entities through the RenderSet-only indexed-indirect path. */
class WebglPostprocessingFxaaMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and enables alpha-capable material phases. */
    constructor(RenderSet<WebglPostprocessingFxaaSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<WebglPostprocessingFxaaFrameResources> frameResources [[Slot1]])
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
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity and instance components, morphs vertices, and applies optional particle animation. */
    WebglPostprocessingFxaaVertexOutput vertex(WebglPostprocessingFxaaVertex inputValue [[VertexInput0]],
                                   uint renderEntityID [[RenderEntityID]],
                                   uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingFxaaObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingFxaaInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float morphWeight = objectData.parameters.x;
        float4 localPosition = lerp(inputValue.position, inputValue.morphPosition, morphWeight);
        const float particleMode = objectData.parameters.y;
        if (particleMode > 0.5f)
        {
            const float timeValue = frameResources->frame->ambientAndTime.w;
            const uint stateIndex = (renderEntityInstanceID + renderEntityID * 4096u) & 65535u;
            const float3 computeDisplacement = frameResources->computeState[stateIndex].xyz;
            localPosition.xyz += (computeDisplacement + float3(
                sin(localPosition.y * 1.7f + timeValue),
                cos(localPosition.z * 1.3f + timeValue * 0.7f),
                sin(localPosition.x * 1.1f - timeValue * 0.4f))) * objectData.parameters.z;
        }
        const float4 instancedPosition = instanceData.transformColumn0 * localPosition.x +
                                         instanceData.transformColumn1 * localPosition.y +
                                         instanceData.transformColumn2 * localPosition.z +
                                         instanceData.transformColumn3 * localPosition.w;
        const float4 viewPosition = mul(objectData.modelView, instancedPosition);
        WebglPostprocessingFxaaVertexOutput outputValue;
        const float cameraSine = sin(objectData.parameters.x);
        const float cameraCosine = cos(objectData.parameters.x);
        const float fxaaViewX =
            cameraCosine * instancedPosition.x -
            cameraSine * instancedPosition.z;
        const float fxaaViewY = instancedPosition.y;
        const float viewDistance =
            500.0f -
            (cameraSine * instancedPosition.x +
             cameraCosine * instancedPosition.z);
        outputValue.position =
            objectData.parameters.w > 9.5f &&
                    objectData.parameters.w < 10.5f
                ? float4(
                      fxaaViewX * 1.5088834765f,
                      fxaaViewY * -2.4142135624f,
                      viewDistance - 1.0f,
                      viewDistance)
                : mul(
                      objectData.modelViewProjection,
                      instancedPosition);
        outputValue.viewPosition =
            objectData.parameters.w > 9.5f &&
                    objectData.parameters.w < 10.5f
                ? instancedPosition.xyz
                : viewPosition.xyz;
        float3 instanceNormal;
        if (objectData.parameters.w > 9.5f && objectData.parameters.w < 10.5f)
        {
            const float3 quaternionVector = instanceData.color.xyz;
            const float3 quaternionCross = cross(quaternionVector, inputValue.normal.xyz) * 2.0f;
            instanceNormal = normalize(inputValue.normal.xyz +
                quaternionCross * instanceData.color.w +
                cross(quaternionVector, quaternionCross));
        }
        else
        {
            instanceNormal = normalize(
                instanceData.transformColumn0.xyz * inputValue.normal.x +
                instanceData.transformColumn1.xyz * inputValue.normal.y +
                instanceData.transformColumn2.xyz * inputValue.normal.z);
        }
        const float4 transformedNormal = mul(objectData.modelView,
            float4(instanceNormal, 0.0f));
        outputValue.viewNormal = normalize(float3(transformedNormal.xyz));
        outputValue.worldNormal = instanceNormal;
        outputValue.color = objectData.parameters.w > 9.5f && objectData.parameters.w < 10.5f
            ? inputValue.color
            : inputValue.color * instanceData.color;
        outputValue.uv = inputValue.uvAndCorner.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates Lambert, particle, morph, and procedural sky material modes. */
    WebglPostprocessingFxaaSceneFrameBuffer fragment(WebglPostprocessingFxaaVertexOutput inputValue)
    {
        const WebglPostprocessingFxaaMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        const float materialMode = materialData.modeAndParameters.x;
        float3 linearColor;
        float opacity = materialData.emissiveAndOpacity.w;
        if (materialMode > 1.5f && materialMode < 2.5f)
        {
            linearColor = webglPostprocessingFxaaSky(normalize(-inputValue.viewPosition),
                frameResources->frame->ambientAndTime.w);
            opacity = 1.0f;
        }
        else if (materialMode > 9.5f && materialMode < 10.5f)
        {
            const WebglPostprocessingFxaaObjectData objectData =
                sceneSet->objects->get(inputValue.entityID, 0u);
            const float3 normal = normalize(inputValue.worldNormal);
            const float3 skyColor = float3(1.0f);
            const float3 groundColor = float3(0.26635566f);
            const float hemisphereWeight = normal.y * 0.5f + 0.5f;
            const float3 hemisphere = lerp(groundColor, skyColor, hemisphereWeight);
            const float3 directional = normalize(float3(-3000.0f, 1000.0f, -1000.0f));
            const float dotNormalLight = max(dot(normal, directional), 0.0f);
            const float directDiffuse = dotNormalLight * 0.9549296586f;
            const float cameraSine = sin(objectData.parameters.x);
            const float cameraCosine = cos(objectData.parameters.x);
            const float3 cameraPosition =
                float3(cameraSine * 500.0f, 0.0f, cameraCosine * 500.0f);
            const float3 viewDirection =
                normalize(cameraPosition - inputValue.viewPosition);
            const float3 halfDirection = normalize(directional + viewDirection);
            const float dotNormalView = max(dot(normal, viewDirection), 0.0f);
            const float dotViewHalf = max(dot(viewDirection, halfDirection), 0.0f);
            const float fresnelWeight = exp2(
                (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
            const float3 fresnel =
                float3(0.04f) + float3(0.96f * fresnelWeight);
            const float visibility =
                0.5f / max(dotNormalLight + dotNormalView, 0.000001f);
            const float3 directSpecular =
                fresnel * (dotNormalLight * 3.0f * visibility * 0.3183098862f);
            linearColor = materialData.baseColor.xyz *
                              (hemisphere * 0.3183098862f + float3(directDiffuse)) +
                          directSpecular;
            opacity = 1.0f;
        }
        else
        {
            const float3 lightVector = normalize(frameResources->frame->lightPositionAndIntensity.xyz - inputValue.viewPosition);
            const float diffuse = max(dot(normalize(inputValue.viewNormal), lightVector), 0.0f);
            const float ambient = frameResources->frame->ambientAndTime.x;
            linearColor = materialData.baseColor.xyz * inputValue.color.xyz *
                          (ambient + diffuse * frameResources->frame->lightPositionAndIntensity.w) +
                          materialData.emissiveAndOpacity.xyz;
            if (materialMode > 0.5f && materialMode < 1.5f)
            {
                const float2 centered = inputValue.uv * 2.0f - float2(1.0f);
                opacity *= smoothstep(1.0f, 0.65f, dot(centered, centered));
                if (opacity < 0.01f) discard_fragment();
            }
        }
        WebglPostprocessingFxaaSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(opacity));
        return frameBuffer;
    }
};

/** Applies the r185 output transfer into the encoded intermediate consumed by FXAA. */
class WebglPostprocessingFxaaOutputColorPass final : public IRenderClass
{
public:
    /** Binds the linear Scene texture and disables culling for a fullscreen triangle. */
    constructor(BindGroup<WebglPostprocessingFxaaEncodeResources> encodeResources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle for output conversion. */
    WebglPostprocessingFxaaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglPostprocessingFxaaScreenVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Encodes linear Scene RGB while preserving alpha for the following FXAA pass. */
    WebglPostprocessingFxaaEncodedFrameBuffer fragment(WebglPostprocessingFxaaScreenVertexOutput inputValue)
    {
        const float4 linearColor =
            encodeResources->sceneColor->sample(encodeResources->sceneSampler, inputValue.uv);
        WebglPostprocessingFxaaEncodedFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingFxaaLinearToSrgb(linearColor.x)),
            half(webglPostprocessingFxaaLinearToSrgb(linearColor.y)),
            half(webglPostprocessingFxaaLinearToSrgb(linearColor.z)),
            half(linearColor.w));
        return frameBuffer;
    }
};

/** Applies the batch's Sobel, afterimage, FXAA-like, radial, or plain output mode. */
class WebglPostprocessingFxaaCompositePass final : public IRenderClass
{
public:
    /** Binds only fullscreen resources and disables culling. */
    constructor(BindGroup<WebglPostprocessingFxaaScreenResources> screenResources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingFxaaScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglPostprocessingFxaaScreenVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Selects one deterministic private postprocess and performs output conversion. */
    WebglPostprocessingFxaaOutputFrameBuffer fragment(WebglPostprocessingFxaaScreenVertexOutput inputValue)
    {
        const float2 texel =
            float2(1.0f) / screenResources->frame->viewportAndInput.xy;
        const float mode = screenResources->frame->backgroundAndCaseMode.w;
        float3 color = screenResources->sceneColor->sample(screenResources->sceneSampler, inputValue.uv).xyz;
        bool colorIsEncoded = false;
        if (mode > 9.5f && mode < 10.5f)
        {
            const float3 encodedCenter = webglPostprocessingFxaaSampleFxaaColor(
                screenResources->encodedSceneColor,
                screenResources->sceneSampler,
                inputValue.uv);
            color = encodedCenter;
            colorIsEncoded = true;
            const float leftScissorLimit =
                (screenResources->frame->viewportAndInput.x * 0.5f - 1.0f) /
                screenResources->frame->viewportAndInput.x;
            if (inputValue.uv.x >= leftScissorLimit &&
                inputValue.uv.x < 0.5f)
            {
                color = float3(1.0f);
            }
            else if (inputValue.uv.x >= 0.5f)
            {
                const float luminanceM = dot(encodedCenter, float3(0.3f, 0.59f, 0.11f));
                const float luminanceN = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv + float2(0.0f, texel.y));
                const float luminanceE = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv + float2(texel.x, 0.0f));
                const float luminanceS = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv - float2(0.0f, texel.y));
                const float luminanceW = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv - float2(texel.x, 0.0f));
                const float luminanceNE = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv + texel);
                const float luminanceNW = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv + float2(-texel.x, texel.y));
                const float luminanceSE = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv + float2(texel.x, -texel.y));
                const float luminanceSW = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, inputValue.uv - texel);
                const float highest = max(max(max(max(luminanceN, luminanceE), luminanceS), luminanceW), luminanceM);
                const float lowest = min(min(min(min(luminanceN, luminanceE), luminanceS), luminanceW), luminanceM);
                const float contrast = highest - lowest;
                const float contrastThreshold = max(0.0312f, 0.063f * highest);
                if (contrast >= contrastThreshold)
                {
                    float neighborhood = 2.0f * (luminanceN + luminanceE + luminanceS + luminanceW);
                    neighborhood += luminanceNE + luminanceNW + luminanceSE + luminanceSW;
                    neighborhood *= 0.0833333333f;
                    const float subpixelDelta = clamp(abs(neighborhood - luminanceM) / contrast, 0.0f, 1.0f);
                    const float subpixelSmooth = smoothstep(0.0f, 1.0f, subpixelDelta);
                    const float pixelBlend = subpixelSmooth * subpixelSmooth;

                    const float horizontal =
                        abs(luminanceN + luminanceS - 2.0f * luminanceM) * 2.0f +
                        abs(luminanceNE + luminanceSE - 2.0f * luminanceE) +
                        abs(luminanceNW + luminanceSW - 2.0f * luminanceW);
                    const float vertical =
                        abs(luminanceE + luminanceW - 2.0f * luminanceM) * 2.0f +
                        abs(luminanceNE + luminanceNW - 2.0f * luminanceN) +
                        abs(luminanceSE + luminanceSW - 2.0f * luminanceS);
                    const bool isHorizontal = horizontal >= vertical;
                    const float positiveLuminance = isHorizontal ? luminanceN : luminanceE;
                    const float negativeLuminance = isHorizontal ? luminanceS : luminanceW;
                    const float positiveGradient = abs(positiveLuminance - luminanceM);
                    const float negativeGradient = abs(negativeLuminance - luminanceM);
                    float pixelStep = isHorizontal ? texel.y : texel.x;
                    float oppositeLuminance = positiveLuminance;
                    float gradient = positiveGradient;
                    if (positiveGradient < negativeGradient)
                    {
                        pixelStep = -pixelStep;
                        oppositeLuminance = negativeLuminance;
                        gradient = negativeGradient;
                    }

                    float2 edgeUv = inputValue.uv;
                    float2 edgeStep;
                    if (isHorizontal)
                    {
                        edgeUv.y += pixelStep * 0.5f;
                        edgeStep = float2(texel.x, 0.0f);
                    }
                    else
                    {
                        edgeUv.x += pixelStep * 0.5f;
                        edgeStep = float2(0.0f, texel.y);
                    }
                    const float edgeLuminance = (luminanceM + oppositeLuminance) * 0.5f;
                    const float gradientThreshold = gradient * 0.25f;

                    float2 positiveUv = edgeUv + edgeStep;
                    float positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance;
                    bool positiveAtEnd = abs(positiveDelta) >= gradientThreshold;
                    if (!positiveAtEnd) { positiveUv += edgeStep * 1.5f; positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance; positiveAtEnd = abs(positiveDelta) >= gradientThreshold; }
                    if (!positiveAtEnd) { positiveUv += edgeStep * 2.0f; positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance; positiveAtEnd = abs(positiveDelta) >= gradientThreshold; }
                    if (!positiveAtEnd) { positiveUv += edgeStep * 2.0f; positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance; positiveAtEnd = abs(positiveDelta) >= gradientThreshold; }
                    if (!positiveAtEnd) { positiveUv += edgeStep * 2.0f; positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance; positiveAtEnd = abs(positiveDelta) >= gradientThreshold; }
                    if (!positiveAtEnd) { positiveUv += edgeStep * 4.0f; positiveDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, positiveUv) - edgeLuminance; positiveAtEnd = abs(positiveDelta) >= gradientThreshold; }
                    if (!positiveAtEnd) positiveUv += edgeStep * 8.0f;

                    float2 negativeUv = edgeUv - edgeStep;
                    float negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance;
                    bool negativeAtEnd = abs(negativeDelta) >= gradientThreshold;
                    if (!negativeAtEnd) { negativeUv -= edgeStep * 1.5f; negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance; negativeAtEnd = abs(negativeDelta) >= gradientThreshold; }
                    if (!negativeAtEnd) { negativeUv -= edgeStep * 2.0f; negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance; negativeAtEnd = abs(negativeDelta) >= gradientThreshold; }
                    if (!negativeAtEnd) { negativeUv -= edgeStep * 2.0f; negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance; negativeAtEnd = abs(negativeDelta) >= gradientThreshold; }
                    if (!negativeAtEnd) { negativeUv -= edgeStep * 2.0f; negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance; negativeAtEnd = abs(negativeDelta) >= gradientThreshold; }
                    if (!negativeAtEnd) { negativeUv -= edgeStep * 4.0f; negativeDelta = webglPostprocessingFxaaSampleFxaaLuminance(screenResources->encodedSceneColor, screenResources->sceneSampler, negativeUv) - edgeLuminance; negativeAtEnd = abs(negativeDelta) >= gradientThreshold; }
                    if (!negativeAtEnd) negativeUv -= edgeStep * 8.0f;

                    const float positiveDistance = isHorizontal
                        ? positiveUv.x - inputValue.uv.x
                        : positiveUv.y - inputValue.uv.y;
                    const float negativeDistance = isHorizontal
                        ? inputValue.uv.x - negativeUv.x
                        : inputValue.uv.y - negativeUv.y;
                    const bool usePositive = positiveDistance <= negativeDistance;
                    const float shortestDistance = usePositive ? positiveDistance : negativeDistance;
                    const bool shortestDeltaSign = (usePositive ? positiveDelta : negativeDelta) >= 0.0f;
                    float edgeBlend = 0.0f;
                    if (shortestDeltaSign != (luminanceM - edgeLuminance >= 0.0f))
                    {
                        edgeBlend = 0.5f - shortestDistance / (positiveDistance + negativeDistance);
                    }
                    const float finalBlend = max(pixelBlend, edgeBlend);
                    float2 filteredUv = inputValue.uv;
                    if (isHorizontal)
                    {
                        filteredUv.y += pixelStep * finalBlend;
                    }
                    else
                    {
                        filteredUv.x += pixelStep * finalBlend;
                    }
                    color = webglPostprocessingFxaaSampleFxaaColor(
                        screenResources->encodedSceneColor,
                        screenResources->sceneSampler,
                        filteredUv);
                }
            }
        }
        else if (mode > 10.5f && mode < 11.5f)
        {
            const float3 sampleX = screenResources->sceneColor->sample(screenResources->sceneSampler, inputValue.uv + float2(texel.x, 0.0f)).xyz;
            const float3 sampleY = screenResources->sceneColor->sample(screenResources->sceneSampler, inputValue.uv + float2(0.0f, texel.y)).xyz;
            color = abs(sampleX - color) + abs(sampleY - color);
        }
        else if (mode > 11.5f)
        {
            const float2 direction = float2(0.5f) - inputValue.uv;
            color = color * 0.4f;
            for (uint sampleIndex = 1u; sampleIndex < 7u; ++sampleIndex)
            {
                color += screenResources->sceneColor->sample(screenResources->sceneSampler,
                    inputValue.uv + direction * (float(sampleIndex) / 42.0f)).xyz * 0.1f;
            }
        }
        const float3 encoded = colorIsEncoded
            ? color
            : float3(webglPostprocessingFxaaLinearToSrgb(color.x),
                     webglPostprocessingFxaaLinearToSrgb(color.y),
                     webglPostprocessingFxaaLinearToSrgb(color.z));
        WebglPostprocessingFxaaOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique FXAA Scene RenderSet and its side-by-side postprocess output chain. */
class WebglPostprocessingFxaaRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingFxaaSceneRenderSet> sceneSet;
    Buffer<WebglPostprocessingFxaaFrameData, BufferUsage<Uniform, CopyDst>> frameBuffer;
    Buffer<float4, BufferUsage<Storage>> computeStateBuffer;
    BindGroup<WebglPostprocessingFxaaFrameResources> frameResources;
    BindGroup<WebglPostprocessingFxaaComputeResources> computeResources;
    BindGroup<WebglPostprocessingFxaaEncodeResources> encodeResources;
    BindGroup<WebglPostprocessingFxaaScreenResources> screenResources;
    RenderClass<WebglPostprocessingFxaaMainPass> scenePass;
    ComputeClass<WebglPostprocessingFxaaComputePass> computePass;
    RenderClass<WebglPostprocessingFxaaOutputColorPass> encodePass;
    RenderClass<WebglPostprocessingFxaaCompositePass> screenPass;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> rightSceneColor;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> encodedSceneColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> rightSceneDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Sampler sceneSampler;
    WebglPostprocessingFxaaFrameData frameData;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and shared frame resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglPostprocessingFxaaSceneRenderSet>();
        frameBuffer = device->createBuffer("WebglPostprocessingFxaaFrameData", 1u);
        computeStateBuffer = device->createBuffer("WebglPostprocessingFxaaComputeState", 65536u);
        frameResources = device->createBindGroup<WebglPostprocessingFxaaFrameResources>(frameBuffer, computeStateBuffer);
        computeResources = device->createBindGroup<WebglPostprocessingFxaaComputeResources>(computeStateBuffer, frameBuffer);
        computePass = device->createComputeClass<WebglPostprocessingFxaaComputePass>(computeResources);
        scenePass = device->createRenderClass<WebglPostprocessingFxaaMainPass>(sceneSet, frameResources);
        sceneSampler = device->createSampler({
            .label = "WebglPostprocessingFxaaSceneSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the fixed host-sized Scene, depth, and final readback targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneColor = device->createTexture("WebglPostprocessingFxaaSceneColor", width, height, 1u);
        rightSceneColor = device->createTexture("WebglPostprocessingFxaaRightSceneColor", width, height, 1u);
        encodedSceneColor = device->createTexture("WebglPostprocessingFxaaEncodedSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("WebglPostprocessingFxaaSceneDepth", width, height, 1u);
        rightSceneDepth = device->createTexture("WebglPostprocessingFxaaRightSceneDepth", width, height, 1u);
        outputColor = device->createTexture("WebglPostprocessingFxaaOutput", width, height, 1u);
        encodeResources = device->createBindGroup<WebglPostprocessingFxaaEncodeResources>(
            rightSceneColor->createView(), sceneSampler);
        screenResources = device->createBindGroup<WebglPostprocessingFxaaScreenResources>(
            sceneColor->createView(), sceneSampler, frameBuffer,
            encodedSceneColor->createView());
        encodePass = device->createRenderClass<WebglPostprocessingFxaaOutputColorPass>(encodeResources);
        screenPass = device->createRenderClass<WebglPostprocessingFxaaCompositePass>(screenResources);
    }

    /** Uploads one case's deterministic lighting, background, time, and postprocess controls. */
    void configureCase(float caseMode, float timeValue, float inputX, float inputY)
    {
        frameData.lightPositionAndIntensity = float4(3.0f, 4.0f, 6.0f, 0.85f);
        frameData.ambientAndTime = float4(0.22f, 0.0f, 0.0f, timeValue);
        frameData.backgroundAndCaseMode = float4(0.02f, 0.025f, 0.04f, caseMode);
        if (caseMode > 9.5f && caseMode < 10.5f)
        {
            frameData.backgroundAndCaseMode = float4(1.0f, 1.0f, 1.0f, caseMode);
        }
        frameData.viewportAndInput = float4(float(readbackWidth), float(readbackHeight), inputX, inputY);
        graphicsQueue->writeBuffer(BufferRange(frameBuffer), &frameData, sizeof(frameData))->submit();
    }

    /** Updates the unique Set, draws it indirectly once, then executes the fullscreen output pass. */
    void render() override
    {
        sceneSet->update();
        WebglPostprocessingFxaaSceneFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = sceneColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {
            frameData.backgroundAndCaseMode.x,
            frameData.backgroundAndCaseMode.y,
            frameData.backgroundAndCaseMode.z,
            1.0f};
        sceneFrameBuffer.depth = sceneDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebglPostprocessingFxaaSceneFrameBuffer rightSceneFrameBuffer;
        rightSceneFrameBuffer.color = rightSceneColor->createView();
        rightSceneFrameBuffer.color.loadOp = LoadOp::Clear;
        rightSceneFrameBuffer.color.storeOp = StoreOp::Store;
        rightSceneFrameBuffer.color.clearValue = {
            frameData.backgroundAndCaseMode.x,
            frameData.backgroundAndCaseMode.y,
            frameData.backgroundAndCaseMode.z,
            1.0f};
        rightSceneFrameBuffer.depth = rightSceneDepth->createView();
        rightSceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        rightSceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        rightSceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebglPostprocessingFxaaOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        WebglPostprocessingFxaaEncodedFrameBuffer encodedFrameBuffer;
        encodedFrameBuffer.color = encodedSceneColor->createView();
        encodedFrameBuffer.color.loadOp = LoadOp::Clear;
        encodedFrameBuffer.color.storeOp = StoreOp::Store;
        encodedFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto nextTexture = swapchain->queryNextTexture();
        if (frameData.backgroundAndCaseMode.w > 9.5f &&
            frameData.backgroundAndCaseMode.w < 10.5f)
        {
            graphicsQueue
                ->renderPass("WebglPostprocessingFxaaSceneLeft", sceneFrameBuffer, scenePass())
                ->renderPass("WebglPostprocessingFxaaSceneRight", rightSceneFrameBuffer, scenePass())
                ->renderPass("WebglPostprocessingFxaaEncode", encodedFrameBuffer, encodePass(3u, 1u, 0u, 0u))
                ->renderPass("WebglPostprocessingFxaaOutput", outputFrameBuffer, screenPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        }
        else
        {
            graphicsQueue->computePass("WebglPostprocessingFxaaCompute", computePass(65536u, 1u, 1u))
                ->renderPass("WebglPostprocessingFxaaScene", sceneFrameBuffer, scenePass())
                ->renderPass("WebglPostprocessingFxaaEncode", encodedFrameBuffer, encodePass(3u, 1u, 0u, 0u))
                ->renderPass("WebglPostprocessingFxaaOutput", outputFrameBuffer, screenPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        }
        swapchain->present();
    }

    /** Returns the DSL-owned final color target. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the unique Scene RenderSet and every private FXAA resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeBuffer(computeStateBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(rightSceneColor);
        device->freeTexture(encodedSceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(rightSceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
