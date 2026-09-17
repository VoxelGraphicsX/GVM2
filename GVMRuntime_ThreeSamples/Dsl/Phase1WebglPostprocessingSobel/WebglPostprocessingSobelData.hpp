#ifndef GVM_THREE_WEBGL_POSTPROCESSING_SOBEL_DATA_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_SOBEL_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact TorusKnotGeometry position and smooth normal. */
struct WebglPostprocessingSobelVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

#endif
