#pragma once
#include "UGL.h"
using namespace UGL;

/// Provides a valid function whose address must not become a DSL task value.
int evaluateValue(int input)
{
    return input;
}

/// Must accept static declarations only, not a variable containing a function pointer.
[[Export]] auto buildInvalidGraph(int input)
{
    auto callable = evaluateValue;
    TaskGraph graph;
    return graph.add(callable, input);
}
