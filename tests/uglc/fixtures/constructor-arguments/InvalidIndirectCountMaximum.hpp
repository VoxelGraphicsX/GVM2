#pragma once
#include "ConstructorArguments.hpp"

/** Requires the draw capacity to initialize an unsigned integer through normal C++ conversions. */
inline void invalidIndirectCountMaximum(RenderClass<ArgumentMesh> mesh, ArgumentStorage storage,
    ArgumentIndirect commands, ArgumentCamera camera, ArgumentMaterial material)
{
    mesh->drawIndirectCount(storage, camera, material, UnusedArguments<int>{}, commands, commands, camera);
}
