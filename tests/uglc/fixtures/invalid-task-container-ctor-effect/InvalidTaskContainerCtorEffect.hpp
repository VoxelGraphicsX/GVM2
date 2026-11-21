#pragma once
#include "UGL.h"
#include <EASTL/vector.h>
using namespace UGL;

int gHiddenContainerCounter = 0;

/// Mutates shared global state inside a user-defined default constructor invoked by container resize.
struct LeakyRow
{
    int value = 0;

    /// Side-effecting default constructor that must be rejected when invoked via eastl::vector::resize.
    LeakyRow()
    {
        ++gHiddenContainerCounter;
        value = gHiddenContainerCounter;
    }
};

/// Resizes a local owning container whose element default constructor writes shared global state.
int runContainerResize(int count)
{
    eastl::vector<LeakyRow> rows;
    rows.resize(uint(count));
    return rows.empty() ? 0 : rows[0].value;
}

/// Exports the invalid CPU graph entry.
[[Export]] auto graphEntry(int input)
{
    TaskGraph graph;
    return graph.add(runContainerResize, input);
}
