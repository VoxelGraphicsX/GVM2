#pragma once
#include "ConstructorArguments.hpp"

/** Rejects an Args dispatch whose buffer selects the existing constrained indirect overload. */
inline void invalidArgumentDispatchIndirectPrefix(ComputeClass<ArgumentCompute> compute, ArgumentIndirect commands)
{
    compute->dispatchWorkgroupsIndirect(commands);
}
