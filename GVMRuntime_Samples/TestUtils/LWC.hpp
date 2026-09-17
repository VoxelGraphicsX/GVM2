#pragma once

#include "UGL.h"
using namespace UGL;
struct LWCData
{
    int4 intData;
    float4 floatData;
};

LWCData LWCFromFloat3(IN float3 f3)
{
    LWCData result;
    result.intData = int4(0);
    result.floatData = float4(0.0f);
    float3 intPart;
    float3 fracPart;
    fracPart = modf(f3, intPart);
    result.floatData.x = fracPart.x;
    result.floatData.y = fracPart.y;
    result.floatData.z = fracPart.z;

    result.intData.x = intPart.x;
    result.intData.y = intPart.y;
    result.intData.z = intPart.z;
    return result;
}

LWCData LWCFromInt3(IN int3 i3)
{
    LWCData result;
    result.intData = int4(0);
    result.intData.xyz = i3;
    result.floatData = float4(0.0f);
    return result;
}

float3 LWCToFloat3(IN LWCData lwc)
{
    return float3((int3)lwc.intData.xyz) + float3(lwc.floatData.xyz);
}

LWCData LWCFromDouble3(IN double3 d3)
{
    LWCData result;
    result.intData = int4(0);
    result.floatData = float4(0.0f);
    double3 intPart;
    double3 fracPart;
    fracPart = modf(d3, intPart);
    result.floatData.x = fracPart.x;
    result.floatData.y = fracPart.y;
    result.floatData.z = fracPart.z;

    result.intData.x = intPart.x;
    result.intData.y = intPart.y;
    result.intData.z = intPart.z;
    return result;
}

float3 LWCDiff(IN LWCData lwc1, IN LWCData lwc2)
{
    float3 result;
    result = float3((int3)(lwc1.intData.xyz - lwc2.intData.xyz));
    result += float3(lwc1.floatData.xyz) - float3(lwc2.floatData.xyz);
    return result;
}

/* LWCData LWCRegularization(IN LWCData lwc)
{
    return lwc;
     LWCData result;
    float3 intPart;
    float3 fracPart;
    fracPart = modf(lwc.floatData, intPart);
    result.intData = lwc.intData + int3(intPart);
    result.floatData = fracPart;
    return result;
}
*/

/* LWCData LWCAddWithFloat3(IN LWCData lwc, IN float3 f3)
{
    LWCData result = LWCFromFloat3(f3);

    result.intData += lwc.intData;
    result.floatData += lwc.floatData;
    result = LWCRegularization(result);
    return result;
}

LWCData LWCSubWithFloat3(IN LWCData lwc, IN float3 f3)
{
    return LWCAddWithFloat3(lwc, -f3);
} */
/*
LWCData LWCMulWithFloat3(IN LWCData lwc, IN float3 f3)
{
    LWCData result;
    // float3 intPart;
    // float3 fracPart;
    // fracPart = modf(f3, intPart);
    result.intData = lwc.intData * int3((f3));
    result.floatData = lwc.floatData * f3;
    result = LWCRegularization(result);
    return result;
}

LWCData LWCDivWithFloat3Bad(IN LWCData lwc, IN float3 f3)
{
    LWCData result;
    result.intData = int3(0);
    result.floatData = float3(lwc.intData) / (f3);
    result.floatData += lwc.floatData / f3;
    result = LWCRegularization(result);
    return result;
} */

/* LWCData LWCAddWithInt3(IN LWCData lwc, IN int3 i3)
{
    LWCData result;
    result.intData = lwc.intData + i3;
    result.floatData = lwc.floatData;
    result = LWCRegularization(result);
    return result;
}

LWCData LWCSubWithInt3(IN LWCData lwc, IN int3 i3)
{
    return LWCAddWithInt3(lwc, -i3);
}

LWCData LWCMulWithInt3(IN LWCData lwc, IN int3 i3)
{
    LWCData result;
    result.intData = lwc.intData * i3;
    result.floatData = lwc.floatData * float3(i3);
    result = LWCRegularization(result);
    return result;
}

LWCData LWCDivWithInt3(IN LWCData lwc, IN int3 i3)
{
    LWCData result;
    result.intData = lwc.intData / i3;
    result.floatData = lwc.floatData / float3(i3);
    result = LWCRegularization(result);
    return result;
} */
