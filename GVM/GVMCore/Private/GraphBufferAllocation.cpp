#include "GraphBufferAllocation.hpp"
#include <cstring>

namespace GVM::Core::Private
{
    GraphBufferAllocation::GraphBufferAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::BufferDescriptor &descriptor)
        : mDevice(eastl::move(device))
    {
        auto lock = mDevice->lock();
        mBuffer = mDevice->device().createBuffer(descriptor);
    }
    GraphBufferAllocation::~GraphBufferAllocation()
    {
        auto lock = mDevice->lock();
        mDevice->device().freeBuffer(mBuffer);
    }
    RHI::Buffer GraphBufferAllocation::buffer() const
    {
        return mBuffer;
    }
    void GraphBufferAllocation::write(const void *data, uint64_t size)
    {
        auto lock = mDevice->lock();
        mBuffer->map();
        try
        {
            std::memcpy(mBuffer->getMappedRange(0, size), data, size);
        }
        catch (...)
        {
            mBuffer->unmap();
            throw;
        }
        mBuffer->unmap();
    }
    void GraphBufferAllocation::read(uint64_t offset, void *destination, uint64_t size) const
    {
        auto lock = mDevice->lock();
        mBuffer->map();
        try
        {
            std::memcpy(destination, mBuffer->getConstMappedRange(offset, size), size);
        }
        catch (...)
        {
            mBuffer->unmap();
            throw;
        }
        mBuffer->unmap();
    }
}
