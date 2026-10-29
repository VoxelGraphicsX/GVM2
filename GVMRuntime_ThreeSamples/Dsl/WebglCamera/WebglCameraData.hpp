#ifndef GVM_THREE_WEBGL_CAMERA_DATA_HPP
#define GVM_THREE_WEBGL_CAMERA_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one CPU-prepared line, point, or background-expansion vertex. */
struct WebglCameraVertex
{
    float4 endpoint0 [[Attribute0]];
    float4 endpoint1 [[Attribute1]];
    float4 colorAndKind [[Attribute2]];
    float4 expansion [[Attribute3]];
    float4 leftClipEndpoint0 [[Attribute4]];
    float4 leftClipEndpoint1 [[Attribute5]];
};

#endif
