#ifndef GVM_THREE_WEBGPU_MATERIALS_DISPLACEMENTMAP_DATA_HPP
#define GVM_THREE_WEBGPU_MATERIALS_DISPLACEMENTMAP_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one OBJLoader-compatible ninja-head vertex. */
struct WebgpuMaterialsDisplacementmapVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Stores the orthographic camera, lights, and mutable physical material state. */
struct WebgpuMaterialsDisplacementmapUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
    float4 redLightPositionAndIntensity;
    float4 cameraLightPositionAndIntensity;
    float4 blueLightPositionAndIntensity;
    float4 materialState0;
    float4 materialState1;
    float4 viewportAndUi;
};

#endif
