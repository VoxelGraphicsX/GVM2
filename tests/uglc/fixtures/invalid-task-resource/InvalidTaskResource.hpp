#pragma once
#include "UGL.h"
using namespace UGL;

/// Exposes an invalid GPU resource through otherwise ordinary callable state.
class InvalidTask
{
public:
    Buffer<uint, BufferUsage<Storage>> resource;
    /// Attempts to use a resource-bearing object as a CPU task.
    int operator()(int value) const
    {
        return value;
    }
};

/// Must reject transitive GPU state instead of guessing a GPU execution domain.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(InvalidTask{}, input);
}
