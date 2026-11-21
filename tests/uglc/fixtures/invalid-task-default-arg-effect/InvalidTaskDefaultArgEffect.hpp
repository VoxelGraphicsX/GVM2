#pragma once
#include "UGL.h"
using namespace UGL;

int gDefaultArgCounter = 0;

/// Helper mutating global state used inside an omitted default argument expression.
int nextDefaultBias()
{
    ++gDefaultArgCounter;
    return gDefaultArgCounter;
}

/// Accepts a default argument whose expression mutates shared global storage when omitted.
int computeWithDefaultBias(int x, int bias = nextDefaultBias())
{
    return x + bias;
}

/// Calls computeWithDefaultBias without supplying the second argument.
int invokeOmittedDefault(int x)
{
    return computeWithDefaultBias(x);
}

/// Exports the invalid CPU graph entry.
[[Export]] auto graphEntry(int input)
{
    TaskGraph graph;
    return graph.add(invokeOmittedDefault, input);
}
