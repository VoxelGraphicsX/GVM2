#pragma once
#include "UGL.h"
using namespace UGL;

/// Rejects an ordinary CPU callable whose nested input contains an owning GPU value.
struct Input
{
    UGL::GraphBuffer<float> buffer;
};
/// Must remain a CPU function and cannot inspect GPU resource descriptions.
uint consume(Input input)
{
    return uint(input.buffer.size());
}
/// Forces transitive CPU ownership validation through graph.add.
[[Export]] auto graphEntry(Input input)
{
    UGL::TaskGraph graph;
    return graph.add(consume, input);
}
