#include "VKReadback.hpp"
#include "VKDevice.hpp"
#include "VKBuffer.hpp"
#include "VKQueue.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    bool VKReadbackImpl::isDeviceLost() const
    {
        return mQueue != nullptr && mQueue->getDevice() != nullptr && mQueue->getDevice()->getLostState().isLost();
    }

    bool VKReadbackImpl::isReady() const
    {
        return mReady.load(eastl::memory_order_acquire) || isDeviceLost();
    }

    void VKReadbackImpl::wait()
    {
        if (!mHasSubmissionSerial)
        {
            throw std::logic_error("VKReadback::wait called before the queue submitted the read.");
        }
        if (isReady() || isDeviceLost())
        {
            return;
        }
        mQueue->waitForSubmission(mSubmissionSerial, "vk_readback_wait");
        markReady();
    }

    eastl::span<const uint8_t> VKReadbackImpl::getData() const
    {
        if (isDeviceLost() && !mReady.load(eastl::memory_order_acquire))
        {
            throw DeviceLostError("VKReadback::getData: device lost before the readback completed.");
        }
        if (!isReady())
        {
            throw std::logic_error("VKReadback::getData called before the readback was ready.");
        }
        const uint8_t *base = nullptr;
        if (mUsesDirectMapping && !mSourceBuffer.isNull())
        {
            base = static_cast<const uint8_t *>(static_cast<VKBuffer *>(mSourceBuffer.get())->getPersistentMappedData());
        }
        else if (!mStagingBuffer.isNull())
        {
            base = static_cast<const uint8_t *>(static_cast<VKBuffer *>(mStagingBuffer.get())->getPersistentMappedData());
        }
        if (base == nullptr)
        {
            throw std::logic_error("VKReadback::getData has no readable mapped data.");
        }
        return eastl::span<const uint8_t>(base + mOffset, mSize);
    }

    void VKReadbackImpl::initDirect(VKQueue *queue, const Buffer &sourceBuffer, uint64_t offset, uint64_t size)
    {
        mQueue = queue;
        mSourceBuffer = sourceBuffer;
        mOffset = offset;
        mSize = size;
        mUsesDirectMapping = true;
    }

    void VKReadbackImpl::initStaging(VKQueue *queue, const Buffer &stagingBuffer, uint64_t size)
    {
        mQueue = queue;
        mStagingBuffer = stagingBuffer;
        mOffset = 0u;
        mSize = size;
        mUsesDirectMapping = false;
    }

    void VKReadbackImpl::assignSubmissionSerial(VKSubmissionSerial serial)
    {
        mSubmissionSerial = serial;
        mHasSubmissionSerial = true;
    }

    void VKReadbackImpl::markReady()
    {
        mReady.store(true, eastl::memory_order_release);
    }

    VKSubmissionSerial VKReadbackImpl::getSubmissionSerial() const
    {
        return mSubmissionSerial;
    }

} // namespace GVM::RHI::Vulkan
