#include "VKBufferCopyMultipleRegionExecutor.hpp"

#include "VKBindingUtils.hpp"
#include "VKBuffer.hpp"
#include "VKBufferCopyMultipleRegionSpirv.hpp"
#include "VKCommandEncoder.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKComputePassEncoder.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"

#include <EASTL/string.h>
namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr uint32_t kCopyRegionsPerWorkgroup = 64u;

        struct CopyPushConstants
        {
            uint32_t copyRegionCount = 0u;
        };

        eastl::vector<uint32_t> buildCopyBufferToBufferMultipleRegionSpirv()
        {
            const auto *begin = Detail::CopyBufferToBufferMultipleRegionSpirvWords;
            return eastl::vector<uint32_t>(begin, begin + Detail::CopyBufferToBufferMultipleRegionSpirvWordCount);
        }

        eastl::vector<vk::DescriptorSetLayoutBinding> buildDescriptorBindings()
        {
            return {
                vk::DescriptorSetLayoutBinding{
                    0u,
                    vk::DescriptorType::eStorageBuffer,
                    1u,
                    vk::ShaderStageFlagBits::eCompute,
                    nullptr,
                },
                vk::DescriptorSetLayoutBinding{
                    1u,
                    vk::DescriptorType::eStorageBuffer,
                    1u,
                    vk::ShaderStageFlagBits::eCompute,
                    nullptr,
                },
                vk::DescriptorSetLayoutBinding{
                    2u,
                    vk::DescriptorType::eStorageBuffer,
                    1u,
                    vk::ShaderStageFlagBits::eCompute,
                    nullptr,
                },
            };
        }

        eastl::vector<vk::DescriptorPoolSize> buildPoolSizesPerSet()
        {
            return {
                vk::DescriptorPoolSize{
                    vk::DescriptorType::eStorageBuffer,
                    3u,
                },
            };
        }

        void validateCopyRegionsAreWordAligned(const VKBuffer *regionBuffer, uint32_t copyRegionCount)
        {
            if (copyRegionCount == 0u)
            {
                return;
            }
            if (regionBuffer == nullptr)
            {
                throw makeInvalidArgument("validateCopyRegionsAreWordAligned requires a valid Vulkan region buffer.");
            }

            const BufferUsageFlags usage = regionBuffer->getUsage();
            if ((usage & (BufferUsage::MapRead | BufferUsage::MapWrite)) == 0u)
            {
                throw makeInvalidArgument("copyBufferToBufferMultipleRegion requires the region descriptor buffer to be created with MapRead or MapWrite so 4-byte alignment can be validated.");
            }

            const auto *regions = static_cast<const BufferCopyRegion *>(regionBuffer->getPersistentMappedData());
            if (regions == nullptr)
            {
                throw makeRuntimeError("copyBufferToBufferMultipleRegion failed to access the persistently mapped region descriptor buffer.");
            }

            for (uint32_t regionIndex = 0u; regionIndex < copyRegionCount; ++regionIndex)
            {
                const BufferCopyRegion &region = regions[regionIndex];
                const bool aligned = (region.srcOffset % 4u) == 0u &&
                    (region.dstOffset % 4u) == 0u &&
                    (region.size % 4u) == 0u;
                if (!aligned)
                {
                    throw makeInvalidArgument(
                        "copyBufferToBufferMultipleRegion only supports 4-byte-aligned srcOffset/dstOffset/size. Region " + eastl::to_string(regionIndex) +
                        " has srcOffset=" + eastl::to_string(region.srcOffset) +
                        ", dstOffset=" + eastl::to_string(region.dstOffset) +
                        ", size=" + eastl::to_string(region.size) + ".");
                }
            }
        }

        void validateStorageBindableBuffer(const VKBuffer *buffer, const char *role)
        {
            if (buffer == nullptr)
            {
                throw makeInvalidArgument("copyBufferToBufferMultipleRegion requires a valid Vulkan " + eastl::string(role != nullptr ? role : "buffer") + " buffer.");
            }
            if (!buffer->supportsStorageBinding())
            {
                throw makeInvalidArgument(
                    "copyBufferToBufferMultipleRegion binds the " + eastl::string(role != nullptr ? role : "buffer") +
                    " buffer as a storage buffer in the Vulkan compute fallback, but that buffer was not created with BufferUsage::Storage.");
            }
        }

        void validateCopyRegionsAreInBounds(
            const VKBuffer *sourceBuffer,
            const VKBuffer *destinationBuffer,
            const VKBuffer *regionBuffer,
            uint32_t copyRegionCount)
        {
            if (copyRegionCount == 0u)
            {
                return;
            }

            validateStorageBindableBuffer(sourceBuffer, "source");
            validateStorageBindableBuffer(destinationBuffer, "destination");
            validateStorageBindableBuffer(regionBuffer, "region-descriptor");

            const uint64_t requiredRegionBytes = static_cast<uint64_t>(copyRegionCount) * sizeof(BufferCopyRegion);
            if (regionBuffer->getStorageSize() < requiredRegionBytes)
            {
                throw makeInvalidArgument(
                    "copyBufferToBufferMultipleRegion received a region descriptor buffer that is smaller than copyRegionCount * sizeof(BufferCopyRegion).");
            }

            const auto *regions = static_cast<const BufferCopyRegion *>(regionBuffer->getPersistentMappedData());
            if (regions == nullptr)
            {
                throw makeRuntimeError("copyBufferToBufferMultipleRegion failed to access the persistently mapped region descriptor buffer for bounds validation.");
            }

            const uint64_t sourceSize = sourceBuffer->getStorageSize();
            const uint64_t destinationSize = destinationBuffer->getStorageSize();
            for (uint32_t regionIndex = 0u; regionIndex < copyRegionCount; ++regionIndex)
            {
                const BufferCopyRegion &region = regions[regionIndex];
                const uint64_t srcBegin = region.srcOffset;
                const uint64_t dstBegin = region.dstOffset;
                const uint64_t bytes = region.size;
                const uint64_t srcEnd = srcBegin + bytes;
                const uint64_t dstEnd = dstBegin + bytes;
                if (srcEnd > sourceSize)
                {
                    throw makeInvalidArgument(
                        "copyBufferToBufferMultipleRegion source region out of bounds. Region " + eastl::to_string(regionIndex) +
                        " requires [" + eastl::to_string(srcBegin) + ", " + eastl::to_string(srcEnd) +
                        "), but source buffer size is " + eastl::to_string(sourceSize) + ".");
                }
                if (dstEnd > destinationSize)
                {
                    throw makeInvalidArgument(
                        "copyBufferToBufferMultipleRegion destination region out of bounds. Region " + eastl::to_string(regionIndex) +
                        " requires [" + eastl::to_string(dstBegin) + ", " + eastl::to_string(dstEnd) +
                        "), but destination buffer size is " + eastl::to_string(destinationSize) + ".");
                }
            }
        }

        uint32_t computeCopyDispatchGroupCount(uint32_t copyRegionCount)
        {
            return copyRegionCount == 0u ? 0u : ((copyRegionCount + kCopyRegionsPerWorkgroup - 1u) / kCopyRegionsPerWorkgroup);
        }
    } // namespace

    VKBufferCopyMultipleRegionExecutorImpl::~VKBufferCopyMultipleRegionExecutorImpl()
    {
    }

    void VKBufferCopyMultipleRegionExecutorImpl::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKBufferCopyMultipleRegionExecutorImpl::init requires a valid device.");
        }

        mDevice = device;

        // Pre-build the compute fallback during queue init so the first
        // queue-internal multi-region copy does not pay a surprise cold-start cost.
        ensureComputeFallbackInitialized();
    }

    void VKBufferCopyMultipleRegionExecutorImpl::ensureComputeFallbackInitialized()
    {
        if (mPipeline)
        {
            return;
        }

        static_assert(sizeof(BufferCopyRegion) == 12u, "BufferCopyRegion must remain a tightly packed 12-byte StructuredBuffer element.");
        static_assert(sizeof(CopyPushConstants) == 4u, "CopyPushConstants must stay a 4-byte push constant payload.");
        mDescriptorBindings = buildDescriptorBindings();
        mPoolSizesPerSet = buildPoolSizesPerSet();

        vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
        descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(mDescriptorBindings.size());
        descriptorSetLayoutCreateInfo.pBindings = mDescriptorBindings.data();
        mDescriptorSetLayout = mDevice->getNativeDevice().createDescriptorSetLayoutUnique(descriptorSetLayoutCreateInfo);

        const vk::PushConstantRange pushConstantRange = {
            vk::ShaderStageFlagBits::eCompute,
            0u,
            sizeof(CopyPushConstants),
        };

        const vk::DescriptorSetLayout descriptorSetLayout = mDescriptorSetLayout.get();
        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.setLayoutCount = 1u;
        pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1u;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        mPipelineLayout = mDevice->getNativeDevice().createPipelineLayoutUnique(pipelineLayoutCreateInfo);

        const eastl::vector<uint32_t> spirv = buildCopyBufferToBufferMultipleRegionSpirv();
        vk::ShaderModuleCreateInfo shaderModuleCreateInfo = {};
        shaderModuleCreateInfo.codeSize = spirv.size() * sizeof(uint32_t);
        shaderModuleCreateInfo.pCode = spirv.data();
        mShaderModule = mDevice->getNativeDevice().createShaderModuleUnique(shaderModuleCreateInfo);

        vk::PipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStage.module = mShaderModule.get();
        shaderStage.pName = "computeMain";

        vk::ComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.stage = shaderStage;
        pipelineCreateInfo.layout = mPipelineLayout.get();
        mPipeline = mDevice->getNativeDevice().createComputePipelineUnique(nullptr, pipelineCreateInfo).value;
    }

    void VKBufferCopyMultipleRegionExecutorImpl::validateCopyRegions(Buffer srcBuffer, Buffer dstBuffer, Buffer copyRegionBuffer, uint32_t copyRegionCount) const
    {
        if (copyRegionCount == 0u)
        {
            return;
        }
        if (srcBuffer.isNull() || dstBuffer.isNull())
        {
            throw makeInvalidArgument("VKBufferCopyMultipleRegionExecutorImpl::validateCopyRegions requires valid source and destination buffers.");
        }
        if (copyRegionBuffer.isNull())
        {
            throw makeInvalidArgument("VKBufferCopyMultipleRegionExecutorImpl::validateCopyRegions requires a valid region buffer.");
        }

        auto *sourceBuffer = static_cast<VKBuffer *>(srcBuffer.get());
        auto *destinationBuffer = static_cast<VKBuffer *>(dstBuffer.get());
        auto *regionBuffer = static_cast<VKBuffer *>(copyRegionBuffer.get());
        if (sourceBuffer == nullptr || destinationBuffer == nullptr || regionBuffer == nullptr)
        {
            throw makeInvalidArgument("VKBufferCopyMultipleRegionExecutorImpl::validateCopyRegions received a non-Vulkan buffer handle.");
        }
        validateCopyRegionsAreWordAligned(regionBuffer, copyRegionCount);
        validateCopyRegionsAreInBounds(sourceBuffer, destinationBuffer, regionBuffer, copyRegionCount);
    }

    void VKBufferCopyMultipleRegionExecutorImpl::encode(
        ComputePassEncoder encoder,
        Buffer srcBuffer,
        Buffer dstBuffer,
        Buffer copyRegionBuffer,
        uint32_t copyRegionCount)
    {
        if (encoder == nullptr || srcBuffer.isNull() || dstBuffer.isNull() || copyRegionBuffer.isNull())
        {
            throw makeInvalidArgument("VKBufferCopyMultipleRegionExecutorImpl::encode requires a valid encoder and source/destination/region buffers.");
        }
        if (copyRegionCount == 0u)
        {
            return;
        }

        auto *computeEncoder = static_cast<VKComputePassEncoder *>(encoder.get());
        auto *commandEncoder = computeEncoder != nullptr ? computeEncoder->getCommandEncoder() : nullptr;
        auto *sourceBuffer = static_cast<VKBuffer *>(srcBuffer.get());
        auto *destinationBuffer = static_cast<VKBuffer *>(dstBuffer.get());
        auto *regionBuffer = static_cast<VKBuffer *>(copyRegionBuffer.get());
        if (commandEncoder == nullptr || sourceBuffer == nullptr || destinationBuffer == nullptr || regionBuffer == nullptr)
        {
            throw makeLogicError("VKBufferCopyMultipleRegionExecutorImpl::encode could not resolve the Vulkan command context.");
        }

        ensureComputeFallbackInitialized();
        if (!mDescriptorSetLayout || !mPipelineLayout || !mPipeline)
        {
            throw makeLogicError("VKBufferCopyMultipleRegionExecutorImpl::encode could not initialize the compute fallback resources.");
        }

        commandEncoder->retainBuffer(srcBuffer);
        commandEncoder->retainBuffer(dstBuffer);
        commandEncoder->retainBuffer(copyRegionBuffer);

        vk::CommandBuffer commandBuffer = commandEncoder->getNativeCommandBuffer();
        auto &stateTracker = commandEncoder->getTaskDependencyResolver();
        const uint64_t regionBytes = static_cast<uint64_t>(copyRegionCount) * sizeof(BufferCopyRegion);

        stateTracker.synchronizeBufferRange(
            commandBuffer,
            *sourceBuffer,
            0u,
            WholeSize,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eShaderRead);
        stateTracker.synchronizeBufferRange(
            commandBuffer,
            *destinationBuffer,
            0u,
            WholeSize,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        stateTracker.synchronizeBufferRange(
            commandBuffer,
            *regionBuffer,
            0u,
            regionBytes,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eShaderRead);

        const VKTransientDescriptorAllocator::AllocationResult allocation = commandEncoder->getTransientDescriptorAllocator().allocate(
            mDescriptorSetLayout.get(),
            mPoolSizesPerSet);
        const vk::DescriptorSet descriptorSet = allocation.descriptorSet;

        const vk::DescriptorBufferInfo bufferInfos[] = {
            vk::DescriptorBufferInfo{sourceBuffer->getNativeBuffer(), 0u, VK_WHOLE_SIZE},
            vk::DescriptorBufferInfo{destinationBuffer->getNativeBuffer(), 0u, VK_WHOLE_SIZE},
            vk::DescriptorBufferInfo{regionBuffer->getNativeBuffer(), 0u, regionBytes},
        };
        const vk::WriteDescriptorSet writes[] = {
            vk::WriteDescriptorSet{
                descriptorSet,
                0u,
                0u,
                1u,
                vk::DescriptorType::eStorageBuffer,
                nullptr,
                &bufferInfos[0],
                nullptr,
            },
            vk::WriteDescriptorSet{
                descriptorSet,
                1u,
                0u,
                1u,
                vk::DescriptorType::eStorageBuffer,
                nullptr,
                &bufferInfos[1],
                nullptr,
            },
            vk::WriteDescriptorSet{
                descriptorSet,
                2u,
                0u,
                1u,
                vk::DescriptorType::eStorageBuffer,
                nullptr,
                &bufferInfos[2],
                nullptr,
            },
        };
        mDevice->getNativeDevice().updateDescriptorSets(writes, {});

        const CopyPushConstants pushConstants = {
            .copyRegionCount = copyRegionCount,
        };

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, mPipeline.get());
        bindDescriptorSet(
            commandBuffer,
            vk::PipelineBindPoint::eCompute,
            mPipelineLayout.get(),
            descriptorSet,
            0u);
        commandBuffer.pushConstants(mPipelineLayout.get(), vk::ShaderStageFlagBits::eCompute, 0u, sizeof(pushConstants), &pushConstants);
        commandBuffer.dispatch(computeCopyDispatchGroupCount(copyRegionCount), 1u, 1u);
    }
} // namespace GVM::RHI::Vulkan
