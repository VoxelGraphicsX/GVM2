#pragma once
#include "UGL.h"
using namespace UGL;

/// Rejects an ordinary CPU callable whose nested input contains a GPU resource handle.
struct Input
{
    UGL::Buffer<float, UGL::BufferUsage<UGL::Storage, UGL::CopySrc>> buffer;
};
/// Must remain a CPU function and cannot inspect GPU resource descriptions.
uint consume(Input input)
{
    return 0u;
}
/// Forces transitive CPU ownership validation through graph.add.
[[Export]] auto graphEntry(Input input)
{
    UGL::TaskGraph graph;
    return graph.add(consume, input);
}
