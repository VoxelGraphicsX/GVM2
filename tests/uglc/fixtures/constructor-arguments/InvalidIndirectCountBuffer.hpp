#pragma once
#include "ConstructorArguments.hpp"

/** Rejects counters whose buffer type lacks indirect usage even when the command buffer is valid. */
inline void invalidIndirectCountBuffer(RenderClass<ArgumentMesh> mesh, ArgumentStorage storage,
    ArgumentIndirect commands, ArgumentCamera camera, ArgumentMaterial material)
{
    mesh->drawIndirectCount(storage, camera, material, UnusedArguments<int>{}, commands, storage, 3u);
}
