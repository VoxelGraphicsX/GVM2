#pragma once
#include "SlotIndirectCalls.hpp"

/** Rejects a non-indirect buffer even when the unconstrained placeholder is selected. */
inline void invalidSlotIndirectBuffer(RenderClass<SlotIndirectRender> render,
                                      Buffer<uint, BufferUsage<Storage>> commands)
{
    render->drawIndirect(commands, 1u, sizeof(uint) * 4);
}
