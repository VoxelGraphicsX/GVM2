#pragma once
#include "IRenderNode.hpp"
#include "RenderNode.hpp"

namespace GVM::Core
{
    /// Converts a tightly packed GPU buffer into an owned CPU vector after its transfer pass completes.
    /// T must be trivially copyable and use the same layout on the host and GPU.
    template <class T>
    class ReadbackNode final : public IRenderNode<GraphBuffer, eastl::vector<T>>
    {
    public:
        /// Emits the staging copy; the runtime publishes the actual value through the completion continuation.
        eastl::vector<T> build(RenderNode &node, GraphBuffer source) const
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "Readback elements must be trivially copyable.");
            if (source.size() % sizeof(T) != 0)
            {
                throw std::invalid_argument("Graph readback does not match the requested element layout.");
            }
            auto staging = node.prepareReadback(eastl::move(source));
            node.resolveAfterGpu([staging = eastl::move(staging)]() -> eastl::any
            {
                eastl::vector<T> values(staging.size() / sizeof(T));
                staging.copyTo(values.data(), staging.size());
                return values;
            });
            return {};
        }
    };
}
