#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_INDEXED_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_INDEXED_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one shared indexed grid vertex with the exact r185 attributes. */
struct WebglBuffergeometryIndexedVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float3 color [[Attribute2]];
};

/** Stores one statically expanded wire edge as a screen-space triangle-list vertex. */
struct WebglBuffergeometryIndexedWireVertex
{
    float3 startPosition [[Attribute0]];
    float3 endPosition [[Attribute1]];
    float3 startColor [[Attribute2]];
    float3 endColor [[Attribute3]];
    float2 lineCoordinate [[Attribute4]];
};

#endif
