#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Omits the argument prefix through the historical wrapper call operator. */
inline void invalidLegacyCall(Device device, DiagnosticStorage storage)
{
    auto compute = device->createComputeClass<DiagnosticCompute>(storage, DiagnosticSettings{});
    compute(1, 1, 1);
}
