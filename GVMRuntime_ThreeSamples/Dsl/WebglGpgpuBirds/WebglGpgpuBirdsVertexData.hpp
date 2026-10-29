#ifndef GVM_THREE_WEBGL_GPGPU_BIRDS_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_GPGPU_BIRDS_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one physically batched, non-instanced r185 bird triangle vertex. */
struct WebglGpgpuBirdsVertex
{
    float3 position [[Attribute0]];
    float3 birdColor [[Attribute1]];
    float2 reference [[Attribute2]];
    float birdVertex [[Attribute3]];
};

/** Carries the exact per-callback flock parameters shared by both simulation passes. */
struct WebglGpgpuBirdsSimulationUniforms
{
    float4 deltaAndTime;
    float4 distancesAndFreedom;
    float4 predator;
};

/** Carries the fixed r185 perspective and view transforms for the sole Scene draw. */
struct WebglGpgpuBirdsRenderUniforms
{
    float4x4 projectionMatrix;
    float4x4 viewMatrix;
};

#endif
