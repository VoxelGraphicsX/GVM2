#pragma once

namespace GVM::Core
{
    /// Marks a statically resolved subgraph builder; native build receives an invocation-local RenderNode.
    /// DSL lowering supplies that argument implicitly, without mutable node context or thread-local storage.
    template <class InputValue, class OutputValue>
    class IRenderNode
    {
    public:
        using Input = InputValue;
        using Output = OutputValue;
        using RenderNodeProtocol = void;
    };
}
