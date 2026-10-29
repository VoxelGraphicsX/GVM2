#pragma once
#include "ConstructorArguments.hpp"
/** Uses a storage buffer without indirect usage as an operation control. */
inline void invalidControlCall(Device device, ArgumentStorage storage)
{
    auto compute = device->createComputeClass<ArgumentCompute>(storage, ArgumentCamera{});
    compute->dispatchWorkgroupsIndirect(storage, ArgumentCamera{}, storage);
}
