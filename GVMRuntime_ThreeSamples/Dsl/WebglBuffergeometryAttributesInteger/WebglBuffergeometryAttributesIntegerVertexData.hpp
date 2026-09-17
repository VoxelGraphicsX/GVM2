#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_INTEGER_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_INTEGER_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one r185 triangle vertex with its signed texture-selection attribute. */
struct WebglBuffergeometryAttributesIntegerVertex
{
    float3 position [[Attribute0]];
    float2 texCoord [[Attribute1]];
    int textureIndex [[Attribute2]];
};

#endif
