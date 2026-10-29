#ifndef GVM_THREE_WEBGPU_MULTIPLE_CANVAS_DATA_HPP
#define GVM_THREE_WEBGPU_MULTIPLE_CANVAS_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one standalone simple-canvas vertex after CPU viewport composition. */
struct WebgpuMultipleCanvasSimpleVertex
{
    float4 position [[Attribute0]];
    float4 normalAndFlags [[Attribute1]];
    float4 colorAndFlags [[Attribute2]];
};

#endif
