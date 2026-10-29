#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Passes a scalar where the constructor requires a settings record. */
inline void invalidArgumentCall(Device device, DiagnosticStorage storage)
{
    auto compute = device->createComputeClass<DiagnosticCompute>(storage, DiagnosticSettings{});
    compute->dispatchThreads(storage, 1u, 1, 1, 1);
}
