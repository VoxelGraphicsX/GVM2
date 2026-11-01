#pragma once
#include "UGL.h"
using namespace UGL;

/// Deliberately declares a reference outside the DSL value contract.
int invalidReference(const int &input)
{
    return input;
}

/// Must reject a referenced task parameter even when native C++ could invoke it.
[[Export]] auto buildInvalidGraph(int input)
{
    TaskGraph graph;
    return graph.add(invalidReference, input);
}
