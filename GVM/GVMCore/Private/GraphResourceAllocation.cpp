#include "GraphResourceAllocation.hpp"
#include <cstring>

namespace GVM::Core::Private
{
    GraphResourceAllocation::GraphResourceAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::BufferDescriptor &descriptor)
        : mDevice(eastl::move(device))
    {
        auto lock = mDevice->lock();
        mBuffer = mDevice->device().createBuffer(descriptor);
    }
    GraphResourceAllocation::GraphResourceAllocation(eastl::shared_ptr<GraphDevice> device, const RHI::TextureDescriptor &descriptor)
        : mDevice(eastl::move(device))
    {
        auto lock = mDevice->lock();
        mTexture = mDevice->device().createTexture(descriptor);
    }
    GraphResourceAllocation::~GraphResourceAllocation()
    {
        if (!mOwned)
        {
            return;
        }
        auto lock = mDevice->lock();
        if (!mBuffer.isNull())
        {
            mDevice->device().freeBuffer(mBuffer);
        }
        if (!mTexture.isNull())
        {
            mDevice->device().freeTexture(mTexture);
        }
    }
    RHI::Buffer GraphResourceAllocation::buffer() const
    {
        return mBuffer;
    }
    RHI::Texture GraphResourceAllocation::texture() const
    {
        return mTexture;
    }
    const void *GraphResourceAllocation::resource() const
    {
        return mBuffer.isNull() ? static_cast<const void *>(mTexture.get()) : mBuffer.get();
    }
    void GraphResourceAllocation::releaseOwnership()
    {
        mOwned = false;
    }
    void GraphResourceAllocation::write(const void *data, uint64_t size)
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
    void GraphResourceAllocation::read(uint64_t offset, void *destination, uint64_t size) const
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
