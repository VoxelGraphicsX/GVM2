#pragma once
#include "UGL.h"
using namespace UGL;

/// Supplies an ordinary record with one deferred field.
struct Output
{
    uint count;
};

/// Produces an owned CPU value for the projection diagnostic.
Output produce(uint count)
{
    return {count};
}

/// Rejects assigning through a deferred field rather than connecting it as a graph input.
[[Export]] auto invalidProjection(uint count)
{
    TaskGraph graph;
    auto first = graph.add(produce, count);
    auto second = graph.add(produce, count);
    first.count = second.count;
    return first.count;
}
