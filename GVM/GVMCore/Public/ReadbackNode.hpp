#pragma once
#include "IRenderNode.hpp"
#include "RenderNode.hpp"
#include "../Private/GraphResourceAllocation.hpp"

namespace GVM::Core
{
    /// Converts a tightly packed GPU buffer into an owned CPU vector after its transfer pass completes.
    /// T must be trivially copyable and use the same layout on the host and GPU.
    template <class T>
    class ReadbackNode final : public IRenderNode<RHI::BufferRange, eastl::vector<T>>
    {
    public:
        /// Emits the staging copy; the runtime publishes the actual value through the completion continuation.
        eastl::vector<T> build(RenderNode &node, RHI::BufferRange source) const
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "Readback elements must be trivially copyable.");
            source = RHI::BufferRange(source.buffer, source.offset, source.size);
            if (source.size % sizeof(T) != 0)
            {
                throw std::invalid_argument("Graph readback does not match the requested element layout.");
            }
            auto staging = node.prepareReadback(source);
            node.resolveAfterGpu([staging = eastl::move(staging), size = source.size]() -> eastl::any
            {
                eastl::vector<T> values(size / sizeof(T));
                staging->read(0, values.data(), size);
                return values;
            });
            return {};
        }
    };

    /// Converts tightly packed texel bytes from a GPU texture into an owned CPU vector after GPU completion.
    /// T must be trivially copyable and divide the copied subresource byte count evenly.
    template <class T>
    class ReadbackTextureNode final : public IRenderNode<TextureReadbackDescriptor, eastl::vector<T>>
    {
    public:
        /// Emits the texture staging copy; the runtime publishes the typed vector through the completion continuation.
        eastl::vector<T> build(RenderNode &node, TextureReadbackDescriptor source) const
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "Texture readback elements must be trivially copyable.");
            auto staged = node.prepareTextureReadback(source);
            if (staged.byteCount % sizeof(T) != 0)
            {
                throw std::invalid_argument("Graph texture readback does not match the requested element layout.");
            }
            node.resolveAfterGpu([staging = eastl::move(staged.allocation), size = staged.byteCount]() -> eastl::any
            {
                eastl::vector<T> values(size / sizeof(T));
                staging->read(0, values.data(), size);
                return values;
            });
            return {};
        }
    };
}
