#pragma once
#include "GraphDevice.hpp"

namespace GVM::Core::Private
{
    /// Owns one native allocation internally until its execution and GPU continuations release it.
    class GraphBufferAllocation final
    {
    public:
        /// Allocates through the graph device's serialized object boundary.
        GraphBufferAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::BufferDescriptor &descriptor);
        /// Retires the allocation after accepted GPU work has released its graph ownership.
        ~GraphBufferAllocation();
        /// Returns the live native allocation for argument capture and pass recording.
        RHI::Buffer buffer() const;
        /// Copies CPU data into mapped staging storage; the allocation must permit MapWrite.
        void write(const void *data, uint64_t size);
        /// Copies completed staging storage into an owned CPU result; no GPU wait is performed here.
        void read(uint64_t offset, void *destination, uint64_t size) const;
    private:
        eastl::shared_ptr<GraphDevice> mDevice;
        RHI::Buffer mBuffer;
    };
}
