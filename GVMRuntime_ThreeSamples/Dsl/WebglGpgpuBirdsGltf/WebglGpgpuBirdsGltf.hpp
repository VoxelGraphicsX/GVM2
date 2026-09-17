#ifndef GVM_THREE_WEBGL_GPGPU_BIRDS_GLTF_HPP
#define GVM_THREE_WEBGL_GPGPU_BIRDS_GLTF_HPP

#include "UGL.h"
#include "WebglGpgpuBirdsGltfData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglGpgpuBirdsGltfTextureWidth = 64u;
static const uint WebglGpgpuBirdsGltfBirdCount =
    WebglGpgpuBirdsGltfTextureWidth * WebglGpgpuBirdsGltfTextureWidth;
static const uint WebglGpgpuBirdsGltfMaxAnimationTextures = 2u;

/** Defines the sole Scene RenderSet used by the loaded flock entity. */
struct WebglGpgpuBirdsGltfSceneRenderSet : public IRenderSet
{
    /** Declares geometry, entity data, GPU-readback simulation, and morph texture components. */
    constructor(
        BufferComponent<WebglGpgpuBirdsGltfVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGpgpuBirdsGltfObjectData> objects,
        BufferComponent<WebglGpgpuBirdsGltfInstanceData> instances,
        BufferComponent<WebglGpgpuBirdsGltfMaterialData> materials,
        BufferComponent<WebglGpgpuBirdsGltfSimulationState> simulationStates,
        (TextureComponent<float4, WebglGpgpuBirdsGltfMaxAnimationTextures> animationTextures))
    {
    }
};

/** Binds one immutable source pair to the private velocity simulation pass. */
struct WebglGpgpuBirdsGltfVelocityBindGroup final : public IBindGroup
{
    /** Declares full-precision source textures and exact flock controls. */
    constructor(
        UniformBuffer<WebglGpgpuBirdsGltfSimulationUniforms> uniforms [[Binding0]],
        Texture2D<float4> positionTexture [[Binding1]],
        Texture2D<float4> velocityTexture [[Binding2]],
        Sampler simulationSampler [[Binding3]])
    {
    }
};

/** Binds one immutable source pair to the private position simulation pass. */
struct WebglGpgpuBirdsGltfPositionBindGroup final : public IBindGroup
{
    /** Declares full-precision source textures and exact integration controls. */
    constructor(
        UniformBuffer<WebglGpgpuBirdsGltfSimulationUniforms> uniforms [[Binding0]],
        Texture2D<float4> positionTexture [[Binding1]],
        Texture2D<float4> velocityTexture [[Binding2]],
        Sampler simulationSampler [[Binding3]])
    {
    }
};

/** Binds the immutable morph sampler and fixed pixel-center Scene offset. */
struct WebglGpgpuBirdsGltfSceneResources final : public IBindGroup
{
    /** Declares the nearest clamp sampler and requested-size transform. */
    constructor(
        Sampler animationSampler [[Binding0]],
        UniformBuffer<WebglGpgpuBirdsGltfSampleUniforms> sampleUniforms [[Binding1]])
    {
    }
};

/** Carries one fullscreen triangle into a full-precision simulation fragment pass. */
struct WebglGpgpuBirdsGltfSimulationVertexOutput
{
    float4 position [[Position]];
};

/** Defines one RGBA32Float ping-pong target matching GPUComputationRenderer. */
struct WebglGpgpuBirdsGltfSimulationFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
};

/** Carries the loaded vertex color and view-space position into flat PBR shading. */
struct WebglGpgpuBirdsGltfSceneVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    uint renderEntityID [[Attribute2]];
};

/** Defines one requested-size Scene color and independent depth attachment. */
struct WebglGpgpuBirdsGltfSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces one r185 64 by 64 flock velocity fragment step. */
class WebglGpgpuBirdsGltfVelocityPass final : public IRenderClass
{
public:
    /** Binds one fixed ping-pong direction and disables triangle culling. */
    constructor(BindGroup<WebglGpgpuBirdsGltfVelocityBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by GPUComputationRenderer. */
    WebglGpgpuBirdsGltfSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuBirdsGltfSimulationVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Scans all 4,096 birds and emits the next full-precision velocity. */
    WebglGpgpuBirdsGltfSimulationFrameBuffer fragment(
        WebglGpgpuBirdsGltfSimulationVertexOutput inputValue)
    {
        const float delta = bindGroup->uniforms->deltaAndTime.x;
        const float separationDistance =
            bindGroup->uniforms->distancesAndFreedom.x;
        const float alignmentDistance =
            bindGroup->uniforms->distancesAndFreedom.y;
        const float cohesionDistance =
            bindGroup->uniforms->distancesAndFreedom.z;
        const float zoneRadius =
            separationDistance + alignmentDistance + cohesionDistance;
        const float separationThreshold = separationDistance / zoneRadius;
        const float alignmentThreshold =
            (separationDistance + alignmentDistance) / zoneRadius;
        const float zoneRadiusSquared = zoneRadius * zoneRadius;
        const float2 resolution = float2(64.0f, 64.0f);
        const float2 selfUv = inputValue.position.xy / resolution;
        const float3 selfPosition = bindGroup->positionTexture
                                        ->sample(bindGroup->simulationSampler, selfUv)
                                        .xyz;
        const float3 selfVelocity = bindGroup->velocityTexture
                                        ->sample(bindGroup->simulationSampler, selfUv)
                                        .xyz;
        float3 velocity = selfVelocity;
        float limit = 9.0f;
        float3 direction =
            bindGroup->uniforms->predator.xyz * 800.0f - selfPosition;
        direction.z = 0.0f;
        float distanceValue = length(direction);
        float distanceSquared = distanceValue * distanceValue;
        if (distanceValue < 150.0f)
        {
            const float force =
                (distanceSquared / 22500.0f - 1.0f) * delta * 100.0f;
            velocity += normalize(direction) * force;
            limit += 5.0f;
        }

        direction = selfPosition;
        distanceValue = length(direction);
        direction.y *= 2.5f;
        velocity -= normalize(direction) * delta * 5.0f;

        for (float y = 0.0f; y < 64.0f; y += 1.0f)
        {
            for (float x = 0.0f; x < 64.0f; x += 1.0f)
            {
                const float2 reference =
                    float2(x + 0.5f, y + 0.5f) / resolution;
                const float3 birdPosition = bindGroup->positionTexture
                                                ->sample(
                                                    bindGroup->simulationSampler,
                                                    reference)
                                                .xyz;
                direction = birdPosition - selfPosition;
                distanceValue = length(direction);
                if (distanceValue < 0.0001f)
                {
                    continue;
                }
                distanceSquared = distanceValue * distanceValue;
                if (distanceSquared > zoneRadiusSquared)
                {
                    continue;
                }
                const float percent = distanceSquared / zoneRadiusSquared;
                if (percent < separationThreshold)
                {
                    const float force =
                        (separationThreshold / percent - 1.0f) * delta;
                    velocity -= normalize(direction) * force;
                }
                else if (percent < alignmentThreshold)
                {
                    const float thresholdDelta =
                        alignmentThreshold - separationThreshold;
                    const float adjustedPercent =
                        (percent - separationThreshold) / thresholdDelta;
                    const float3 birdVelocity = bindGroup->velocityTexture
                                                    ->sample(
                                                        bindGroup->simulationSampler,
                                                        reference)
                                                    .xyz;
                    const float force =
                        (0.5f -
                         cos(adjustedPercent * 6.28318530717958647692f) *
                             0.5f +
                         0.5f) *
                        delta;
                    velocity += normalize(birdVelocity) * force;
                }
                else
                {
                    const float thresholdDelta = 1.0f - alignmentThreshold;
                    float adjustedPercent = 1.0f;
                    if (thresholdDelta != 0.0f)
                    {
                        adjustedPercent =
                            (percent - alignmentThreshold) / thresholdDelta;
                    }
                    const float force =
                        (0.5f -
                         (cos(adjustedPercent * 6.28318530717958647692f) *
                              -0.5f +
                          0.5f)) *
                        delta;
                    velocity += normalize(direction) * force;
                }
            }
        }
        if (length(velocity) > limit)
        {
            velocity = normalize(velocity) * limit;
        }
        WebglGpgpuBirdsGltfSimulationFrameBuffer frameBuffer;
        frameBuffer.color = float4(velocity, 1.0f);
        return frameBuffer;
    }
};

/** Reproduces one r185 64 by 64 flock position fragment step. */
class WebglGpgpuBirdsGltfPositionPass final : public IRenderClass
{
public:
    /** Binds one fixed ping-pong direction and disables triangle culling. */
    constructor(BindGroup<WebglGpgpuBirdsGltfPositionBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by GPUComputationRenderer. */
    WebglGpgpuBirdsGltfSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuBirdsGltfSimulationVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Integrates old position and velocity while advancing the retained phase. */
    WebglGpgpuBirdsGltfSimulationFrameBuffer fragment(
        WebglGpgpuBirdsGltfSimulationVertexOutput inputValue)
    {
        const float2 uv = inputValue.position.xy / float2(64.0f, 64.0f);
        const float4 positionAndPhase = bindGroup->positionTexture->sample(
            bindGroup->simulationSampler,
            uv);
        const float3 velocity = bindGroup->velocityTexture
                                    ->sample(bindGroup->simulationSampler, uv)
                                    .xyz;
        const float delta = bindGroup->uniforms->deltaAndTime.x;
        float phase =
            positionAndPhase.w + delta +
            length(float2(velocity.x, velocity.z)) * delta * 3.0f +
            max(velocity.y, 0.0f) * delta * 6.0f;
        phase = fmod(phase, 62.83f);
        WebglGpgpuBirdsGltfSimulationFrameBuffer frameBuffer;
        frameBuffer.color =
            float4(positionAndPhase.xyz + velocity * delta * 15.0f, phase);
        return frameBuffer;
    }
};

/** Returns Three r185's optimized Schlick Fresnel approximation. */
float3 webglGpgpuBirdsGltfFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Returns one exact half-float row from Three r185's DFG LUT at roughness one. */
float2 webglGpgpuBirdsGltfDfgRoughnessOneRow(uint row)
{
    if (row == 0u)
        return float2(0.85986328125f, 0.039031982421875f);
    if (row == 1u)
        return float2(0.7548828125f, 0.0269775390625f);
    if (row == 2u)
        return float2(0.68359375f, 0.019439697265625f);
    if (row == 3u)
        return float2(0.62841796875f, 0.01419830322265625f);
    if (row == 4u)
        return float2(0.583984375f, 0.01041412353515625f);
    if (row == 5u)
        return float2(0.54638671875f, 0.007633209228515625f);
    if (row == 6u)
        return float2(0.51416015625f, 0.005565643310546875f);
    if (row == 7u)
        return float2(0.486083984375f, 0.00402069091796875f);
    if (row == 8u)
        return float2(0.46142578125f, 0.0028629302978515625f);
    if (row == 9u)
        return float2(0.439453125f, 0.0020046234130859375f);
    if (row == 10u)
        return float2(0.419677734375f, 0.00136566162109375f);
    if (row == 11u)
        return float2(0.402099609375f, 0.0008993148803710938f);
    if (row == 12u)
        return float2(0.385986328125f, 0.0005650520324707031f);
    if (row == 13u)
        return float2(0.37109375f, 0.0003311634063720703f);
    if (row == 14u)
        return float2(0.357666015625f, 0.00017201900482177734f);
    return float2(0.34521484375f, 0.00007051229476928711f);
}

/** Reproduces normalized linear sampling of the roughness-one DFG column. */
float2 webglGpgpuBirdsGltfDfgRoughnessOne(float dotNormalView)
{
    const float texelCoordinate =
        clamp(dotNormalView, 0.0f, 1.0f) * 16.0f - 0.5f;
    if (texelCoordinate <= 0.0f)
        return webglGpgpuBirdsGltfDfgRoughnessOneRow(0u);
    if (texelCoordinate >= 15.0f)
        return webglGpgpuBirdsGltfDfgRoughnessOneRow(15u);
    const float lowerCoordinate = floor(texelCoordinate);
    const uint lowerRow = uint(lowerCoordinate);
    return lerp(
        webglGpgpuBirdsGltfDfgRoughnessOneRow(lowerRow),
        webglGpgpuBirdsGltfDfgRoughnessOneRow(lowerRow + 1u),
        texelCoordinate - lowerCoordinate);
}

/** Evaluates Three r185's roughness-one direct GGX multiscatter BRDF. */
float3 webglGpgpuBirdsGltfDirectSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalLight = saturate(dot(normal, lightDirection));
    const float dotNormalView = saturate(dot(normal, viewDirection));
    const float dotViewHalf = saturate(dot(viewDirection, halfDirection));
    const float visibility =
        0.5f / max(dotNormalLight + dotNormalView, 0.000001f);
    const float3 singleScatter =
        webglGpgpuBirdsGltfFresnel(float3(0.04f), dotViewHalf) *
        (visibility * 0.3183098861837907f);
    const float2 dfgView =
        webglGpgpuBirdsGltfDfgRoughnessOne(dotNormalView);
    const float2 dfgLight =
        webglGpgpuBirdsGltfDfgRoughnessOne(dotNormalLight);
    const float3 f0 = float3(0.04f);
    const float3 viewEnergy = f0 * dfgView.x + float3(dfgView.y);
    const float3 lightEnergy = f0 * dfgLight.x + float3(dfgLight.y);
    const float viewMissingEnergy = 1.0f - dfgView.x - dfgView.y;
    const float lightMissingEnergy = 1.0f - dfgLight.x - dfgLight.y;
    const float3 averageFresnel =
        f0 + (float3(1.0f) - f0) * 0.047619f;
    const float3 multipleFresnel =
        viewEnergy * lightEnergy * averageFresnel /
        (float3(1.0f) - averageFresnel *
             (viewMissingEnergy * lightMissingEnergy) +
         float3(0.000001f));
    return singleScatter +
           multipleFresnel * (viewMissingEnergy * lightMissingEnergy);
}

/** Converts one linear channel with Three r185's default sRGB output transfer. */
float webglGpgpuBirdsGltfLinearToSrgb(float value)
{
    return value <= 0.0031308f
               ? value * 12.92f
               : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the sole loaded flock entity through RenderSet indexed-indirect metadata. */
class WebglGpgpuBirdsGltfMainPass final : public IRenderClass
{
public:
    /** Binds the Scene's unique Set and immutable morph sampler. */
    constructor(
        RenderSet<WebglGpgpuBirdsGltfSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGpgpuBirdsGltfSceneResources> sceneResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity, bird, simulation, and morph data for one physical vertex. */
    WebglGpgpuBirdsGltfSceneVertexOutput vertex(
        WebglGpgpuBirdsGltfVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const uint birdIndex = uint(inputValue.seeds.x);
        const WebglGpgpuBirdsGltfSimulationState simulationState =
            sceneSet->simulationStates->get(renderEntityID, birdIndex);
        const WebglGpgpuBirdsGltfObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGpgpuBirdsGltfInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, 0u);
        auto animationTexture =
            sceneSet->animationTextures->get(renderEntityID, 0u);
        const float3 velocity = normalize(float3(
            simulationState.velocityAndReserved.x,
            simulationState.velocityAndReserved.y,
            simulationState.velocityAndReserved.z));
        float animationV = fmod(
            objectData.cameraPositionAndTime.w +
                inputValue.seeds.x *
                    (0.0004f + inputValue.seeds.y / 10000.0f +
                     velocity.x / 20000.0f),
            inputValue.animationReference.y);
        animationV = max(animationV, 0.0f);
        uint animationWidth = 1u;
        uint animationHeight = 1u;
        animationTexture->getDimensions(animationWidth, animationHeight);
        const uint animationX = min(
            uint(inputValue.animationReference.x * float(animationWidth)),
            animationWidth - 1u);
        const uint animationY = min(
            uint(animationV * float(animationHeight)),
            animationHeight - 1u);
        const float3 animationPosition =
            animationTexture->read(uint2(animationX, animationY), 0u).xyz;

        float3 localPosition = inputValue.position.xyz + animationPosition;
        localPosition = float3(
            localPosition.z,
            localPosition.y,
            -localPosition.x);
        const float size = objectData.sizeAndFogRange.x;
        localPosition *= size + inputValue.seeds.y * size * 0.2f;

        float3 orientedVelocity = velocity;
        orientedVelocity.z *= -1.0f;
        const float horizontalLength = length(orientedVelocity.xz);
        const float cosineY = orientedVelocity.x / horizontalLength;
        const float sineY = orientedVelocity.z / horizontalLength;
        const float cosineZ = sqrt(1.0f - orientedVelocity.y * orientedVelocity.y);
        const float sineZ = orientedVelocity.y;
        const float3 zRotated = float3(
            cosineZ * localPosition.x - sineZ * localPosition.y,
            sineZ * localPosition.x + cosineZ * localPosition.y,
            localPosition.z);
        float3 worldPosition = float3(
            cosineY * zRotated.x + sineY * zRotated.z,
            zRotated.y,
            -sineY * zRotated.x + cosineY * zRotated.z);
        worldPosition += simulationState.positionAndPhase.xyz;
        worldPosition = worldPosition * instanceData.translationAndScale.w +
                        instanceData.translationAndScale.xyz;
        worldPosition = float3(
            worldPosition.z,
            worldPosition.y,
            -worldPosition.x);

        const float4 cameraPosition = mul(
            objectData.viewMatrix,
            float4(worldPosition, 1.0f));
        float4 clipPosition = mul(
            objectData.projectionMatrix,
            cameraPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        const float4 sampleOffsetAndViewport =
            sceneResources->sampleUniforms->sampleOffsetAndViewport;
        clipPosition.x -=
            sampleOffsetAndViewport.x * 2.0f /
            sampleOffsetAndViewport.z * clipPosition.w;
        clipPosition.y +=
            sampleOffsetAndViewport.y * 2.0f /
            sampleOffsetAndViewport.w * clipPosition.w;

        WebglGpgpuBirdsGltfSceneVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = inputValue.color.xyz;
        outputValue.viewPosition = -cameraPosition.xyz;
        outputValue.renderEntityID = renderEntityID;
        return outputValue;
    }

    /** Applies flat Standard lighting, default sRGB output, and linear fog. */
    WebglGpgpuBirdsGltfSceneFrameBuffer fragment(
        WebglGpgpuBirdsGltfSceneVertexOutput inputValue)
    {
        const WebglGpgpuBirdsGltfMaterialData material =
            sceneSet->materials->get(inputValue.renderEntityID, 0u);
        const WebglGpgpuBirdsGltfObjectData objectData =
            sceneSet->objects->get(inputValue.renderEntityID, 0u);
        const float3 normal = normalize(cross(
            ddy(inputValue.viewPosition),
            ddx(inputValue.viewPosition)));
        WebglGpgpuBirdsGltfSceneFrameBuffer normalProbeFrameBuffer;
        normalProbeFrameBuffer.color = half4(
            half(1.0f), half(0.0f), half(0.0f), half(1.0f));
        return normalProbeFrameBuffer;
        const float3 viewDirection = normalize(inputValue.viewPosition);
        const float3 directionalDirection =
            normalize(float3(-1.0f, 1.75f, 1.0f));
        const float3 directionalColor =
            float3(1.0f, 0.9114075004f, 0.7874122894f) * 2.0f;
        const float directionalDot =
            saturate(dot(normal, directionalDirection));
        const float3 directionalIrradiance =
            directionalColor * directionalDot;
        const float hemisphereWeight =
            dot(normal, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
        const float3 hemisphereIrradiance = lerp(
            float3(1.0f, 0.5787145259f, 0.2140411405f) * 4.5f,
            float3(0.0331047666f, 0.2330219993f, 1.0f) * 4.5f,
            hemisphereWeight);
        const float3 diffuseColor =
            material.baseColor.xyz * inputValue.color;
        float3 linearColor =
            (directionalIrradiance + hemisphereIrradiance) *
            diffuseColor * 0.3183098861837907f;
        linearColor += directionalIrradiance *
                       webglGpgpuBirdsGltfDirectSpecular(
                           directionalDirection,
                           viewDirection,
                           normal);
        float3 displayColor = float3(
            webglGpgpuBirdsGltfLinearToSrgb(linearColor.x),
            webglGpgpuBirdsGltfLinearToSrgb(linearColor.y),
            webglGpgpuBirdsGltfLinearToSrgb(linearColor.z));
        const float fogFactor = smoothstep(
            objectData.sizeAndFogRange.y,
            objectData.sizeAndFogRange.z,
            inputValue.viewPosition.z);
        displayColor = lerp(
            displayColor,
            float3(0.8000028755f, 1.0f, 1.0f),
            fogFactor);
        WebglGpgpuBirdsGltfSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns private GPU simulation and the Scene's unique RenderSet. */
class WebglGpgpuBirdsGltfRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGpgpuBirdsGltfSceneRenderSet> sceneSet;
    Buffer<WebglGpgpuBirdsGltfSimulationUniforms, BufferUsage<Uniform, CopyDst>>
        simulationUniformBuffer;
    Buffer<WebglGpgpuBirdsGltfSampleUniforms, BufferUsage<Uniform, CopyDst>>
        sampleUniformBuffer0;
    Sampler simulationSampler;
    Sampler animationSampler;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        positionTexture0;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        positionTexture1;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        velocityTexture0;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        velocityTexture1;
    BindGroup<WebglGpgpuBirdsGltfVelocityBindGroup> velocityBindGroup0To1;
    BindGroup<WebglGpgpuBirdsGltfVelocityBindGroup> velocityBindGroup1To0;
    BindGroup<WebglGpgpuBirdsGltfPositionBindGroup> positionBindGroup0To1;
    BindGroup<WebglGpgpuBirdsGltfPositionBindGroup> positionBindGroup1To0;
    BindGroup<WebglGpgpuBirdsGltfSceneResources> sceneResources0;
    RenderClass<WebglGpgpuBirdsGltfVelocityPass> velocityPass0To1;
    RenderClass<WebglGpgpuBirdsGltfVelocityPass> velocityPass1To0;
    RenderClass<WebglGpgpuBirdsGltfPositionPass> positionPass0To1;
    RenderClass<WebglGpgpuBirdsGltfPositionPass> positionPass1To0;
    RenderClass<WebglGpgpuBirdsGltfMainPass> mainPass0;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        sceneDepth;
    uint currentSimulationIndex = 0u;
    uint simulationStepCount = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the frozen simulation resources and exactly one Scene RenderSet. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglGpgpuBirdsGltfSceneRenderSet>();
        simulationUniformBuffer = device->createBuffer(
            "WebglGpgpuBirdsGltfSimulationUniforms",
            1u);
        sampleUniformBuffer0 = device->createBuffer(
            "WebglGpgpuBirdsGltfSampleUniforms0",
            1u);
        simulationSampler = device->createSampler({
            .label = "WebglGpgpuBirdsGltfSimulationSampler",
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
        animationSampler = device->createSampler({
            .label = "WebglGpgpuBirdsGltfAnimationSampler",
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
        positionTexture0 = device->createTexture(
            "WebglGpgpuBirdsGltfPosition0RGBA32Float",
            WebglGpgpuBirdsGltfTextureWidth,
            WebglGpgpuBirdsGltfTextureWidth,
            1u);
        positionTexture1 = device->createTexture(
            "WebglGpgpuBirdsGltfPosition1RGBA32Float",
            WebglGpgpuBirdsGltfTextureWidth,
            WebglGpgpuBirdsGltfTextureWidth,
            1u);
        velocityTexture0 = device->createTexture(
            "WebglGpgpuBirdsGltfVelocity0RGBA32Float",
            WebglGpgpuBirdsGltfTextureWidth,
            WebglGpgpuBirdsGltfTextureWidth,
            1u);
        velocityTexture1 = device->createTexture(
            "WebglGpgpuBirdsGltfVelocity1RGBA32Float",
            WebglGpgpuBirdsGltfTextureWidth,
            WebglGpgpuBirdsGltfTextureWidth,
            1u);
        velocityBindGroup0To1 =
            device->createBindGroup<WebglGpgpuBirdsGltfVelocityBindGroup>(
                simulationUniformBuffer,
                positionTexture0->createView(),
                velocityTexture0->createView(),
                simulationSampler);
        velocityBindGroup1To0 =
            device->createBindGroup<WebglGpgpuBirdsGltfVelocityBindGroup>(
                simulationUniformBuffer,
                positionTexture1->createView(),
                velocityTexture1->createView(),
                simulationSampler);
        positionBindGroup0To1 =
            device->createBindGroup<WebglGpgpuBirdsGltfPositionBindGroup>(
                simulationUniformBuffer,
                positionTexture0->createView(),
                velocityTexture0->createView(),
                simulationSampler);
        positionBindGroup1To0 =
            device->createBindGroup<WebglGpgpuBirdsGltfPositionBindGroup>(
                simulationUniformBuffer,
                positionTexture1->createView(),
                velocityTexture1->createView(),
                simulationSampler);
        velocityPass0To1 =
            device->createRenderClass<WebglGpgpuBirdsGltfVelocityPass>(
                velocityBindGroup0To1);
        velocityPass1To0 =
            device->createRenderClass<WebglGpgpuBirdsGltfVelocityPass>(
                velocityBindGroup1To0);
        positionPass0To1 =
            device->createRenderClass<WebglGpgpuBirdsGltfPositionPass>(
                positionBindGroup0To1);
        positionPass1To0 =
            device->createRenderClass<WebglGpgpuBirdsGltfPositionPass>(
                positionBindGroup1To0);
        sceneResources0 =
            device->createBindGroup<WebglGpgpuBirdsGltfSceneResources>(
                animationSampler,
                sampleUniformBuffer0);
        mainPass0 =
            device->createRenderClass<WebglGpgpuBirdsGltfMainPass>(
                sceneSet,
                sceneResources0);
    }

    /** Allocates the requested-size single-sample Scene targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneColor = device->createTexture(
            "WebglGpgpuBirdsGltfSceneRGBA8",
            width,
            height,
            1u);
        sceneDepth = device->createTexture(
            "WebglGpgpuBirdsGltfSceneDepth32",
            width,
            height,
            1u);
        WebglGpgpuBirdsGltfSampleUniforms sample0;
        sample0.sampleOffsetAndViewport =
            float4(0.0f, 0.0f, float(width), float(height));
        graphicsQueue
            ->writeBuffer(
                BufferRange(sampleUniformBuffer0),
                &sample0,
                sizeof(sample0))
            ->submit();
    }

    /** Uploads exact initial states to both private ping-pong banks. */
    void configureSimulation(
        const eastl::vector<float4> &initialPosition,
        const eastl::vector<float4> &initialVelocity)
    {
        graphicsQueue
            ->writeTexture(
                positionTexture0,
                initialPosition.data(),
                uint64_t(WebglGpgpuBirdsGltfBirdCount) * sizeof(float4))
            ->writeTexture(
                positionTexture1,
                initialPosition.data(),
                uint64_t(WebglGpgpuBirdsGltfBirdCount) * sizeof(float4))
            ->writeTexture(
                velocityTexture0,
                initialVelocity.data(),
                uint64_t(WebglGpgpuBirdsGltfBirdCount) * sizeof(float4))
            ->writeTexture(
                velocityTexture1,
                initialVelocity.data(),
                uint64_t(WebglGpgpuBirdsGltfBirdCount) * sizeof(float4))
            ->submit();
        currentSimulationIndex = 0u;
        simulationStepCount = 0u;
    }

    /** Executes velocity then position from one old state and flips the current bank. */
    void advanceSimulation(WebglGpgpuBirdsGltfSimulationUniforms uniforms)
    {
        WebglGpgpuBirdsGltfSimulationFrameBuffer velocityFrameBuffer;
        velocityFrameBuffer.color.loadOp = LoadOp::Clear;
        velocityFrameBuffer.color.storeOp = StoreOp::Store;
        velocityFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglGpgpuBirdsGltfSimulationFrameBuffer positionFrameBuffer;
        positionFrameBuffer.color.loadOp = LoadOp::Clear;
        positionFrameBuffer.color.storeOp = StoreOp::Store;
        positionFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        graphicsQueue->writeBuffer(
            BufferRange(simulationUniformBuffer),
            &uniforms,
            sizeof(uniforms));
        if (currentSimulationIndex == 0u)
        {
            velocityFrameBuffer.color = velocityTexture1->createView();
            positionFrameBuffer.color = positionTexture1->createView();
            graphicsQueue
                ->renderPass(
                    "WebglGpgpuBirdsGltfVelocity0To1",
                    velocityFrameBuffer,
                    velocityPass0To1(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglGpgpuBirdsGltfPosition0To1",
                    positionFrameBuffer,
                    positionPass0To1(3u, 1u, 0u, 0u));
            currentSimulationIndex = 1u;
        }
        else
        {
            velocityFrameBuffer.color = velocityTexture0->createView();
            positionFrameBuffer.color = positionTexture0->createView();
            graphicsQueue
                ->renderPass(
                    "WebglGpgpuBirdsGltfVelocity1To0",
                    velocityFrameBuffer,
                    velocityPass1To0(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglGpgpuBirdsGltfPosition1To0",
                    positionFrameBuffer,
                    positionPass1To0(3u, 1u, 0u, 0u));
            currentSimulationIndex = 0u;
        }
        graphicsQueue->submit();
        simulationStepCount += 1u;
    }

    /** Draws the unique Set into the single-sample target and presents. */
    void render() override
    {
        sceneSet->update();
        WebglGpgpuBirdsGltfSceneFrameBuffer sceneFrameBuffer0;
        sceneFrameBuffer0.color = sceneColor->createView();
        sceneFrameBuffer0.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer0.color.storeOp = StoreOp::Store;
        sceneFrameBuffer0.color.clearValue = {0.8, 1.0, 1.0, 1.0};
        sceneFrameBuffer0.depth = sceneDepth->createView();
        sceneFrameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer0.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer0.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "main-standard-morph-flock",
                sceneFrameBuffer0,
                mainPass0())
            ->renderToSwapchain(
                nextTexture,
                sceneColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the current full-precision position and phase bank. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
    getCurrentPositionTextureHandle() const
    {
        return currentSimulationIndex == 0u
                   ? positionTexture0
                   : positionTexture1;
    }

    /** Returns the current full-precision velocity bank. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
    getCurrentVelocityTextureHandle() const
    {
        return currentSimulationIndex == 0u
                   ? velocityTexture0
                   : velocityTexture1;
    }

    /** Returns the number of private two-pass simulation steps submitted so far. */
    uint getSimulationStepCount() const
    {
        return simulationStepCount;
    }

    /** Returns the DSL-created final RGBA8 output used by host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return sceneColor;
    }

    /** Returns the explicitly configured final output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured final output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Set and every private simulation or attachment resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(simulationUniformBuffer);
        device->freeBuffer(sampleUniformBuffer0);
        device->freeTexture(positionTexture0);
        device->freeTexture(positionTexture1);
        device->freeTexture(velocityTexture0);
        device->freeTexture(velocityTexture1);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }
};

#endif
