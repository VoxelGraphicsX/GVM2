#pragma once
#include "SlotIndirectCalls.hpp"

/** Rejects a Slot indirect call that omits its stride control. */
inline void invalidSlotIndirectCount(RenderClass<SlotIndirectRender> render, SlotIndirectBuffer commands)
{
    render->drawIndirect(commands, 1u);
}
