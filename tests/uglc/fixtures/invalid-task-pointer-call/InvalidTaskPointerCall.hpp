#pragma once
#include "UGL.h"
#include <EASTL/vector.h>
using namespace UGL;

/// Attempts to observe pointer values without declaring a pointer variable.
bool invalidPointerCall(eastl::vector<int> input)
{
    return input.data() == input.data();
}

/// Must reject raw container pointers as part of the DSL value contract.
[[Export]] auto buildInvalidGraph(eastl::vector<int> input)
{
    TaskGraph graph;
    return graph.add(invalidPointerCall, input);
}
