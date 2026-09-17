#ifndef GVM_THREE_WEBGL_SHADER_LAVA_DATA_HPP
#define GVM_THREE_WEBGL_SHADER_LAVA_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one indexed r185 TorusGeometry vertex. */
struct WebglShaderLavaVertex
{
    float3 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Stores fixed-frame transforms and complete lava material parameters. */
struct WebglShaderLavaUniforms
{
    float4x4 modelViewProjection;
    float4 timeFogAndStrength;
};

#endif
