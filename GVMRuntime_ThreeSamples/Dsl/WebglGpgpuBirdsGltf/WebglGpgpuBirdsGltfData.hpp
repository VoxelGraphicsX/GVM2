#ifndef GVM_THREE_WEBGL_GPGPU_BIRDS_GLTF_DATA_HPP
#define GVM_THREE_WEBGL_GPGPU_BIRDS_GLTF_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one physically batched glTF vertex and its deterministic bird identity. */
struct WebglGpgpuBirdsGltfVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 animationReference [[Attribute2]];
    float4 seeds [[Attribute3]];
};

/** Stores the current GPU-produced position, phase, and velocity for one bird. */
struct WebglGpgpuBirdsGltfSimulationState
{
    float4 positionAndPhase;
    float4 velocityAndReserved;
};

/** Stores the fixed camera, current animation time, size, and fog parameters. */
struct WebglGpgpuBirdsGltfObjectData
{
    float4x4 projectionMatrix;
    float4x4 viewMatrix;
    float4 cameraPositionAndTime;
    float4 sizeAndFogRange;
};

/** Stores the mandatory identity transform for the sole non-instanced entity. */
struct WebglGpgpuBirdsGltfInstanceData
{
    float4 translationAndScale;
};

/** Stores the material base color and the immutable source-model selector. */
struct WebglGpgpuBirdsGltfMaterialData
{
    float4 baseColor;
    uint4 modelAndReserved;
};

/** Carries one deterministic callback's private flocking simulation parameters. */
struct WebglGpgpuBirdsGltfSimulationUniforms
{
    float4 deltaAndTime;
    float4 distancesAndFreedom;
    float4 predator;
};

/** Stores the fixed pixel-center offset and requested Scene viewport. */
struct WebglGpgpuBirdsGltfSampleUniforms
{
    float4 sampleOffsetAndViewport;
};

#endif
