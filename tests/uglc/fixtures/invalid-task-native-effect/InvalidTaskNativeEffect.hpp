#pragma once
#include "UGL.h"
using namespace UGL;

/// Represents an external function with no registered effect contract or source body.
int unknownNativeOperation(int value);

/// Reaches an opaque external operation through an ordinary task.
int invalidEffect(int input)
{
    return unknownNativeOperation(input);
}

/// Must reject unknown effects instead of treating the task as harmless CPU work.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidEffect, input);
}
