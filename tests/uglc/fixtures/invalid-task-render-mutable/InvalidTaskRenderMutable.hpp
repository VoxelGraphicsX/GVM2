#pragma once
#include "UGL.h"
using namespace UGL;

/// Rejects mutation of shared subgraph configuration during build.
class InvalidNode : public UGL::IRenderNode<uint, uint>
{
public:
    /// Omits the required const configuration contract.
    uint build(uint value)
    {
        return value;
    }
};
/// Invokes the invalid subgraph through the unified API.
[[Export]] auto graphEntry(uint input)
{
    UGL::TaskGraph graph;
    return graph.add(InvalidNode{}, input);
}
