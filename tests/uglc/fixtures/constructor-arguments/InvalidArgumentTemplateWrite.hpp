#pragma once
#include "UGL.h"
using namespace UGL;

/** Defers an invalid input write until Clang instantiates the concrete shader entry. */
template<typename T>
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentTemplateWrite final : public IComputeClass
{
public:
    /** Retains the readonly qualifier when the dependent field type is substituted. */
    constructor(T values [[Args0]]) {}
private:
    /** Requires the instantiated vector's const element access to reject the write. */
    void compute(uint3 id [[DispatchThreadID]]) { values[0] = float(id.x); }
};

/** Supplies the specialization that semantic preparation must analyze. */
inline void instantiateInvalidArgumentTemplate(Device device, float4 value)
{
    auto pipeline = device->createComputeClass<InvalidArgumentTemplateWrite<float4>>(value);
    pipeline->dispatchThreads(value, 1, 1, 1);
}
