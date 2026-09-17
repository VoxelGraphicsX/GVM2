#ifndef GVM_THREE_MISC_UV_TESTS_VERTEX_DATA_HPP
#define GVM_THREE_MISC_UV_TESTS_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one screen-space contour or glyph-atlas corner for misc_uv_tests. */
struct MiscUvTestsVertex
{
    float2 pixelPosition [[Attribute0]];
    float2 atlasCoordinate [[Attribute1]];
    float4 colorAndAtlasFlag [[Attribute2]];
    float4 lineSegment [[Attribute3]];
};

#endif
