#pragma once
#include "UGL.h"
using namespace UGL;

/** Checks both frontend declarations without instantiating an executable Args shader. */
template<typename Unused>
class [[LocalWorkGroupSize(1, 1, 1)]] UninstantiatedConstArgumentInput final : public IComputeClass
{
public:
    /** Provides a nondependent field so each frontend must check its actual qualifier immediately. */
    constructor(uint value [[Args0]]) {}
#ifdef __UGLC_SHADER_ARGS_READONLY__
    static_assert(std::is_const_v<decltype(value)>);
#else
    static_assert(!std::is_const_v<decltype(value)>);
#endif
private:
    /** Leaves the template-dependent shader entry unused in this native compatibility fixture. */
    void compute(uint3 id [[DispatchThreadID]]) { const uint result = value + id.x; }
};

/** Forces native host and shader parsing so the unused template checks both frontend states. */
class [[LocalWorkGroupSize(1, 1, 1)]] ConstArgumentIsolationControl final : public IComputeClass
{
public:
    /** Keeps native compilation on the existing zero-argument pipeline ABI. */
    constructor() {}
private:
    /** Provides the concrete native shader needed by the isolation fixture. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
