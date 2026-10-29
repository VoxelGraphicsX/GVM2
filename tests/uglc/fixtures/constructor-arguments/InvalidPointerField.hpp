#pragma once
#include "UGL.h"
using namespace UGL;
/** Hides a host pointer in an unused concrete template argument. */
template <typename T> struct InvalidNestedArgument { T *owner; };
/** Requires recursive completion and validation of an unused constructor parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidPointerField final : public IComputeClass
{
public:
    /** Declares an unsupported pointer field through a concrete template specialization. */
    constructor(InvalidNestedArgument<uint> unused [[Args0]]) {}
private:
    /** Does not use the parameter, which must still be rejected by contract preparation. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
