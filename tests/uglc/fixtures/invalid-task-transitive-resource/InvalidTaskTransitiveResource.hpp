#pragma once
#include "UGL.h"
using namespace UGL;

/// Hides a GPU handle in the local state of a reachable helper.
int hiddenResource(int input)
{
    Buffer<uint, BufferUsage<Storage>> resource;
    return input;
}

/// Appears CPU-only at its signature but has a resource-bearing reachable effect.
int invalidTransitiveEffect(int input)
{
    return hiddenResource(input);
}

/// Must reject a transitive resource even though the leaf signature contains only integers.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidTransitiveEffect, input);
}
