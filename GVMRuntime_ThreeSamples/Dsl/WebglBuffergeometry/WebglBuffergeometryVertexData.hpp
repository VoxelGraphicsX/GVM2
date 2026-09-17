#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one authored non-indexed r185 triangle vertex with Float32 RGBA color. */
struct WebglBuffergeometryVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float4 color [[Attribute2]];
};

#endif
