#ifndef GVM_THREE_TEXTURED_BOX_COMMON_HPP
#define GVM_THREE_TEXTURED_BOX_COMMON_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one indexed BoxGeometry vertex in a textured-box Scene RenderSet. */
struct TexturedBoxVertex
{
    float4 position [[Attribute0]];
    float4 texCoord [[Attribute1]];
};

/** Stores one entity transform and the material index selected by that entity. */
struct TexturedBoxObjectData
{
    float4x4 modelViewProjection;
    uint4 materialAndFlags;
};

/** Stores the tint selected through one entity-local instance component entry. */
struct TexturedBoxInstanceData
{
    float4 tint;
};

/** Stores the common base color selected through one entity material component entry. */
struct TexturedBoxMaterialData
{
    float4 baseColor;
};

/** Carries entity-resolved textured-box values from the vertex stage to the fragment stage. */
struct TexturedBoxVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
    uint2 entityAndMaterial [[Attribute1]];
    float4 instanceTint [[Attribute2]];
};

/** Defines the single-sample RGBA8 and native depth attachments shared by textured-box cases. */
struct TexturedBoxFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear color channel with the exact Three r185 sRGB output transfer constants. */
float texturedBoxLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

#endif
