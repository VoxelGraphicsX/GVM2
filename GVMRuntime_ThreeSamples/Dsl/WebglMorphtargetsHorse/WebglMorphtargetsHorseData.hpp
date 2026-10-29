#ifndef GVM_THREE_WEBGL_MORPHTARGETS_HORSE_DATA_HPP
#define GVM_THREE_WEBGL_MORPHTARGETS_HORSE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one immutable Horse GLB base vertex and its authored linear color. */
struct WebglMorphtargetsHorseVertex
{
    float3 position [[Attribute0]];
    float3 color [[Attribute1]];
};

/** Stores camera transforms, fifteen weights, and two view-space directional lights. */
struct WebglMorphtargetsHorseUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 morphWeights0;
    float4 morphWeights1;
    float4 morphWeights2;
    float4 morphWeights3;
    float4 lightDirectionIntensity0;
    float4 lightDirectionIntensity1;
    float4 lightColor0;
    float4 lightColor1;
};

#endif
