#ifndef GVM_THREE_WEBGL_MATERIALS_NORMALMAP_OBJECT_SPACE_DATA_HPP
#define GVM_THREE_WEBGL_MATERIALS_NORMALMAP_OBJECT_SPACE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Nefertiti position and texture coordinate after GLB decoding. */
struct WebglMaterialsNormalmapObjectSpaceVertex
{
    float3 position [[Attribute0]];
    float2 textureCoordinate [[Attribute1]];
};

/** Stores the fixed camera transforms and the r185 Standard lighting constants. */
struct WebglMaterialsNormalmapObjectSpaceUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 ambientPointRoughness;
    float4 sampleOffsetAndPadding;
};

#endif
