#pragma once
#include "GraphDevice.hpp"

namespace GVM::Core::Private
{
    /// Owns one native allocation until graph cleanup or successful output retrieval transfers explicit destruction.
    class GraphResourceAllocation final
    {
    public:
        /// Allocates through the graph device's serialized object boundary.
        GraphResourceAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::BufferDescriptor &descriptor);
        /// Allocates a texture whose owned views follow the existing backend retirement contract.
        GraphResourceAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::TextureDescriptor &descriptor);
        /// Retires the allocation after accepted GPU work has released its graph ownership.
        ~GraphResourceAllocation();
        /// Returns the live native allocation for argument capture and pass recording.
        RHI::Buffer buffer() const;
        /// Returns the live native texture without exporting allocation ownership.
        RHI::Texture texture() const;
        /// Returns the allocation identity used to match successful graph exports.
        const void *resource() const;
        /// Transfers explicit destruction responsibility to the caller after a successful result retrieval.
        void releaseOwnership();
        /// Copies CPU data into mapped staging storage; the allocation must permit MapWrite.
        void write(const void *data, uint64_t size);
        /// Copies completed staging storage into an owned CPU result; no GPU wait is performed here.
        void read(uint64_t offset, void *destination, uint64_t size) const;
    private:
        eastl::shared_ptr<GraphDevice> mDevice;
        RHI::Buffer mBuffer;
        RHI::Texture mTexture;
        bool mOwned = true;
    };
}
