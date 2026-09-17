#pragma once

#include "UGL.h"
using namespace UGL;

const int numLWCFloatBits = 8;
struct LWCData
{
    int64_t intX;
    int64_t intY;
    int64_t intZ;
    int64_t pad;
};

namespace LWC
{
    namespace Private
    {
        int64_t GetIntValue(int64_t value)
        {
            return value >> numLWCFloatBits;
        }
        float GetFloatValue(int64_t value)
        {
            return (value & ((1 << numLWCFloatBits) - 1)) / (float)(1 << numLWCFloatBits);
        }

        LWCData GetIntValue3(LWCData lwc)
        {
            LWCData result;
            result.intX = GetIntValue(lwc.intX);
            result.intY = GetIntValue(lwc.intY);
            result.intZ = GetIntValue(lwc.intZ);
            return result;
        }

        float3 GetFloatValue3(LWCData lwc)
        {
            float3 result;
            result.x = GetFloatValue(lwc.intX);
            result.y = GetFloatValue(lwc.intY);
            result.z = GetFloatValue(lwc.intZ);
            return result;
        }

        LWCData SetValue(LWCData intPart, float3 floatPart)
        {
            LWCData result;

            float3 regularizedFloatPart;
            float3 regularizedIntPart;
            regularizedFloatPart = modf(floatPart, regularizedIntPart);

            result.intX = intPart.intX + int64_t(regularizedIntPart.x);
            result.intY = intPart.intY + int64_t(regularizedIntPart.y);
            result.intZ = intPart.intZ + int64_t(regularizedIntPart.z);
            result.intX = (result.intX << numLWCFloatBits) + (int64_t)(regularizedFloatPart.x * (1 << numLWCFloatBits));
            result.intY = (result.intY << numLWCFloatBits) + (int64_t)(regularizedFloatPart.y * (1 << numLWCFloatBits));
            result.intZ = (result.intZ << numLWCFloatBits) + (int64_t)(regularizedFloatPart.z * (1 << numLWCFloatBits));
            return result;
        }
    }

    float3 Minus(const LWCData &lwc1, const LWCData &lwc2)
    {
        float3 result;

        LWCData intPart1 = Private::GetIntValue3(lwc1);
        LWCData intPart2 = Private::GetIntValue3(lwc2);

        LWCData intPartResult;
        intPartResult.intX = intPart1.intX - intPart2.intX;
        intPartResult.intY = intPart1.intY - intPart2.intY;
        intPartResult.intZ = intPart1.intZ - intPart2.intZ;

        float3 floatPart1 = Private::GetFloatValue3(lwc1);
        float3 floatPart2 = Private::GetFloatValue3(lwc2);

        LWCData lwcResult = Private::SetValue(intPartResult, floatPart1 - floatPart2);

        result = Private::GetFloatValue3(lwcResult);
        result.x += float(lwcResult.intX);
        result.y += float(lwcResult.intY);
        result.z += float(lwcResult.intZ);

        return result;
    }

    LWCData ValueFromFloat3(const float3 &f3)
    {
        LWCData result;
        float3 intPart;
        float3 fracPart;
        fracPart = modf(f3, intPart);

        result.intX = int64_t(intPart.x);
        result.intY = int64_t(intPart.y);
        result.intZ = int64_t(intPart.z);

        result = Private::SetValue(result, fracPart);
        return result;
    }

    LWCData ValueFromDouble3(const double3 &d3)
    {
        LWCData result;
        double3 intPart;
        double3 fracPart;
        fracPart = modf(d3, intPart);
        result.intX = int64_t(intPart.x);
        result.intY = int64_t(intPart.y);
        result.intZ = int64_t(intPart.z);

        float3 fracPartFloat;
        fracPartFloat.x = float(fracPart.x);
        fracPartFloat.y = float(fracPart.y);
        fracPartFloat.z = float(fracPart.z);

        result = Private::SetValue(result, fracPart);
        return result;
    }
} // namespace LWC