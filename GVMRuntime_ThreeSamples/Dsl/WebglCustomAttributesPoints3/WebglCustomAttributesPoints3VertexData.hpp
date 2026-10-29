#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS3_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_POINTS3_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one non-instanced corner with WebGL's missing color alpha padded to one. */
struct WebglCustomAttributesPoints3Vertex
{
    float3 position [[Attribute0]];
    float4 customColor [[Attribute1]];
    float2 corner [[Attribute2]];
};

#endif
