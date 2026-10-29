#include "Private/BufferCopyMultipleRegionExecutor.hpp"

#include <EASTL/algorithm.h>

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Maximum regions encoded per dispatch; 65472 * 12 is a multiple of 256 and stays below the 65535-workgroup guarantee. */
        constexpr uint32_t kMaxRegionsPerDispatch = 65472u;

        void requireStorageBindableBuffer(BufferUsageFlags usage, const char *role)
        {
            if ((usage & BufferUsage::Storage) == 0u)
            {
                throw std::invalid_argument(
                    (eastl::string("copyBufferToBufferMultipleRegion binds the ") + (role != nullptr ? role : "buffer") +
                        " buffer as a storage buffer in the compute copy, but that buffer was not created with BufferUsage::Storage.").c_str());
            }
        }
    } // namespace

    void BufferCopyMultipleRegionExecutor::init(DeviceImpl &device, const BufferCopyMultipleRegionShader &shader)
    {
        if (shader.module == nullptr || shader.entryPoint == nullptr)
        {
            throw std::invalid_argument("BufferCopyMultipleRegionExecutor::init requires a valid shader module and entry point.");
        }

        mDevice = &device;
        mShader = shader;
        mBindGroupLayout = device.createBindGroupLayout({
            .label = "BufferCopyMultipleRegionBindGroupLayout",
            .entries = {
                {
                    .binding = 0u,
                    .visibility = ShaderStage::Compute,
                    .buffer = {.type = BufferBindingType::Storage, .access = StorageBufferAccess::ReadOnly},
                },
                {
                    .binding = 1u,
                    .visibility = ShaderStage::Compute,
                    .buffer = {.type = BufferBindingType::Storage, .access = StorageBufferAccess::ReadWrite},
                },
                {
                    .binding = 2u,
                    .visibility = ShaderStage::Compute,
                    .buffer = {.type = BufferBindingType::Storage, .access = StorageBufferAccess::ReadOnly},
                },
            },
        });
        mPipelineLayout = device.createPipelineLayout({
            .label = "BufferCopyMultipleRegionPipelineLayout",
            .bindGroupLayouts = {mBindGroupLayout},
        });
        mPipeline = device.createComputePipeline({
            .label = "BufferCopyMultipleRegionPipeline",
            .layout = mPipelineLayout,
            .compute = {
                .module = shader.module,
                .entryPoint = shader.entryPoint,
                .workgroupX = 64u,
                .workgroupY = 1u,
                .workgroupZ = 1u,
            },
        });
    }

    void BufferCopyMultipleRegionExecutor::validate(const BufferCopyMultipleRegionInputs &inputs) const
    {
        if (inputs.regionCount == 0u)
        {
            return;
        }
        if (inputs.source.isNull() || inputs.destination.isNull() || inputs.regions.isNull())
        {
            throw std::invalid_argument("BufferCopyMultipleRegionExecutor::encode requires valid source, destination, and region buffers.");
        }

        requireStorageBindableBuffer(inputs.sourceUsage, "source");
        requireStorageBindableBuffer(inputs.destinationUsage, "destination");
        requireStorageBindableBuffer(inputs.regionsUsage, "region-descriptor");

        if ((inputs.regionsUsage & (BufferUsage::MapRead | BufferUsage::MapWrite)) == 0u)
        {
            throw std::invalid_argument("copyBufferToBufferMultipleRegion requires a CPU-readable region buffer.");
        }
        const uint64_t requiredRegionBytes = static_cast<uint64_t>(inputs.regionCount) * sizeof(BufferCopyRegion);
        if (inputs.regions->getStorageSize() < requiredRegionBytes)
        {
            throw std::invalid_argument(
                "copyBufferToBufferMultipleRegion received a region descriptor buffer that is smaller than copyRegionCount * sizeof(BufferCopyRegion).");
        }
        if (inputs.cpuRegions == nullptr)
        {
            throw std::runtime_error("copyBufferToBufferMultipleRegion failed to access the persistently mapped region descriptor buffer for bounds validation.");
        }

        const uint64_t sourceSize = inputs.source->getStorageSize();
        const uint64_t destinationSize = inputs.destination->getStorageSize();
        for (uint32_t regionIndex = 0u; regionIndex < inputs.regionCount; ++regionIndex)
        {
            const BufferCopyRegion &region = inputs.cpuRegions[regionIndex];
            if ((region.srcOffset % 4u) != 0u || (region.dstOffset % 4u) != 0u || (region.size % 4u) != 0u)
            {
                throw std::invalid_argument("copyBufferToBufferMultipleRegion requires 4-byte-aligned region offsets and sizes.");
            }
            const uint64_t srcBegin = region.srcOffset;
            const uint64_t dstBegin = region.dstOffset;
            const uint64_t bytes = region.size;
            const uint64_t srcEnd = srcBegin + bytes;
            const uint64_t dstEnd = dstBegin + bytes;
            if (srcBegin > sourceSize || bytes > sourceSize - srcBegin)
            {
                throw std::invalid_argument(
                    ("copyBufferToBufferMultipleRegion source region out of bounds. Region " + eastl::to_string(regionIndex) +
                        " requires [" + eastl::to_string(srcBegin) + ", " + eastl::to_string(srcEnd) +
                        "), but source buffer size is " + eastl::to_string(sourceSize) + ".").c_str());
            }
            if (dstBegin > destinationSize || bytes > destinationSize - dstBegin)
            {
                throw std::invalid_argument(
                    ("copyBufferToBufferMultipleRegion destination region out of bounds. Region " + eastl::to_string(regionIndex) +
                        " requires [" + eastl::to_string(dstBegin) + ", " + eastl::to_string(dstEnd) +
                        "), but destination buffer size is " + eastl::to_string(destinationSize) + ".").c_str());
            }
        }
    }

    void BufferCopyMultipleRegionExecutor::encode(ComputePassEncoderImpl &encoder, const BufferCopyMultipleRegionInputs &inputs)
    {
        for (uint64_t base = 0u; base < inputs.regionCount; base += kMaxRegionsPerDispatch)
        {
            const uint32_t chunkCount = static_cast<uint32_t>(eastl::min<uint64_t>(inputs.regionCount - base, kMaxRegionsPerDispatch));
            BindGroup bindGroup = mDevice->createBindGroup({
                .label = "BufferCopyMultipleRegionBindGroup",
                .layout = mBindGroupLayout,
                .entries = {
                    {.binding = 0u, .buffer = BufferRange(inputs.source)},
                    {.binding = 1u, .buffer = BufferRange(inputs.destination)},
                    {.binding = 2u, .buffer = BufferRange(inputs.regions, base * sizeof(BufferCopyRegion), chunkCount * sizeof(BufferCopyRegion))},
                },
            });
            encoder.setPipeline(mPipeline);
            encoder.setBindGroup(bindGroup, 0u);
            encoder.dispatchWorkgroups(chunkCount, 1u, 1u);
        }
    }
} // namespace GVM::RHI::Private
