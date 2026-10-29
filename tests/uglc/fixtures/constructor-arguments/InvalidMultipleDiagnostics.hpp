#pragma once
#include "UGL.h"
using namespace UGL;

/** Mixes an Args parameter with an unannotated creation-only parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] MultipleDiagnosticMissing final : public IComputeClass
{
public:
    /** Intentionally omits the required second argument annotation. */
    constructor(uint first [[Args0]], uint second) {}
private:
    /** Supplies an entry body for the missing-annotation diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};

/** Declares multiple argument positions on one constructor parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] MultipleDiagnosticDuplicate final : public IComputeClass
{
public:
    /** Intentionally attaches two Args annotations to one value. */
    constructor(uint value [[Args0]] [[Args1]]) {}
private:
    /** Supplies an entry body for the duplicate-annotation diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};

/** Supplies one valid Args class so its invalid call site reports a third diagnostic. */
class [[LocalWorkGroupSize(1, 1, 1)]] MultipleDiagnosticCaller final : public IComputeClass
{
public:
    /** Receives one scalar argument for the execution prefix. */
    constructor(uint value [[Args0]]) {}
private:
    /** Supplies an entry body for the call-count diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};

/** Omits the mandatory value argument from an otherwise valid execution call. */
inline void multipleDiagnosticCall(Device device)
{
    auto compute = device->createComputeClass<MultipleDiagnosticCaller>(7u);
    compute->dispatchThreads(1, 1, 1);
}
