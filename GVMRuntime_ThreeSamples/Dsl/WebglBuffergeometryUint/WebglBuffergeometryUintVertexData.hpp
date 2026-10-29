#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_UINT_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_UINT_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one dual-winding vertex while preserving the authored integer attribute bits. */
struct WebglBuffergeometryUintVertex
{
    float3 position [[Attribute0]];
    uint2 packedNormalSnorm16x3 [[Attribute1]];
    uint packedColorUnorm8x3 [[Attribute2]];
};

#endif
