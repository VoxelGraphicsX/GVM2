#ifndef UGLC_TEST_CPP_SWIZZLE_CONSTRUCTOR_NO_VEC_CAST_HPP
#define UGLC_TEST_CPP_SWIZZLE_CONSTRUCTOR_NO_VEC_CAST_HPP

#include "UGL.h"

using namespace UGL;

struct CppSwizzleConstructorNoVecCastVertexInput
{
    float4 pos [[Attribute0]];
};

struct CppSwizzleConstructorNoVecCastVertexOutput
{
    float4 pos [[Position]];
};

struct CppSwizzleConstructorNoVecCastFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class CppSwizzleConstructorNoVecCastPass final : public IRenderClass
{
public:
    constructor()
    {
        const float4 floatValue = float4(1.0f, 2.0f, 3.0f, 4.0f);
        const int4 intValue = int4(1, 2, 3, 4);
        const int4 otherIntValue = int4(4, 3, 2, 1);
        const uint2 index = uint2(1u, 2u);
        const uint3 resolution = uint3(4u, 8u, 16u);

        const float3 sameType = float3(floatValue.xyz);
        const float3 crossScalar = float3(intValue.xyz);
        const float2 twoComponent = float2(floatValue.xz);
        const float2 uint2Swizzle = float2(index.xy);
        const float3 uint3Swizzle = float3(resolution.xyz);
        float2 values[2] = {float2(1.0f, 2.0f), float2(3.0f, 4.0f)};
        int cursor = 0;
        const half3 convertedHalf = half3(values[cursor++].xyx);
        const half3 castHalf = (half3)floatValue.rgb;
        const half3 staticHalf = static_cast<half3>(floatValue.rgb);
        const half3 poweredHalf = pow(convertedHalf, half3(half(2.0f)));
        const half rootHalf = sqrt(half(4.0f));
        const float3 weightedFloat = lerp(float3(1.0f), float3(3.0f), half(0.5f));
        const half mixedHalf = lerp(half(1.0f), half(3.0f), 0.5f);

        const float3 binarySwizzle = float3(intValue.xyz - otherIntValue.xyz);

        mScale = sameType.x + crossScalar.y + twoComponent.x + twoComponent.y + uint2Swizzle.x + uint3Swizzle.z + binarySwizzle.x;
    }

private:
    float mScale = 1.0f;

    CppSwizzleConstructorNoVecCastVertexOutput vertex(uint vid [[VertexID]],
                                                      CppSwizzleConstructorNoVecCastVertexInput inputValue [[VertexInput0]])
    {
        CppSwizzleConstructorNoVecCastVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    CppSwizzleConstructorNoVecCastFrameBuffer fragment(CppSwizzleConstructorNoVecCastVertexOutput inputValue)
    {
        CppSwizzleConstructorNoVecCastFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
