#ifndef GVM_THREE_WEBGL_GEOMETRY_MINECRAFT_DATA_HPP
#define GVM_THREE_WEBGL_GEOMETRY_MINECRAFT_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one vertex from the CPU-merged r185 Minecraft terrain mesh. */
struct WebglGeometryMinecraftVertex
{
    float3 position [[Attribute0]];
    float2 texCoord [[Attribute1]];
};

/** Stores the fixed perspective projection and current FirstPersonControls view. */
struct WebglGeometryMinecraftUniforms
{
    float4x4 projectionMatrix;
    float4x4 viewMatrix;
    float4 samplePositionAndReserved;
};

#endif
