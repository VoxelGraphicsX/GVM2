#pragma once

#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <EASTL/atomic.h>
#include <Metal/Metal.hpp>

namespace GVM::RHI::Metal
{
    /// Retains the native storage and completion of a single queued CPU read.
    class MReadbackImpl final : public ReadbackImpl
    {
    public:
        /// Retains the mapped allocation; a null buffer represents an immediately ready empty range.
        MReadbackImpl(const eastl::shared_ptr<DeviceLostState> &device, MTL::Buffer *buffer, uint64_t offset, uint64_t size);
        /// Releases the command buffer retained when the queue accepted this read.
        ~MReadbackImpl() override;
        /// Queries native completion without requiring a host wait or a subsequent queue operation.
        bool isReady() const override;
        /// Waits for this read only; a nonempty read must have been submitted first.
        void wait() override;
        /// Returns mapped bytes while this readback is held, or throws if the data is unavailable.
        eastl::span<const uint8_t> getData() const override;
        /// Publishes the single committed command buffer that orders this read.
        void assignCommandBuffer(MTL::CommandBuffer &commandBuffer);

    private:
        eastl::shared_ptr<DeviceLostState> mDevice;
        NS::SharedPtr<MTL::Buffer> mBuffer;
        eastl::atomic<MTL::CommandBuffer *> mCommandBuffer{nullptr};
        uint64_t mOffset;
        uint64_t mSize;
    };

    using MReadback = eastl::intrusive_ptr<MReadbackImpl>;
}
