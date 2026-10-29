#ifndef GVM_THREE_PHASE1_LOADER_TEXTURE_EXR_SIMPLE_DATA_HPP
#define GVM_THREE_PHASE1_LOADER_TEXTURE_EXR_SIMPLE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one vertex of the canonical EXR display plane. */
struct WebglLoaderTextureExrVertex
{
    float2 position [[Attribute0]];
    float2 textureCoordinate [[Attribute1]];
};

/** Stores the selected deterministic Reinhard exposure. */
struct WebglLoaderTextureExrUniforms
{
    float4 exposureAndPadding;
};

#endif
