#pragma once
#include "UGL.h"
using namespace UGL;

/** Binds writable storage for an installed SDK's compute readback check. */
struct InstalledStorage final : public IBindGroup
{
    /** Creates a writable binding for the four output values. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]]) {}
};

/** Writes a deterministic sequence through the installed generated-code runtime interface. */
class [[LocalWorkGroupSize(1, 1, 1)]] InstalledCompute final : public IComputeClass
{
public:
    /** Creates a pass that writes into the supplied storage binding. */
    constructor(BindGroup<InstalledStorage> storage [[Slot0]]) {}
private:
    /** Writes one independently verifiable value per dispatched thread. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        storage->output[threadID.x] = 37u + threadID.x;
    }
};
