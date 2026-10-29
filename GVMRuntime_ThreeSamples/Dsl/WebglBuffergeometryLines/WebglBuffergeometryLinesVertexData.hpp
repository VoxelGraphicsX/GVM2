#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_LINES_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one base position, absolute morph target, and interpolated line color. */
struct WebglBuffergeometryLinesVertex
{
    float3 position [[Attribute0]];
    float3 morphPosition [[Attribute1]];
    float3 color [[Attribute2]];
};

#endif
