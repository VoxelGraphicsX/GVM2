#ifndef GVM_THREE_PHASE1_MODIFIER_TESSELLATION_SIMPLE_DATA_HPP
#define GVM_THREE_PHASE1_MODIFIER_TESSELLATION_SIMPLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one final r185 tessellated text vertex and its face-constant deformation attributes. */
struct WebglModifierTessellationVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 customColor [[Attribute2]];
    float4 displacement [[Attribute3]];
};

/** Stores the fixed camera transform and animated displacement amplitude. */
struct WebglModifierTessellationUniforms
{
    float4 modelViewProjectionColumn0;
    float4 modelViewProjectionColumn1;
    float4 modelViewProjectionColumn2;
    float4 modelViewProjectionColumn3;
    float4 amplitudeAndReserved;
};

#endif
