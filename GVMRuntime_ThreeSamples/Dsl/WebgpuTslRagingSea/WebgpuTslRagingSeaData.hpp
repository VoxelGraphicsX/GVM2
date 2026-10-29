#ifndef GVM_THREE_WEBGPU_TSL_RAGING_SEA_DATA_HPP
#define GVM_THREE_WEBGPU_TSL_RAGING_SEA_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one vertex of the exact 256-by-256 r185 PlaneGeometry grid. */
struct WebgpuTslRagingSeaVertex
{
    float3 position [[Attribute0]];
};

/** Stores the deterministic camera, material, wave, light, and output state. */
struct WebgpuTslRagingSeaUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 cameraPositionAndTime;
    float4 lightDirectionAndIntensity;
    float4 baseColorAndRoughness;
    float4 emissiveColorAndLow;
    float4 emissiveHighPowerShiftIterations;
    float4 largeFrequencySpeedMultiplier;
    float4 smallFrequencySpeedMultiplier;
    float4 viewportAndInspector;
};

#endif
