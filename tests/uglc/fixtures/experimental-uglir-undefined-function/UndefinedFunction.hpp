#pragma once
#include "UGL.h"
using namespace UGL;

/** Declares an unavailable shader function that must be rejected before either backend emits a payload. */
uint unavailableValue(uint input);

/** Binds output for the unresolved shader function regression. */
struct UndefinedFunctionBindings final : public IBindGroup
{
    /** Creates the storage binding used by the shader. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]]) {}
};

/** Exercises an ordinary shader call with a declaration but no definition. */
class [[LocalWorkGroupSize(1, 1, 1)]] UndefinedFunctionPass final : public IComputeClass
{
public:
    /** Binds the unresolved-call regression resources. */
    constructor(BindGroup<UndefinedFunctionBindings> bindings [[Slot0]]) {}
private:
    /** Calls the missing function with runtime input so the call cannot be constant-folded. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindings->output[threadID.x] = unavailableValue(threadID.x);
    }
};
