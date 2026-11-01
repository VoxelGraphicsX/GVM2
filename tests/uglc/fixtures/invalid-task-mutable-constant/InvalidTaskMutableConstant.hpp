#pragma once
#include "UGL.h"
using namespace UGL;

/// Declares writable storage inside an otherwise constant object.
struct MutableConstant
{
    mutable int count = 0;
};

constexpr MutableConstant sharedConstant;

/// Attempts to mutate a subobject whose containing variable is constexpr.
int invalidMutableConstant(int input)
{
    return input + ++sharedConstant.count;
}

/// Must reject constexpr storage that still permits cross-task mutation.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidMutableConstant, input);
}
