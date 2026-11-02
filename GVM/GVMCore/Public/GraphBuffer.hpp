#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/shared_ptr.h>

namespace GVM::Core
{
    namespace Private { class GraphBufferAllocation; }
    template <class T> class ReadbackNode;

    /// Owns a graph buffer range; copies retain the allocation, which must not outlive its RHI device.
    class GraphBuffer final
    {
    public:
        /// Creates an unbound value that cannot be used as a graph pass resource.
        GraphBuffer();
        /// Retains another range's allocation without copying GPU contents.
        GraphBuffer(const GraphBuffer &other);
        /// Replaces this range with another retained allocation view.
        GraphBuffer &operator=(const GraphBuffer &other);
        /// Transfers allocation ownership and leaves the source unbound.
        GraphBuffer(GraphBuffer &&other) noexcept;
        /// Transfers allocation ownership after releasing this range's previous view.
        GraphBuffer &operator=(GraphBuffer &&other) noexcept;
        /// Releases this range's allocation ownership without waiting for GPU work.
        ~GraphBuffer();
        /// Returns a retained allocation subrange and rejects overflow or out-of-bounds access.
        GraphBuffer slice(uint64_t offset, uint64_t size) const;
        /// Returns the byte count exposed by this range.
        uint64_t size() const;
        /// Supplies a native shader argument; graph nodes retain the owning GraphBuffer while recording and executing.
        RHI::BufferRange range() const;
        /// Adapts an owning graph buffer to generated immutable shader Args at the native boundary.
        operator RHI::BufferRange() const;
    private:
        friend class RenderNode;
        template <class T> friend class ReadbackNode;
        /// Creates an owning view of one runtime-managed allocation.
        GraphBuffer(eastl::shared_ptr<Private::GraphBufferAllocation> allocation, uint64_t offset, uint64_t size);
        /// Copies completed GPU data into ordinary host storage; used only by the readback continuation.
        void copyTo(void *destination, uint64_t size) const;
        eastl::shared_ptr<Private::GraphBufferAllocation> mAllocation;
        uint64_t mOffset = 0;
        uint64_t mSize = 0;
    };
}
