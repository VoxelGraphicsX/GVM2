#ifndef GVM_THREE_WEBGL_POINTS_WAVES_DATA_HPP
#define GVM_THREE_WEBGL_POINTS_WAVES_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one CPU-expanded point corner together with its immutable grid coordinates. */
struct WebglPointsWavesVertex
{
    float4 positionAndGridX [[Attribute0]];
    float4 cornerAndGridY [[Attribute1]];
};

/** Stores the exact Three camera transforms and current sequential wave phase. */
struct WebglPointsWavesUniforms
{
    float4x4 projectionMatrix;
    float4x4 viewMatrix;
    float4 phaseAndViewport;
    float4 sampleOffsetAndPadding;
};

#endif
