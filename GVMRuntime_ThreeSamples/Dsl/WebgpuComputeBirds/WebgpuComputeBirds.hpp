#ifndef GVM_THREE_WEBGPU_COMPUTE_BIRDS_HPP
#define GVM_THREE_WEBGPU_COMPUTE_BIRDS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputeBirdsCount = 8192u;

/** Stores the attribute union for sky and bird triangles. */
struct WebgpuComputeBirdsVertex
{
    float4 positionAndVertex [[Attribute0]];
};

/** Stores one Scene entity model, view, projection, and material phase. */
struct WebgpuComputeBirdsObjectData
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 phaseAndFog;
};

/** Stores deterministic initial flock state and one stable instance ordinal. */
struct WebgpuComputeBirdsInstanceData
{
    float4 initialPosition;
    float4 initialVelocity;
    float4 initialPhase;
    float4 ordinal;
};

/** Stores one Scene material color and phase. */
struct WebgpuComputeBirdsMaterialData
{
    float4 colorAndPhase;
};

/** Defines the unique Scene RenderSet for sky and the instanced flock. */
struct WebgpuComputeBirdsSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and per-entity or per-instance components. */
    constructor(
        BufferComponent<WebgpuComputeBirdsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuComputeBirdsObjectData> objects,
        BufferComponent<WebgpuComputeBirdsInstanceData> instances,
        BufferComponent<WebgpuComputeBirdsMaterialData> materials)
    {
    }
};

/** Stores the exact r185 flock controls and deterministic ray. */
struct WebgpuComputeBirdsControls
{
    float4 separationAlignmentCohesionDelta;
    float4 rayOrigin;
    float4 rayDirection;
    float4 inspectorEnabled;
};

/** Binds all writable flock state to the two ordered Compute passes. */
struct WebgpuComputeBirdsComputeResources final : public IBindGroup
{
    /** Declares current position, velocity, next velocity, phase, and controls. */
    constructor(
        RWStructuredBuffer<float4> positions [[Binding0]],
        RWStructuredBuffer<float4> velocities [[Binding1]],
        RWStructuredBuffer<float4> nextVelocities [[Binding2]],
        RWStructuredBuffer<float4> phases [[Binding3]],
        UniformBuffer<WebgpuComputeBirdsControls> controls [[Binding4]])
    {
    }
};

/** Exposes computed flock state to the Scene vertex stage. */
struct WebgpuComputeBirdsDrawResources final : public IBindGroup
{
    /** Declares read-only position, velocity, and phase buffers. */
    constructor(
        StructuredBuffer<float4> positions [[Binding0]],
        StructuredBuffer<float4> velocities [[Binding1]],
        StructuredBuffer<float4> phases [[Binding2]])
    {
    }
};

/** Binds the linear Scene image to the neutral output pass. */
struct WebgpuComputeBirdsOutputResources final : public IBindGroup
{
    /** Declares the Scene texture and centered single-sample sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]],
        UniformBuffer<WebgpuComputeBirdsControls> controls [[Binding2]])
    {
    }
};

/** Safely normalizes one vector while preserving deterministic zero behavior. */
float3 webgpuComputeBirdsNormalize(float3 value)
{
    return value / length(value);
}

/** Initializes writable flock state from the unique Scene RenderSet. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeBirdsInitPass final
    : public IComputeClass
{
public:
    /** Binds the Scene instance component and writable flock-state resources. */
    constructor(
        RenderSet<WebgpuComputeBirdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuComputeBirdsComputeResources> resources [[Slot1]])
    {
    }

private:
    /** Copies one bird's deterministic CPU-prepared state into GPU buffers. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint birdIndex = dispatchThreadID.x;
        if (birdIndex >= WebgpuComputeBirdsCount) return;
        const WebgpuComputeBirdsInstanceData instanceData =
            sceneSet->instances->get(1u, birdIndex);
        resources->positions[birdIndex] =
            instanceData.initialPosition;
        resources->velocities[birdIndex] =
            instanceData.initialVelocity;
        resources->nextVelocities[birdIndex] =
            instanceData.initialVelocity;
        resources->phases[birdIndex] =
            instanceData.initialPhase;
    }
};

/** Advances one bird's velocity with the exact r185 flock equations. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeBirdsVelocityPass final
    : public IComputeClass
{
public:
    /** Binds the dedicated flock-state resource group. */
    constructor(BindGroup<WebgpuComputeBirdsComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Scans all birds and writes one deterministic next-velocity record. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint birdIndex = dispatchThreadID.x;
        if (birdIndex >= WebgpuComputeBirdsCount) return;
        const float4 controls =
            resources->controls->separationAlignmentCohesionDelta;
        const float separation = controls.x;
        const float alignment = controls.y;
        const float cohesion = controls.z;
        const float deltaTime = controls.w;
        const float zoneRadius = separation + alignment + cohesion;
        const float zoneRadiusSq = zoneRadius * zoneRadius;
        const float separationThreshold = separation / zoneRadius;
        const float alignmentThreshold =
            (separation + alignment) / zoneRadius;
        const float3 position = resources->positions[birdIndex].xyz;
        float3 velocity = resources->velocities[birdIndex].xyz;
        float speedLimit = 9.0f;

        const float3 directionToRay =
            resources->controls->rayOrigin.xyz - position;
        const float3 rayDirection =
            resources->controls->rayDirection.xyz;
        const float projectionLength =
            dot(directionToRay, rayDirection);
        const float3 closestPoint =
            resources->controls->rayOrigin.xyz -
            rayDirection * projectionLength;
        const float3 directionToClosestPoint = closestPoint - position;
        const float distanceToClosestPoint =
            length(directionToClosestPoint);
        const float distanceToClosestPointSq =
            distanceToClosestPoint * distanceToClosestPoint;
        if (distanceToClosestPointSq < 22500.0f)
        {
            const float velocityAdjust =
                (distanceToClosestPointSq / 22500.0f - 1.0f) *
                deltaTime * 100.0f;
            velocity +=
                webgpuComputeBirdsNormalize(directionToClosestPoint) *
                velocityAdjust;
            speedLimit += 5.0f;
        }

        float3 directionToCenter = position;
        directionToCenter.y *= 2.5f;
        velocity -=
            webgpuComputeBirdsNormalize(directionToCenter) *
            deltaTime * 5.0f;

        for (uint otherIndex = 0u;
             otherIndex < WebgpuComputeBirdsCount;
             ++otherIndex)
        {
            if (otherIndex == birdIndex) continue;
            const float3 directionToBird =
                resources->positions[otherIndex].xyz - position;
            const float distanceToBird = length(directionToBird);
            if (distanceToBird < 0.0001f) continue;
            const float distanceToBirdSq =
                distanceToBird * distanceToBird;
            if (distanceToBirdSq > zoneRadiusSq) continue;
            const float percent = distanceToBirdSq / zoneRadiusSq;
            if (percent < separationThreshold)
            {
                const float velocityAdjust =
                    (separationThreshold / percent - 1.0f) * deltaTime;
                velocity -=
                    webgpuComputeBirdsNormalize(directionToBird) *
                    velocityAdjust;
            }
            else if (percent < alignmentThreshold)
            {
                const float adjustedPercent =
                    (percent - separationThreshold) /
                    (alignmentThreshold - separationThreshold);
                const float cosineRange =
                    cos(adjustedPercent * 6.283185307179586f);
                const float cosineRangeAdjust =
                    0.5f - cosineRange * 0.5f + 0.5f;
                velocity +=
                    webgpuComputeBirdsNormalize(
                        resources->velocities[otherIndex].xyz) *
                    (cosineRangeAdjust * deltaTime);
            }
            else
            {
                const float thresholdDelta =
                    1.0f - alignmentThreshold;
                const float adjustedPercent =
                    thresholdDelta == 0.0f
                    ? 1.0f
                    : (percent - alignmentThreshold) / thresholdDelta;
                const float cosineRange =
                    cos(adjustedPercent * 6.283185307179586f);
                const float velocityAdjust =
                    (0.5f - (-0.5f * cosineRange + 0.5f)) *
                    deltaTime;
                velocity +=
                    webgpuComputeBirdsNormalize(directionToBird) *
                    velocityAdjust;
            }
        }
        if (length(velocity) > speedLimit)
        {
            velocity =
                webgpuComputeBirdsNormalize(velocity) * speedLimit;
        }
        resources->nextVelocities[birdIndex] =
            float4(velocity, 0.0f);
    }
};

/** Integrates positions and phases after the velocity pass. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeBirdsPositionPass final
    : public IComputeClass
{
public:
    /** Binds the same ordered flock-state resource group. */
    constructor(BindGroup<WebgpuComputeBirdsComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Writes position, phase, and the current velocity for one bird. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint birdIndex = dispatchThreadID.x;
        if (birdIndex >= WebgpuComputeBirdsCount) return;
        const float deltaTime =
            resources->controls->separationAlignmentCohesionDelta.w;
        const float3 velocity =
            resources->nextVelocities[birdIndex].xyz;
        resources->velocities[birdIndex] =
            float4(velocity, 0.0f);
        resources->positions[birdIndex] +=
            float4(velocity * deltaTime * 15.0f, 0.0f);
        float phase = resources->phases[birdIndex].x;
        phase += deltaTime;
        phase += length(float2(velocity.x, velocity.z)) *
            deltaTime * 3.0f;
        phase += max(velocity.y, 0.0f) * deltaTime * 6.0f;
        resources->phases[birdIndex] =
            float4(fmod(phase, 62.83f), 0.0f, 0.0f, 0.0f);
    }
};

/** Carries linear Scene color, fog distance, and entity identity. */
struct WebgpuComputeBirdsVertexOutput
{
    float4 position [[Position]];
    float4 colorAndDistance [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the shared linear Scene color and depth attachments. */
struct WebgpuComputeBirdsSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final single-sample RGBA8 attachment. */
struct WebgpuComputeBirdsOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen output coordinates. */
struct WebgpuComputeBirdsScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Mixes one linear material color with the white r185 linear fog. */
float3 webgpuComputeBirdsApplyFog(float3 color, float distanceValue)
{
    const float fogFactor =
        smoothstep(700.0f, 3000.0f, distanceValue);
    return lerp(color, float3(1.0f), fogFactor);
}

/** Applies Three r185 NeutralToneMapping. */
float3 webgpuComputeBirdsNeutralToneMap(float3 color)
{
    const float minimumChannel = min(color.x, min(color.y, color.z));
    const float offset = minimumChannel < 0.08f
        ? minimumChannel - 6.25f * minimumChannel * minimumChannel
        : 0.04f;
    color -= float3(offset);
    const float peak = max(color.x, max(color.y, color.z));
    if (peak < 0.76f) return color;
    const float newPeak =
        1.0f - 0.0576f / (peak - 0.52f);
    color *= newPeak / peak;
    const float weight =
        1.0f - 1.0f / (0.15f * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak), weight);
}

/** Converts one linear output channel to sRGB. */
float webgpuComputeBirdsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws only the backside detail-six sky entity through the unique Set. */
class WebgpuComputeBirdsSkyPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and preserves only its inward sky faces. */
    constructor(RenderSet<WebgpuComputeBirdsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one sky vertex and evaluates its local-height color. */
    WebgpuComputeBirdsVertexOutput vertex(
        WebgpuComputeBirdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuComputeBirdsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuComputeBirdsInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 worldPosition =
            mul(objectData.model, float4(inputValue.positionAndVertex.xyz, 1.0f));
        const float4 viewPosition =
            mul(objectData.view, worldPosition);
        WebgpuComputeBirdsVertexOutput outputValue;
        outputValue.position =
            mul(objectData.projection, viewPosition);
        outputValue.colorAndDistance = float4(
            0.25f - inputValue.positionAndVertex.y,
            -0.25f - inputValue.positionAndVertex.y,
            1.5f + inputValue.positionAndVertex.y,
            -viewPosition.z + instanceData.ordinal.x * 0.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Writes only material phase zero and applies linear fog. */
    WebgpuComputeBirdsSceneFrameBuffer fragment(
        WebgpuComputeBirdsVertexOutput inputValue)
    {
        const WebgpuComputeBirdsMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (material.colorAndPhase.w > 0.5f) discard_fragment();
        const float3 skyColor = max(
            float3(
                inputValue.colorAndDistance.x,
                inputValue.colorAndDistance.y,
                inputValue.colorAndDistance.z),
            float3(0.0f));
        WebgpuComputeBirdsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(webgpuComputeBirdsApplyFog(
                skyColor,
                inputValue.colorAndDistance.w)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws only the double-sided 8192-instance flock through the same Set. */
class WebgpuComputeBirdsFlockPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and the computed instance-state buffers. */
    constructor(
        RenderSet<WebgpuComputeBirdsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuComputeBirdsDrawResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies wing deformation, velocity orientation, and instance position. */
    WebgpuComputeBirdsVertexOutput vertex(
        WebgpuComputeBirdsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuComputeBirdsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuComputeBirdsInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 positionAndPhase =
            float4(
                resources->positions[renderEntityInstanceID].xyz,
                resources->phases[renderEntityInstanceID].x);
        float3 velocity =
            webgpuComputeBirdsNormalize(
                resources->velocities[renderEntityInstanceID].xyz);
        float3 localPosition = inputValue.positionAndVertex.xyz;
        const uint birdVertex =
            uint(inputValue.positionAndVertex.w);
        if (birdVertex == 4u || birdVertex == 7u)
        {
            localPosition.y =
                sin(positionAndPhase.w) * 5.0f;
        }
        localPosition =
            mul(objectData.model, float4(localPosition, 1.0f)).xyz;
        velocity.z *= -1.0f;
        const float horizontalLength =
            max(length(float2(velocity.x, velocity.z)), 0.000001f);
        const float cosineY = velocity.x / horizontalLength;
        const float sineY = velocity.z / horizontalLength;
        const float cosineZ =
            sqrt(max(1.0f - velocity.y * velocity.y, 0.0f));
        const float sineZ = velocity.y;
        const float3 zRotatedPosition = float3(
            cosineZ * localPosition.x - sineZ * localPosition.y,
            sineZ * localPosition.x + cosineZ * localPosition.y,
            localPosition.z);
        float3 worldPosition = float3(
            cosineY * zRotatedPosition.x + sineY * zRotatedPosition.z,
            zRotatedPosition.y,
            -sineY * zRotatedPosition.x + cosineY * zRotatedPosition.z);
        worldPosition += positionAndPhase.xyz;
        const float4 viewPosition =
            mul(objectData.view, float4(worldPosition, 1.0f));
        WebgpuComputeBirdsVertexOutput outputValue;
        outputValue.position =
            mul(objectData.projection, viewPosition);
        outputValue.colorAndDistance = float4(
            0.0f,
            0.0f,
            0.0f,
            -viewPosition.z);
        outputValue.entityID =
            renderEntityID + uint(instanceData.ordinal.x * 0.0f);
        return outputValue;
    }

    /** Writes only material phase one and applies linear fog to black birds. */
    WebgpuComputeBirdsSceneFrameBuffer fragment(
        WebgpuComputeBirdsVertexOutput inputValue)
    {
        const WebgpuComputeBirdsMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (material.colorAndPhase.w < 0.5f) discard_fragment();
        WebgpuComputeBirdsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(webgpuComputeBirdsApplyFog(
                material.colorAndPhase.xyz,
                inputValue.colorAndDistance.w)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Applies neutral tone mapping and the sRGB output transfer. */
class WebgpuComputeBirdsNeutralOutputPass final : public IRenderClass
{
public:
    /** Binds the single-sample linear Scene image. */
    constructor(
        BindGroup<WebgpuComputeBirdsOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuComputeBirdsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeBirdsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Converts the centered linear sample to final RGBA8. */
    WebgpuComputeBirdsOutputFrameBuffer fragment(
        WebgpuComputeBirdsScreenOutput inputValue)
    {
        const float3 mapped =
            webgpuComputeBirdsNeutralToneMap(
                resources->sceneColor->sample(
                    resources->sceneSampler,
                    inputValue.uv).xyz);
        WebgpuComputeBirdsOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuComputeBirdsLinearToSrgb(mapped.x),
                webgpuComputeBirdsLinearToSrgb(mapped.y),
                webgpuComputeBirdsLinearToSrgb(mapped.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Composites the non-default Inspector parameters bar. */
class WebgpuComputeBirdsInspectorPass final : public IRenderClass
{
public:
    /** Configures standard source-alpha screen blending. */
    constructor(
        BindGroup<WebgpuComputeBirdsOutputResources> resources [[Slot0]])
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
    WebgpuComputeBirdsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeBirdsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked rounded Inspector bar and shadow. */
    WebgpuComputeBirdsOutputFrameBuffer fragment(
        WebgpuComputeBirdsScreenOutput inputValue)
    {
        if (resources->controls->inspectorEnabled.x < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel =
            float2(inputValue.uv.x * 800.0f, inputValue.uv.y * 500.0f);
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float cornerRadius =
            pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta =
            abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) -
            cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowDelta =
                abs(pixel - float2(699.5f, 37.5f)) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) -
                cornerRadius;
            const float shadowAlpha =
                0.13f * exp(
                    -max(shadowDistance, 0.0f) *
                    max(shadowDistance, 0.0f) / 72.0f);
            if (shadowAlpha < 0.004f) discard_fragment();
            WebgpuComputeBirdsOutputFrameBuffer shadow;
            shadow.color =
                half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadow;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (pixel.x < 663.0f)
        {
            color =
                float3(23.1818f, 61.8182f, 85.7727f) / 255.0f;
            alpha = 0.88f;
        }
        if (roundedDistance > -1.0f)
        {
            color =
                float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuComputeBirdsOutputFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated flock Compute state and unique Scene RenderSet. */
class WebgpuComputeBirdsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuComputeBirdsSceneRenderSet> sceneSet;
    Buffer<float4, BufferUsage<Storage, CopyDst>> positions;
    Buffer<float4, BufferUsage<Storage, CopyDst>> velocities;
    Buffer<float4, BufferUsage<Storage, CopyDst>> nextVelocities;
    Buffer<float4, BufferUsage<Storage, CopyDst>> phases;
    Buffer<WebgpuComputeBirdsControls, BufferUsage<Uniform, CopyDst>>
        controls;
    BindGroup<WebgpuComputeBirdsComputeResources> computeResources;
    BindGroup<WebgpuComputeBirdsDrawResources> drawResources;
    ComputeClass<WebgpuComputeBirdsInitPass> initPass;
    ComputeClass<WebgpuComputeBirdsVelocityPass> velocityPass;
    ComputeClass<WebgpuComputeBirdsPositionPass> positionPass;
    RenderClass<WebgpuComputeBirdsSkyPass> skyPass;
    RenderClass<WebgpuComputeBirdsFlockPass> flockPass;
    Sampler outputSampler;
    BindGroup<WebgpuComputeBirdsOutputResources> outputResources;
    RenderClass<WebgpuComputeBirdsNeutralOutputPass> outputPass;
    RenderClass<WebgpuComputeBirdsInspectorPass> inspectorPass;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint frameIndex = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the Scene Set, flock buffers, ordered Compute, and draw passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuComputeBirdsSceneRenderSet>();
        positions = device->createBuffer(
            "WebgpuComputeBirdsPositions",
            WebgpuComputeBirdsCount);
        velocities = device->createBuffer(
            "WebgpuComputeBirdsVelocities",
            WebgpuComputeBirdsCount);
        nextVelocities = device->createBuffer(
            "WebgpuComputeBirdsNextVelocities",
            WebgpuComputeBirdsCount);
        phases = device->createBuffer(
            "WebgpuComputeBirdsPhases",
            WebgpuComputeBirdsCount);
        controls = device->createBuffer(
            "WebgpuComputeBirdsControls",
            1u);
        computeResources =
            device->createBindGroup<WebgpuComputeBirdsComputeResources>(
                positions,
                velocities,
                nextVelocities,
                phases,
                controls);
        drawResources =
            device->createBindGroup<WebgpuComputeBirdsDrawResources>(
                positions,
                velocities,
                phases);
        initPass =
            device->createComputeClass<WebgpuComputeBirdsInitPass>(
                sceneSet,
                computeResources);
        velocityPass =
            device->createComputeClass<WebgpuComputeBirdsVelocityPass>(
                computeResources);
        positionPass =
            device->createComputeClass<WebgpuComputeBirdsPositionPass>(
                computeResources);
        skyPass =
            device->createRenderClass<WebgpuComputeBirdsSkyPass>(
                sceneSet);
        flockPass =
            device->createRenderClass<WebgpuComputeBirdsFlockPass>(
                sceneSet,
                drawResources);
        outputSampler = device->createSampler({
            .label = "WebgpuComputeBirdsOutputSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates ordinary single-sample Scene, depth, and output textures. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebgpuComputeBirdsSceneRGBA16",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebgpuComputeBirdsRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebgpuComputeBirdsDepth32",
            width,
            height,
            1u);
        outputResources =
            device->createBindGroup<WebgpuComputeBirdsOutputResources>(
                sceneTexture->createView(),
                outputSampler,
                controls);
        outputPass =
            device->createRenderClass<WebgpuComputeBirdsNeutralOutputPass>(
                outputResources);
        inspectorPass =
            device->createRenderClass<WebgpuComputeBirdsInspectorPass>(
                outputResources);
    }

    /** Uploads immutable scenario controls used by Compute and output passes. */
    void configureState(
        float4 separationAlignmentCohesionDelta,
        float4 rayOrigin,
        float4 rayDirection,
        float inspectorEnabled)
    {
        WebgpuComputeBirdsControls value;
        value.separationAlignmentCohesionDelta =
            separationAlignmentCohesionDelta;
        value.rayOrigin = rayOrigin;
        value.rayDirection = rayDirection;
        value.inspectorEnabled =
            float4(inspectorEnabled, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(controls),
                &value,
                sizeof(value))
            ->submit();
    }

    /** Advances the two ordered Compute passes and draws both Scene entities. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebgpuComputeBirdsSceneFrameBuffer skyFrameBuffer;
        skyFrameBuffer.color = sceneTexture->createView();
        skyFrameBuffer.color.loadOp = LoadOp::Clear;
        skyFrameBuffer.color.storeOp = StoreOp::Store;
        skyFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        skyFrameBuffer.depth = depthTexture->createView();
        skyFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        skyFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        skyFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuComputeBirdsSceneFrameBuffer flockFrameBuffer;
        flockFrameBuffer.color = sceneTexture->createView();
        flockFrameBuffer.color.loadOp = LoadOp::Load;
        flockFrameBuffer.color.storeOp = StoreOp::Store;
        flockFrameBuffer.depth = depthTexture->createView();
        flockFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        flockFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        WebgpuComputeBirdsOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        if (frameIndex == 0u)
        {
            graphicsQueue
                ->computePass(
                    "WebgpuComputeBirdsInit",
                    initPass(WebgpuComputeBirdsCount, 1u, 1u))
                ->renderPass(
                    "WebgpuComputeBirdsSky",
                    skyFrameBuffer,
                    skyPass())
                ->renderPass(
                    "WebgpuComputeBirdsFlock",
                    flockFrameBuffer,
                    flockPass())
                ->renderPass(
                    "WebgpuComputeBirdsNeutralOutput",
                    outputFrameBuffer,
                    outputPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue
                ->computePass(
                    "WebgpuComputeBirdsVelocity",
                    velocityPass(WebgpuComputeBirdsCount, 1u, 1u))
                ->computePass(
                    "WebgpuComputeBirdsPosition",
                    positionPass(WebgpuComputeBirdsCount, 1u, 1u))
                ->renderPass(
                    "WebgpuComputeBirdsSky",
                    skyFrameBuffer,
                    skyPass())
                ->renderPass(
                    "WebgpuComputeBirdsFlock",
                    flockFrameBuffer,
                    flockPass())
                ->renderPass(
                    "WebgpuComputeBirdsNeutralOutput",
                    outputFrameBuffer,
                    outputPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        ++frameIndex;
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene Set and all single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
