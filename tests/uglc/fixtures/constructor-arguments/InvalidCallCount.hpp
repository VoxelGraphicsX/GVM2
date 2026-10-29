#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Omits the mandatory settings argument from an execution call. */
inline void invalidArgumentCall(Device device, DiagnosticStorage storage)
{
    auto compute = device->createComputeClass<DiagnosticCompute>(storage, DiagnosticSettings{});
    compute->dispatchThreads(storage, 1, 1, 1);
}
