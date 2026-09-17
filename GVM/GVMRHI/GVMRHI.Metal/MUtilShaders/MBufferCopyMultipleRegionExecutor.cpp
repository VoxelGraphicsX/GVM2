#include "MBufferCopyMultipleRegionExecutor.hpp"
#include "../MBlitPassEncoder.hpp"
#include "../MBuffer.hpp"
#include "../MCommandEncoder.hpp"
#include "../MComputePassEncoder.hpp"
#include "../MComputePipeline.hpp"
#include "../MDevice.hpp"
#include "MUtilShaders.hpp"
#include <stdexcept>
#include <string>
#include <xGEFoundation/xMath.hpp>
namespace GVM::RHI::Metal
{
    namespace
    {
        void validateCopyRegionsAreWordAligned(Buffer copyRegionBuffer, uint32_t copyRegionCount)
        {
            auto *metalRegionBuffer = static_cast<MBuffer *>(copyRegionBuffer.get());
            const BufferUsageFlags usage = metalRegionBuffer->getUsage();
            if ((usage & (BufferUsage::MapRead | BufferUsage::MapWrite)) == 0u)
            {
                throw std::invalid_argument("copyBufferToBufferMultipleRegion requires the region descriptor buffer to be created with MapRead or MapWrite so 4-byte alignment can be validated.");
            }

            const bool shouldUnmapAfterValidation = !metalRegionBuffer->isMapped();
            if (shouldUnmapAfterValidation)
            {
                metalRegionBuffer->map();
            }
            try
            {
                const auto *regions = static_cast<const BufferCopyRegion *>(metalRegionBuffer->getConstMappedRange(0, static_cast<uint64_t>(copyRegionCount) * sizeof(BufferCopyRegion)));
                if (regions == nullptr)
                {
                    throw std::runtime_error("copyBufferToBufferMultipleRegion failed to access the region descriptor buffer for alignment validation.");
                }

                for (uint32_t regionIndex = 0; regionIndex < copyRegionCount; ++regionIndex)
                {
                    const BufferCopyRegion &region = regions[regionIndex];
                    const bool aligned = (region.srcOffset % 4u) == 0u && (region.dstOffset % 4u) == 0u && (region.size % 4u) == 0u;
                    if (!aligned)
                    {
                        throw std::invalid_argument(
                            "copyBufferToBufferMultipleRegion only supports 4-byte-aligned srcOffset/dstOffset/size. Region " + std::to_string(regionIndex) +
                            " has srcOffset=" + std::to_string(region.srcOffset) +
                            ", dstOffset=" + std::to_string(region.dstOffset) +
                            ", size=" + std::to_string(region.size) + ".");
                    }
                }
            }
            catch (...)
            {
                if (shouldUnmapAfterValidation)
                {
                    metalRegionBuffer->unmap();
                }
                throw;
            }

            if (shouldUnmapAfterValidation)
            {
                metalRegionBuffer->unmap();
            }
        }
    } // namespace

    void MBufferCopyMultipleRegionExecutorImpl::create(MDevice *device)
    {
        mDevice = device;
        {
            auto shaderModule = device->createShaderModule({.code = getCopyBufferToBufferMultipleRegion()});

            auto mPipeline = device->createComputePipeline({.compute = {.module = shaderModule, .entryPoint = "executeCopyBufferCommands", .workgroupX = 64, .workgroupY = 1, .workgroupZ = 1}});
            mBufferCopyPipeline = mPipeline;
        }
    }

    void MBufferCopyMultipleRegionExecutorImpl::validateCopyRegions(Buffer copyRegionBuffer, uint32_t copyRegionCount) const
    {
        validateCopyRegionsAreWordAligned(copyRegionBuffer, copyRegionCount);
    }

    void MBufferCopyMultipleRegionExecutorImpl::execute(ComputePassEncoder encoder, Buffer srcBuffer, Buffer dstBuffer, Buffer copyRegionBuffer, uint32_t copyRegionCount)
    {
        auto nativeComputeEncoder = eastl::static_pointer_cast<MComputePassEncoder>(encoder)->getNativeEncoder();
        validateCopyRegions(copyRegionBuffer, copyRegionCount);

        encoder->setPipeline(mBufferCopyPipeline);
        nativeComputeEncoder->setBuffer(static_cast<MBuffer *>(srcBuffer.get())->getNativeBuffer(), 0, 0);
        nativeComputeEncoder->setBuffer(static_cast<MBuffer *>(dstBuffer.get())->getNativeBuffer(), 0, 1);
        nativeComputeEncoder->setBuffer(static_cast<MBuffer *>(copyRegionBuffer.get())->getNativeBuffer(), 0, 2);

        nativeComputeEncoder->setBytes(&copyRegionCount, sizeof(copyRegionCount), 3);
        encoder->dispatchWorkgroups(eastl::max(xGE::Math::IntRoundUp(copyRegionCount, 64), uint32_t(1)), 1, 1);
    }


} // namespace GVM::RHI::Metal
