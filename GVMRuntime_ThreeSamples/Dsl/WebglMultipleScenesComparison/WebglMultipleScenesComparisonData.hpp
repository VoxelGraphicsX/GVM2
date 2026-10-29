#ifndef GVM_THREE_WEBGL_MULTIPLE_SCENES_COMPARISON_DATA_HPP
#define GVM_THREE_WEBGL_MULTIPLE_SCENES_COMPARISON_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one non-indexed detail-3 icosahedron vertex and smooth normal. */
struct WebglMultipleScenesSolidVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores one triangle-expanded wire segment with endpoint normals. */
struct WebglMultipleScenesWireVertex
{
    float4 segmentStart [[Attribute0]];
    float4 segmentEnd [[Attribute1]];
    float4 startNormal [[Attribute2]];
    float4 endNormal [[Attribute3]];
    float4 endpointAndSide [[Attribute4]];
};

/** Stores the shared camera transform, light direction, and split position. */
struct WebglMultipleScenesUniforms
{
    float4 modelViewProjectionColumn0;
    float4 modelViewProjectionColumn1;
    float4 modelViewProjectionColumn2;
    float4 modelViewProjectionColumn3;
    float4 hemisphereDirection;
    float4 sliderAndReserved;
};

#endif
