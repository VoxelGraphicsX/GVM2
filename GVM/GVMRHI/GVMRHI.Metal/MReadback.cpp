#include "MReadback.hpp"
#include "MDevice.hpp"
#include "MBuffer.hpp"
#include "MQueue.hpp"

#include <stdexcept>

namespace GVM::RHI::Metal
{
    /// Releases the retained command buffer when the readback is destroyed without a wait.
    MReadbackImpl::~MReadbackImpl()
    {
        if (mCommandBuffer != nullptr)
        {
            mCommandBuffer->release();
        }
    }

    bool MReadbackImpl::isDeviceLost() const
    {
        return mQueue != nullptr && mQueue->getDevice() != nullptr && mQueue->getDevice()->getLostState().isLost();
    }

    bool MReadbackImpl::isReady() const
    {
        return mReady.load(eastl::memory_order_acquire) || isDeviceLost();
    }

    void MReadbackImpl::wait()
    {
        if (!mHasCommandBuffer)
        {
            throw std::logic_error("MReadback::wait called before the queue submitted the read.");
        }
        if (isReady() || isDeviceLost())
        {
            return;
        }
        if (mCommandBuffer != nullptr)
        {
            mCommandBuffer->waitUntilCompleted();
            if (mCommandBuffer->status() == MTL::CommandBufferStatusError)
            {
                const auto *error = mCommandBuffer->error();
                const auto code = error != nullptr ? error->code() : MTL::CommandBufferErrorNone;
                if (code == MTL::CommandBufferErrorTimeout || code == MTL::CommandBufferErrorPageFault || code == MTL::CommandBufferErrorDeviceRemoved)
                {
                    const char *description = error != nullptr && error->localizedDescription() != nullptr ? error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding) : "command buffer failed";
                    mQueue->getDevice()->getLostState().markLost(DeviceLostReason::GpuFault, description);
                    mCommandBuffer->release();
                    mCommandBuffer = nullptr;
                    return;
                }
            }
            mCommandBuffer->release();
            mCommandBuffer = nullptr;
        }
        markReady();
    }

    eastl::span<const uint8_t> MReadbackImpl::getData() const
    {
        if (isDeviceLost() && !mReady.load(eastl::memory_order_acquire))
        {
            throw DeviceLostError("MReadback::getData: device lost before the readback completed.");
        }
        if (!isReady())
        {
            throw std::logic_error("MReadback::getData called before the readback was ready.");
        }
        const uint8_t *base = nullptr;
        if (mUsesDirectMapping && !mSourceBuffer.isNull())
        {
            base = static_cast<const uint8_t *>(static_cast<MBuffer *>(mSourceBuffer.get())->getNativeBuffer()->contents());
        }
        else if (!mStagingBuffer.isNull())
        {
            base = static_cast<const uint8_t *>(static_cast<MBuffer *>(mStagingBuffer.get())->getNativeBuffer()->contents());
        }
        if (base == nullptr)
        {
            throw std::logic_error("MReadback::getData has no readable mapped data.");
        }
        return eastl::span<const uint8_t>(base + mOffset, mSize);
    }

    void MReadbackImpl::initDirect(MQueue *queue, const Buffer &sourceBuffer, uint64_t offset, uint64_t size)
    {
        mQueue = queue;
        mSourceBuffer = sourceBuffer;
        mOffset = offset;
        mSize = size;
        mUsesDirectMapping = true;
    }

    void MReadbackImpl::initStaging(MQueue *queue, const Buffer &stagingBuffer, uint64_t size)
    {
        mQueue = queue;
        mStagingBuffer = stagingBuffer;
        mOffset = 0u;
        mSize = size;
        mUsesDirectMapping = false;
    }

    void MReadbackImpl::assignCommandBuffer(MTL::CommandBuffer *commandBuffer)
    {
        if (mCommandBuffer != nullptr)
        {
            mCommandBuffer->release();
        }
        mCommandBuffer = commandBuffer;
        if (mCommandBuffer != nullptr)
        {
            mCommandBuffer->retain();
        }
        mHasCommandBuffer = true;
    }

    void MReadbackImpl::markReady()
    {
        mReady.store(true, eastl::memory_order_release);
    }

    MTL::CommandBuffer *MReadbackImpl::getCommandBuffer() const
    {
        return mCommandBuffer;
    }

} // namespace GVM::RHI::Metal
