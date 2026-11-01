#pragma once
#include "UGL.h"
using namespace UGL;

/// Exposes shared storage through a member expression instead of a global reference.
struct SharedCounter
{
    inline static int value = 0;
};

/// Attempts to mutate a static member through an owned object expression.
int invalidStaticMember(int input)
{
    SharedCounter counter;
    return input + ++counter.value;
}

/// Must reject shared state even when accessed with ordinary member syntax.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidStaticMember, input);
}
