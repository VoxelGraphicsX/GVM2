#ifndef GVM_THREE_WEBGL_POSTPROCESSING_DATA_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one expanded flat-shaded BoxGeometry vertex and its local UV. */
struct WebglPostprocessingRgbHalftoneVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

#endif
