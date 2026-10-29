#pragma once
#include "ConstructorArguments.hpp"
/** Passes a raw integer instead of the constructor-independent IndexFormat enum control. */
inline void invalidIndexFormatCall(Device device, ArgumentStorage storage, ArgumentIndex index,
                                   ArgumentCamera camera, ArgumentMaterial material)
{
    auto mesh = device->createRenderClass<ArgumentMesh>(storage, camera, material, UnusedArguments<int>{});
    mesh->drawIndexed(storage, camera, material, UnusedArguments<int>{}, index, 1u, 3, 1, 0, 0, 0);
}
