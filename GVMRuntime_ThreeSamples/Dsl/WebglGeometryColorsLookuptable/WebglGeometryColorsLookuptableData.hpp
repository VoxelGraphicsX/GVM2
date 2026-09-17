#ifndef GVM_THREE_WEBGL_GEOMETRY_COLORS_LOOKUPTABLE_DATA_HPP
#define GVM_THREE_WEBGL_GEOMETRY_COLORS_LOOKUPTABLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one centered pressure-mesh vertex with its CPU-evaluated LUT color. */
struct WebglGeometryColorsLookuptableVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float3 color [[Attribute2]];
};

/** Stores fixed camera matrices and the selected r185 color-map ordinal. */
struct WebglGeometryColorsLookuptableUniforms
{
    float4x4 modelView;
    float4x4 modelViewProjection;
    float4 lightAndMap;
};

#endif
