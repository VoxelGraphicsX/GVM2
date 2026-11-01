#pragma once
#include "UGL.h"
using namespace UGL;

int sharedTaskCounter = 0;

/// Attempts to mutate global storage from a scheduled task.
int invalidGlobal(int input)
{
    return input + ++sharedTaskCounter;
}

/// Must reject unsnapshotted shared state rather than serialize it implicitly.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidGlobal, input);
}
