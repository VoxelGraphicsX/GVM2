#ifndef GVM_THREE_PHASE1_MODIFIER_EDGE_SPLIT_SIMPLE_DATA_HPP
#define GVM_THREE_PHASE1_MODIFIER_EDGE_SPLIT_SIMPLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one indexed Cerberus vertex after the selected r185 EdgeSplit operation. */
struct WebglModifierEdgeSplitVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
};

/** Stores the fixed camera transform and selected GUI material state. */
struct WebglModifierEdgeSplitUniforms
{
    float4 viewProjectionColumn0;
    float4 viewProjectionColumn1;
    float4 viewProjectionColumn2;
    float4 viewProjectionColumn3;
    float4 materialFlagsAndPadding;
};

#endif
