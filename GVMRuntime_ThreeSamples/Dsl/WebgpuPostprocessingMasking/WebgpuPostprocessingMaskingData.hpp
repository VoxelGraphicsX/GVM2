#ifndef GVM_THREE_WEBGPU_POSTPROCESSING_MASKING_DATA_HPP
#define GVM_THREE_WEBGPU_POSTPROCESSING_MASKING_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one local-space position for either masking mesh. */
struct WebgpuPostprocessingMaskingVertex
{
    float4 position [[Attribute0]];
};

/** Stores the standalone torus model-view-projection matrix. */
struct WebgpuPostprocessingMaskingTorusUniforms
{
    float4x4 modelViewProjection;
};

#endif
