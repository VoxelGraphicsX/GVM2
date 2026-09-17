#ifndef GVM_THREE_PHASE1_LINES_POINTS_SIMPLE_DATA_HPP
#define GVM_THREE_PHASE1_LINES_POINTS_SIMPLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one CPU-expanded point center and normalized billboard corner. */
struct WebglPointsBillboardsVertex
{
    float4 center [[Attribute0]];
    float4 corner [[Attribute1]];
};

/** Stores camera columns, animated material color, and point-size mode. */
struct WebglPointsBillboardsUniforms
{
    float4 modelViewColumn0;
    float4 modelViewColumn1;
    float4 modelViewColumn2;
    float4 modelViewColumn3;
    float4 projectionColumn0;
    float4 projectionColumn1;
    float4 projectionColumn2;
    float4 projectionColumn3;
    float4 materialColorAndSize;
    float4 attenuationAndFog;
    float4 reserved;
};

#endif
