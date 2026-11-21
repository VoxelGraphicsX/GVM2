#pragma once
#include "UGL.h"
using namespace UGL;

/// Attempts to accept a RenderNode reference outside an IRenderNode member function.
uint forwardOutsideRenderNode(RenderNode &node, uint value)
{
    return value;
}

/// Rejects passing RenderNode& into a non-IRenderNode free function.
class InvalidNode : public UGL::IRenderNode<uint, uint>
{
public:
    /// Calls a free function taking RenderNode& from an IRenderNode build method.
    uint build(RenderNode &node, uint value) const
    {
        return forwardOutsideRenderNode(node, value);
    }
};
/// Invokes the invalid subgraph through the unified API.
[[Export]] auto graphEntry(uint input)
{
    UGL::TaskGraph graph;
    return graph.add(InvalidNode{}, input);
}
