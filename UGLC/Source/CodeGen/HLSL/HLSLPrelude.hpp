#pragma once

#include <string>

namespace UGLC::CodeGen::HLSL
{
    inline std::string MakeHLSLPreludeSource()
    {
        return R"UGL(
uint atomicLoad(uint atom)
{
    return atom;
}

int atomicLoad(int atom)
{
    return atom;
}

uint atomicAdd(inout uint atom, uint value)
{
    uint originalValue;
    InterlockedAdd(atom, value, originalValue);
    return originalValue;
}

int atomicAdd(inout int atom, int value)
{
    int originalValue;
    InterlockedAdd(atom, value, originalValue);
    return originalValue;
}

uint atomicOr(inout uint atom, uint value)
{
    uint originalValue;
    InterlockedOr(atom, value, originalValue);
    return originalValue;
}

int atomicOr(inout int atom, int value)
{
    int originalValue;
    InterlockedOr(atom, value, originalValue);
    return originalValue;
}

uint atomicAnd(inout uint atom, uint value)
{
    uint originalValue;
    InterlockedAnd(atom, value, originalValue);
    return originalValue;
}

int atomicAnd(inout int atom, int value)
{
    int originalValue;
    InterlockedAnd(atom, value, originalValue);
    return originalValue;
}

void atomicStore(inout uint atom, uint value)
{
    uint ignored;
    InterlockedExchange(atom, value, ignored);
}

void atomicStore(inout int atom, int value)
{
    int ignored;
    InterlockedExchange(atom, value, ignored);
}

void atomicCompareExchange(inout uint atom, uint compare, uint value, out uint originalValue)
{
    InterlockedCompareExchange(atom, compare, value, originalValue);
}

void atomicCompareExchange(inout int atom, int compare, int value, out int originalValue)
{
    InterlockedCompareExchange(atom, compare, value, originalValue);
}

void atomicMax(inout uint atom, uint value)
{
    uint ignored;
    InterlockedMax(atom, value, ignored);
}

void atomicMax(inout int atom, int value)
{
    int ignored;
    InterlockedMax(atom, value, ignored);
}

void atomicMin(inout uint atom, uint value)
{
    uint ignored;
    InterlockedMin(atom, value, ignored);
}

void atomicMin(inout int atom, int value)
{
    int ignored;
    InterlockedMin(atom, value, ignored);
}

struct UGL_RenderEntityInfo_
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint globalInstanceBase;
    uint vertexCount;
    uint entityVersion;
    uint cmdParamsOffset;
};
)UGL";
    }
} // namespace UGLC::CodeGen::HLSL
