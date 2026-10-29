#ifndef GVM_THREE_WEBGL_MATERIALS_BUMPMAP_DATA_HPP
#define GVM_THREE_WEBGL_MATERIALS_BUMPMAP_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Lee Perry Smith vertex required by the private bump material. */
struct WebglMaterialsBumpmapVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Stores fixed camera, shadow, lighting, and bump-map scenario state. */
struct WebglMaterialsBumpmapUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 shadowModelViewProjection;
    float4 lightPositionAndIntensity;
    float4 spotDirectionAndBumpScale;
    float4 hemisphereSky;
    float4 hemisphereGround;
};

#endif
