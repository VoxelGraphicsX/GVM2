#ifndef GVM_THREE_WEBGL_POSTPROCESSING_MASKING_DATA_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_MASKING_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one local-space position for either masking mesh. */
struct WebglPostprocessingMaskingVertex
{
    float4 position [[Attribute0]];
};

/** Stores the standalone torus model-view-projection matrix. */
struct WebglPostprocessingMaskingTorusUniforms
{
    float4x4 modelViewProjection;
};

#endif
