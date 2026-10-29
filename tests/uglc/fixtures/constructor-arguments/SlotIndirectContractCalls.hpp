#pragma once
#include "SlotIndirectCalls.hpp"

/** Checks Slot indirect dispatch overloads during frontend contract analysis. */
inline void validateSlotIndirectDispatchCalls(ComputeClass<SlotIndirectCompute> compute,
                                              SlotIndirectBuffer commands, SlotIndirectRange range)
{
    compute->dispatchWorkgroupsIndirect(commands);
    compute->dispatchWorkgroupsIndirect(range);
}
