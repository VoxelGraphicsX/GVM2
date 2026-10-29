#ifndef GVM_THREE_WEBGL_GPGPU_PROTOPLANET_DATA_HPP
#define GVM_THREE_WEBGL_GPGPU_PROTOPLANET_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Carries the dynamic gravity and density shared by the velocity and draw passes. */
struct WebglGpgpuProtoplanetSimulationUniforms
{
    float4 gravityDensityAndReserved;
};

/** Carries the exact r185 camera transforms and apparent-size constants. */
struct WebglGpgpuProtoplanetRenderUniforms
{
    float4x4 projectionMatrix;
    float4x4 viewMatrix;
    float4 cameraDensityAndViewport;
};

#endif
