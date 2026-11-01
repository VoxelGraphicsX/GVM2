#pragma once
#include "UGL.h"
using namespace UGL;

/// Attempts to take an address without retaining a pointer variable.
int invalidAddress(int input)
{
    (void)&input;
    return input;
}

/// Must reject address-of expressions during reachable task effect analysis.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidAddress, input);
}
