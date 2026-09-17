#ifndef GVM_THREE_WEBGPU_LIGHTPROBE_DATA_HPP
#define GVM_THREE_WEBGPU_LIGHTPROBE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact r185 material or LightProbeHelper sphere vertex. */
struct WebgpuLightprobeVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

#endif
