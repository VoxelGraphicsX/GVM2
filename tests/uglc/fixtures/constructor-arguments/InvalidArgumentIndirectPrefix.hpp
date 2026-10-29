#pragma once
#include "ConstructorArguments.hpp"

/** Rejects an Args draw whose controls select the existing constrained indirect overload. */
inline void invalidArgumentIndirectPrefix(RenderClass<ArgumentMesh> mesh, ArgumentIndirect commands)
{
    mesh->drawIndirect(commands, 1u, 16u);
}
