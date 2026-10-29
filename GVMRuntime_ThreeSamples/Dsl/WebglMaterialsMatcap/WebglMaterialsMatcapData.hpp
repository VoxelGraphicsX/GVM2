#ifndef GVM_THREE_WEBGL_MATERIALS_MATCAP_DATA_HPP
#define GVM_THREE_WEBGL_MATERIALS_MATCAP_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Lee Perry Smith vertex used by the dedicated matcap sample. */
struct WebglMaterialsMatcapVertex
{
    float3 position [[Attribute0]];
    float3 normal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float3 positionDerivativeU [[Attribute3]];
    float3 positionDerivativeV [[Attribute4]];
};

/** Stores the fixed camera, material color, exposure, and selected matcap state. */
struct WebglMaterialsMatcapUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
    float4 colorExposureCustom;
};

#endif
