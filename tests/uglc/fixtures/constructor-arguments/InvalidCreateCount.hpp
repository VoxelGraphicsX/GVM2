#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Omits the settings argument that the existing typed constructor requires at creation. */
inline void invalidCreateCall(Device device, DiagnosticStorage storage)
{
    auto compute = device->createComputeClass<DiagnosticCompute>(storage);
}
