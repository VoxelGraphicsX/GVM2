#pragma once
#include "ConstructorArguments.hpp"

/** Rejects GPU-counted draws that omit the complete constructor prefix. */
inline void invalidIndirectCountPrefix(RenderClass<ArgumentMesh> mesh, ArgumentIndirect commands)
{
    mesh->drawIndirectCount(commands, commands, 3u);
}
