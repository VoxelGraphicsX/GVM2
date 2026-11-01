#pragma once
#include "UGL.h"
using namespace UGL;

int destroyedTaskCount = 0;

/// Hides a shared side effect in object destruction.
struct InvalidLifetime
{
    /// Mutates storage outside the owned task input.
    ~InvalidLifetime()
    {
        ++destroyedTaskCount;
    }
};

/// Attempts to invoke an unsafe destructor implicitly at task completion.
int invalidDestructorEffect(int input)
{
    InvalidLifetime lifetime;
    return input;
}

/// Must include implicit destruction when validating reachable task effects.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidDestructorEffect, input);
}
