#pragma once
#include "UGL.h"
using namespace UGL;
using SampleBuffer = Buffer<float, BufferUsage<Storage, CopySrc, CopyDst>>;

/// Builds a transient allocation using the existing public buffer type.
class Allocate : public IRenderNode<uint, SampleBuffer>
{
public:
    /// Returns a graph-local handle for downstream GPU nodes.
    SampleBuffer build(uint count) const
    {
        return createBuffer<SampleBuffer>("Transient", count);
    }
};

/// Rejects exporting a borrowed GPU handle without the planned owner adapter.
[[Export]] auto invalidExport(uint count)
{
    TaskGraph graph;
    return graph.add(Allocate{}, count);
}
