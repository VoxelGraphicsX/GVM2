#pragma once
#include "UGL.h"
using namespace UGL;

/// Rejects a GPU resource nested inside an uploaded element type.
struct Element
{
    UGL::StructuredBuffer<float> resource;
};
/// Requires transitive element validation before emitting the subgraph.
class InvalidNode : public UGL::IRenderNode<uint, UGL::Buffer<Element, UGL::BufferUsage<UGL::Storage>>>
{
public:
    /// Attempts to allocate elements with resource ownership instead of CPU data.
    UGL::Buffer<Element, UGL::BufferUsage<UGL::Storage>> build(uint count) const
    {
        return createBuffer<UGL::Buffer<Element, UGL::BufferUsage<UGL::Storage>>>("Invalid", count);
    }
};
/// Invokes the invalid subgraph through the unified API.
[[Export]] auto graphEntry(uint input)
{
    UGL::TaskGraph graph;
    return graph.add(InvalidNode{}, input);
}
