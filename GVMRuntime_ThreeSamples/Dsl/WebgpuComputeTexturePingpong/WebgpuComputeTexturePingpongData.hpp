#ifndef GVM_THREE_WEBGPU_COMPUTE_TEXTURE_PINGPONG_DATA_HPP
#define GVM_THREE_WEBGPU_COMPUTE_TEXTURE_PINGPONG_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one standalone orthographic plane position and UV pair. */
struct PingpongPlaneVertex
{
    float4 position [[Attribute0]];
    float4 uvAndReserved [[Attribute1]];
};

#endif
