#pragma once

#include "UGL.h"

using namespace UGL;

/** Stores the packed vertex attribute union for spheres, shadows, and wire edges. */
struct WebglGeometryColorsVertex
{
    float4 position [[Attribute0]];
    float4 normalOrEnd [[Attribute1]];
    float4 color [[Attribute2]];
    float4 uvOrCorner [[Attribute3]];
};

/** Stores one entity camera transform and the directional light parameters. */
struct WebglGeometryColorsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 lightViewAndIntensity;
    float4 viewport;
};

/** Stores the required one-entry RenderSet instance component. */
struct WebglGeometryColorsInstanceData
{
    float4 reserved;
};

/** Selects the solid, shadow, or wireframe material phase. */
struct WebglGeometryColorsMaterialData
{
    float4 baseColorAndPhase;
};
