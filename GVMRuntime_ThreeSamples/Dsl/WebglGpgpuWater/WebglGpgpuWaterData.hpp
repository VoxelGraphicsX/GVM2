#ifndef GVM_THREE_WEBGL_GPGPU_WATER_DATA_HPP
#define GVM_THREE_WEBGL_GPGPU_WATER_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the exact controls consumed by one r185 heightmap fragment step. */
struct WebglGpgpuWaterHeightUniforms
{
    float4 mousePositionSizeAndDepth;
    float4 viscosityAndReserved;
};

/** Stores output extent and the fixed r185 camera ray basis for screen passes. */
struct WebglGpgpuWaterScreenUniforms
{
    float4 outputSizeTanHalfFovAndExposure;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
};

/** Stores the fixed r185 directional-light basis and interactive shadow controls. */
struct WebglGpgpuWaterShadowUniforms
{
    float4 lightRight;
    float4 lightUp;
    float4 lightBackwardAndEnabled;
    float4 lightPositionAndBias;
};

#endif
