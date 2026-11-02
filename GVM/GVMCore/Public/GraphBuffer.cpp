#include "GraphBuffer.hpp"
#include "../Private/GraphBufferAllocation.hpp"
#include <stdexcept>

namespace GVM::Core
{
    GraphBuffer::GraphBuffer() = default;
    GraphBuffer::GraphBuffer(const GraphBuffer &other) = default;
    GraphBuffer &GraphBuffer::operator=(const GraphBuffer &other) = default;
    GraphBuffer::GraphBuffer(GraphBuffer &&other) noexcept = default;
    GraphBuffer &GraphBuffer::operator=(GraphBuffer &&other) noexcept = default;
    GraphBuffer::~GraphBuffer() = default;
    GraphBuffer::GraphBuffer(eastl::shared_ptr<Private::GraphBufferAllocation> allocation, uint64_t offset, uint64_t size)
        : mAllocation(eastl::move(allocation)), mOffset(offset), mSize(size)
    {
    }
    GraphBuffer GraphBuffer::slice(uint64_t offset, uint64_t size) const
    {
        if (!mAllocation || offset > mSize || size > mSize - offset)
        {
            throw std::out_of_range("Graph buffer slice exceeds its owned range.");
        }
        return GraphBuffer(mAllocation, mOffset + offset, size);
    }
    uint64_t GraphBuffer::size() const
    {
        return mSize;
    }
    RHI::BufferRange GraphBuffer::range() const
    {
        if (!mAllocation)
        {
            throw std::logic_error("Graph buffer is unbound.");
        }
        return RHI::BufferRange(mAllocation->buffer(), mOffset, mSize);
    }
    GraphBuffer::operator RHI::BufferRange() const
    {
        return range();
    }
    void GraphBuffer::copyTo(void *destination, uint64_t size) const
    {
        if (!mAllocation || size != mSize)
        {
            throw std::invalid_argument("Readback destination must match the graph buffer range.");
        }
        mAllocation->read(mOffset, destination, size);
    }
}
