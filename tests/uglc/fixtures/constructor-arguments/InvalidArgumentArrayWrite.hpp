#pragma once
#include "UGL.h"
using namespace UGL;
using ArgumentArray = std::array<uint, 2>;


/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentArrayWrite final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(ArgumentArray values [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { values[0] = id.x; }
};
