#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_SELECTIVE_DRAW_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_SELECTIVE_DRAW_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one triangle-list vertex expanded from an r185 selective line segment. */
struct WebglBuffergeometrySelectiveDrawVertex
{
    float3 startPosition [[Attribute0]];
    float3 endPosition [[Attribute1]];
    float3 startColor [[Attribute2]];
    float3 endColor [[Attribute3]];
    float2 lineCoordinate [[Attribute4]];
    float visible [[Attribute5]];
};

#endif
