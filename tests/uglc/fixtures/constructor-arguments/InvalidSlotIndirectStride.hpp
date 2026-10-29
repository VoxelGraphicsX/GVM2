#pragma once
#include "SlotIndirectCalls.hpp"

/** Rejects a Slot stride that cannot initialize the existing uint32_t control. */
inline void invalidSlotIndirectStride(RenderClass<SlotIndirectRender> render, SlotIndirectBuffer commands)
{
    render->drawIndirect(commands, 1u, float4(1.0f));
}
