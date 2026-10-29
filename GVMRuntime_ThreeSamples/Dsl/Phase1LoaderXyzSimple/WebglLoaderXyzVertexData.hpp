#ifndef GVM_THREE_WEBGL_LOADER_XYZ_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_LOADER_XYZ_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one centered XYZ point and one corner of its screen-space square expansion. */
struct WebglLoaderXyzVertex
{
    float3 position [[Attribute0]];
    float2 corner [[Attribute1]];
};

#endif
