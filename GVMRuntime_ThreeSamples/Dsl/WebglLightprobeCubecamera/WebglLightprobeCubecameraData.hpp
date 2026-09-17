#ifndef GVM_THREE_WEBGL_LIGHTPROBE_CUBECAMERA_DATA_HPP
#define GVM_THREE_WEBGL_LIGHTPROBE_CUBECAMERA_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact r185 LightProbeHelper sphere vertex. */
struct WebglLightprobeCubecameraVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

#endif
