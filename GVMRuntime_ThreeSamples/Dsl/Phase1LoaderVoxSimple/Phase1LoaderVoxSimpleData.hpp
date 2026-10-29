#ifndef GVM_THREE_PHASE1_LOADER_VOX_SIMPLE_DATA_HPP
#define GVM_THREE_PHASE1_LOADER_VOX_SIMPLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one flat-shaded greedy VOX mesh vertex. */
struct WebglLoaderVoxVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float3 color [[Attribute2]];
};

/** Stores camera transforms and the two directional-light vectors. */
struct WebglLoaderVoxUniforms
{
    float4 modelViewProjectionColumn0;
    float4 modelViewProjectionColumn1;
    float4 modelViewProjectionColumn2;
    float4 modelViewProjectionColumn3;
    float4 cameraPositionAndScale;
    float4 lightDirection0;
    float4 lightDirection1;
    float4 sampleOffsetAndPadding;
};

#endif
