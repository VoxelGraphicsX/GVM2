#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Writes a local value through an explicit output reference. */
inline void writeLocalArgumentValue(uint &value OUT) { value = 7; }

/** Updates a local value through an explicit input-output reference. */
inline void updateLocalArgumentValue(uint &value INOUT) { value += 1; }

/** Preserves value-spelled output parameters alongside the explicit reference spelling. */
inline void writeLocalArgumentValueByValue(uint value OUT) { value = 11; }

/** Preserves legal output modes and writable resource contents for an Args compute class. */
class [[LocalWorkGroupSize(1, 1, 1)]] ConstructorArgumentOutputModes final : public IComputeClass
{
public:
    /** Requires one writable resource and one ordinary input value. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], uint value [[Args1]]) {}
private:
    /** Copies input into local storage before applying output modes and writing resource contents. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        uint local = value;
        writeLocalArgumentValue(local);
        updateLocalArgumentValue(local);
        writeLocalArgumentValueByValue(local);
        output[id.x] = local;
    }
};
