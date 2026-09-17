#ifndef GVM_THREE_WEBGL_MATERIALS_NORMALMAP_DATA_HPP
#define GVM_THREE_WEBGL_MATERIALS_NORMALMAP_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one expanded Lee Perry Smith vertex and its tangent frame. */
struct WebglMaterialsNormalmapVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float3 tangent [[Attribute3]];
    float3 bitangent [[Attribute4]];
};

/** Stores the camera, lights, material switch, and fixed output extent. */
struct WebglMaterialsNormalmapUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
    float4 pointLightPositionAndIntensity;
    float4 directionalLightAndIntensity;
    float4 materialAndNormalState;
    float4 viewport;
};

#endif
