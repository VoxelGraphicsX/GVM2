#pragma once
#include "UGL.h"
using namespace UGL;

/** Verifies that Args attributes qualify shader members without changing constructor parameter types. */
class [[LocalWorkGroupSize(1, 1, 1)]] ConstArgumentFrontend final : public IComputeClass
{
public:
    /** Receives ordinary values as normal creation-time copies. */
    constructor(uint value [[Args0]], float4 values [[Args1]]) { value += 1; }

#ifdef __UGLC_SHADER_ARGS_READONLY__
    static_assert(std::is_const_v<decltype(value)>);
    static_assert(std::is_const_v<decltype(values)>);
#else
    static_assert(!std::is_const_v<decltype(value)>);
    static_assert(!std::is_const_v<decltype(values)>);
#endif

private:
    /** Reads readonly inputs and modifies only invocation-local copies. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = values;
        local[0] = float(value + id.x);
    }
};

/** Preserves an early readonly qualifier through class-template substitution. */
template<typename T>
class [[LocalWorkGroupSize(1, 1, 1)]] ConstArgumentTemplate final : public IComputeClass
{
public:
    /** Keeps a template-dependent constructor type unchanged in the public contract. */
    constructor(T value [[Args0]]) {}
#ifdef __UGLC_SHADER_ARGS_READONLY__
    static_assert(std::is_const_v<decltype(value)>);
#else
    static_assert(!std::is_const_v<decltype(value)>);
#endif
private:
    /** Uses the substituted readonly field from an out-of-line entry definition. */
    void compute(uint3 id [[DispatchThreadID]]);
};

/** Reads a template input after the declaration's readonly type has been instantiated. */
template<typename T>
void ConstArgumentTemplate<T>::compute(uint3 id)
{
    const float result = value[0] + float(id.x);
}

/** Instantiates the template with the same ordinary argument type used at creation and dispatch. */
inline void inspectConstArgumentTemplate(Device device, float4 value)
{
    auto pipeline = device->createComputeClass<ConstArgumentTemplate<float4>>(value);
    pipeline->dispatchThreads(value, 1, 1, 1);
}
