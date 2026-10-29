#pragma once
#include "SlotIndirectCalls.hpp"
/** Rejects the unannotated indirect overload after the concrete shader type has been erased. */
inline void invalidErasedIndirect(IComputeClass pipeline, SlotIndirectBuffer commands)
{
    pipeline.dispatchWorkgroupsIndirect(commands);
}
