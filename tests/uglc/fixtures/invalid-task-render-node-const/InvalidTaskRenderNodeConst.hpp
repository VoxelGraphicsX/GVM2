#pragma once
#include "UGL.h"
using namespace UGL;

/// Rejects a const-qualified explicit RenderNode parameter on RenderNode::build.
class InvalidNode : public UGL::IRenderNode<uint, uint>
{
public:
    /// Declares an invalid const reference to RenderNode.
    uint build(const RenderNode &node, uint value) const
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
