#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS2_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS2_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one non-instanced corner of a CPU-expanded r185 sorted point sprite. */
struct WebglCustomAttributesPoints2Vertex
{
    float3 position [[Attribute0]];
    float3 customColor [[Attribute1]];
    float2 corner [[Attribute2]];
};

#endif
