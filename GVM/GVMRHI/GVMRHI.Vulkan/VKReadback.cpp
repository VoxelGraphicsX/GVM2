#include "VKReadback.hpp"
#include "VKDevice.hpp"
#include "VKBuffer.hpp"
#include "VKQueue.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    VKReadbackImpl::VKReadbackImpl(VKQueue &queue)
        : mQueue(queue), mRegistry(queue.getDevice()->getResourceRegistryHandle())
    {
    }

    VKReadbackImpl::~VKReadbackImpl()
    {
        const auto registry = mRegistry.lock();
        if (!registry)
        {
            return;
        }
        ScopedLock lock(registry->submissionMutex());
        if (registry->isDestroyed() || mBuffer.isNull())
        {
            return;
        }
        if (mUsesDirectMapping)
        {
            static_cast<VKBuffer *>(mBuffer.get())->releaseReference();
        }
        else
        {
            registry->freeBuffer(mBuffer);
        }
    }

    bool VKReadbackImpl::isReady() const
    {
        if (mReady.load(eastl::memory_order_acquire))
        {
            return true;
        }
        const auto registry = mRegistry.lock();
        if (!registry)
        {
            return true;
        }
        ScopedLock lock(registry->submissionMutex());
        if (registry->isDestroyed() || mQueue.getDevice()->getLostState().isLost())
        {
            return true;
        }
        if (mSubmissionSerial.load(eastl::memory_order_acquire) != UINT64_MAX)
        {
            mQueue.pollCompletedSubmissions();
        }
        return mReady.load(eastl::memory_order_acquire);
    }

    void VKReadbackImpl::wait()
    {
        const auto registry = mRegistry.lock();
        if (!registry || mReady.load(eastl::memory_order_acquire))
        {
            return;
        }
        ScopedLock lock(registry->submissionMutex());
        if (registry->isDestroyed() || mQueue.getDevice()->getLostState().isLost())
        {
            return;
        }
        const auto serial = mSubmissionSerial.load(eastl::memory_order_acquire);
        if (serial == UINT64_MAX)
        {
            throw std::logic_error("VKReadback::wait called before the queue submitted the read.");
        }
        mQueue.waitForSubmission(serial, "vk_readback_wait");
    }

    eastl::span<const uint8_t> VKReadbackImpl::getData() const
    {
        if (!isReady())
        {
            throw std::logic_error("VKReadback::getData called before the readback was ready.");
        }
        const auto registry = mRegistry.lock();
        if (!registry)
        {
            throw DeviceLostError("VKReadback::getData: the owning device no longer exists.");
        }
        ScopedLock lock(registry->submissionMutex());
        if (registry->isDestroyed() || !mReady.load(eastl::memory_order_acquire))
        {
            throw DeviceLostError("VKReadback::getData: mapped storage is no longer available.");
        }
        const auto *base = static_cast<const uint8_t *>(static_cast<VKBuffer *>(mBuffer.get())->getPersistentMappedData());
        return {base + mOffset, mSize};
    }

    void VKReadbackImpl::initDirect(const Buffer &sourceBuffer, uint64_t offset, uint64_t size)
    {
        static_cast<VKBuffer *>(sourceBuffer.get())->retainReference();
        mBuffer = sourceBuffer;
        mOffset = offset;
        mSize = size;
        mUsesDirectMapping = true;
    }

    void VKReadbackImpl::initStaging(const Buffer &stagingBuffer, uint64_t size)
    {
        mBuffer = stagingBuffer;
        mSize = size;
    }

    void VKReadbackImpl::assignSubmissionSerial(VKSubmissionSerial serial)
    {
        mSubmissionSerial.store(serial, eastl::memory_order_release);
    }

    void VKReadbackImpl::markReady()
    {
        mReady.store(true, eastl::memory_order_release);
    }
}
