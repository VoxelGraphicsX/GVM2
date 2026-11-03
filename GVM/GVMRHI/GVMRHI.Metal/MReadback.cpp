#include "MReadback.hpp"
#include <stdexcept>

namespace GVM::RHI::Metal
{
    MReadbackImpl::MReadbackImpl(const eastl::shared_ptr<DeviceLostState> &device, MTL::Buffer *buffer, uint64_t offset, uint64_t size)
        : mDevice(device), mBuffer(NS::RetainPtr(buffer)), mOffset(offset), mSize(size)
    {
    }

    MReadbackImpl::~MReadbackImpl()
    {
        if (auto *command = mCommandBuffer.load(eastl::memory_order_acquire))
        {
            command->release();
        }
    }

    void MReadbackImpl::assignCommandBuffer(MTL::CommandBuffer &commandBuffer)
    {
        mCommandBuffer.store(commandBuffer.retain(), eastl::memory_order_release);
    }

    bool MReadbackImpl::isReady() const
    {
        if (mSize == 0 || mDevice->isLost())
        {
            return true;
        }
        const auto *command = mCommandBuffer.load(eastl::memory_order_acquire);
        if (command == nullptr)
        {
            return false;
        }
        const auto status = command->status();
        if (status == MTL::CommandBufferStatusError)
        {
            const auto *error = command->error();
            const auto *description = error != nullptr ? error->localizedDescription() : nullptr;
            mDevice->markLost(DeviceLostReason::GpuFault, description != nullptr ? description->cString(NS::UTF8StringEncoding) : "Metal readback failed.");
            return true;
        }
        return status == MTL::CommandBufferStatusCompleted;
    }

    void MReadbackImpl::wait()
    {
        if (isReady())
        {
            return;
        }
        auto *command = mCommandBuffer.load(eastl::memory_order_acquire);
        if (command == nullptr)
        {
            throw std::logic_error("MReadback::wait called before the queue submitted the read.");
        }
        command->waitUntilCompleted();
        (void)isReady();
    }

    eastl::span<const uint8_t> MReadbackImpl::getData() const
    {
        if (mSize == 0)
        {
            return {};
        }
        if (!isReady())
        {
            throw std::logic_error("MReadback::getData called before the readback was ready.");
        }
        const auto *command = mCommandBuffer.load(eastl::memory_order_acquire);
        if (command == nullptr || command->status() != MTL::CommandBufferStatusCompleted)
        {
            throw DeviceLostError("MReadback::getData: device lost before the readback completed.");
        }
        return {static_cast<const uint8_t *>(mBuffer->contents()) + mOffset, mSize};
    }
}
