#pragma once
#include "UGL.h"
using namespace UGL;
/** Rejects a compute wrapper that erases the concrete constructor contract. */
inline void invalidInterfaceCompute(ComputeClass<IComputeClass> pipeline) {}
/** Rejects the same erased interface after adding a const qualifier. */
inline void invalidConstInterfaceCompute(ComputeClass<const IComputeClass> pipeline) {}
