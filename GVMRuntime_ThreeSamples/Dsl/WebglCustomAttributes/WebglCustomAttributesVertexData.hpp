#ifndef GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_CUSTOM_ATTRIBUTES_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one r185 SphereGeometry vertex and its frame-updated displacement attribute. */
struct WebglCustomAttributesVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 texCoord [[Attribute2]];
    float displacement [[Attribute3]];
};

#endif
