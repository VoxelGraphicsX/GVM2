#pragma once
#include "../MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MBufferCopyMultipleRegionExecutorImpl final : public GVM::RHI::RefCountedObject
    {
    public:
        void create(MDevice *device);
        void validateCopyRegions(Buffer copyRegionBuffer, uint32_t copyRegionCount) const;
        void execute(ComputePassEncoder encoder, Buffer srcBuffer, Buffer dstBuffer, Buffer copyRegionBuffer, uint32_t copyRegionCount);

    private:
        ComputePipeline mBufferCopyPipeline = nullptr;
        MDevice *mDevice = nullptr;
    };
    using MBufferCopyMultipleRegionExecutor = eastl::intrusive_ptr<MBufferCopyMultipleRegionExecutorImpl>;
} // namespace GVM::RHI::Metal
