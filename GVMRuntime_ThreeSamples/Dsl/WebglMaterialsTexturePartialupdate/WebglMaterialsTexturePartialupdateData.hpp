#ifndef GVM_THREE_WEBGL_MATERIALS_TEXTURE_PARTIALUPDATE_DATA_HPP
#define GVM_THREE_WEBGL_MATERIALS_TEXTURE_PARTIALUPDATE_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one indexed Three r185 PlaneGeometry vertex and its authored UV. */
struct WebglMaterialsTexturePartialupdateVertex
{
    float3 position [[Attribute0]];
    float2 texCoord [[Attribute1]];
};

/** Stores one deterministic 32x32 destination origin for the partial-update compute pass. */
struct WebglMaterialsTexturePartialupdatePatch
{
    uint4 destinationAndSourceRow;
};

#endif
