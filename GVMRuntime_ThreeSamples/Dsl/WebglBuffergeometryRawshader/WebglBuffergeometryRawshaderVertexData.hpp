#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_RAWSHADER_VERTEX_DATA_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_RAWSHADER_VERTEX_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one position and one packed normalized Uint8 RGBA color. */
struct WebglBuffergeometryRawshaderVertex
{
    float3 position [[Attribute0]];
    uint colorRgba8 [[Attribute1]];
};

#endif
