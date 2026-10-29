#pragma once
#include "UGL.h"
using namespace UGL;

/** Contains a mutable field that would bypass ordinary C++ const protection. */
struct MutableArgumentSettings { mutable uint value; };

/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentMutable final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(MutableArgumentSettings settings [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { settings.value = id.x; }
};
