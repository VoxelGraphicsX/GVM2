#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Attempts a view conversion rejected by the existing IsStorageBuffer concept. */
inline void invalidArgumentCall(Device device, DiagnosticStorage storage, DiagnosticUniform uniform)
{
    auto compute = device->createComputeClass<DiagnosticCompute>(storage, DiagnosticSettings{});
    compute->dispatchThreads(uniform, DiagnosticSettings{}, 1, 1, 1);
}
