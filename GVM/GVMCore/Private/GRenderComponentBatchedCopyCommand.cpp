#include "GRenderComponentBatchedCopyCommand.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace GVM::Core
{
    namespace
    {
        static_assert((RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion % sizeof(uint32_t)) == 0u,
                      "RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion must stay 4-byte aligned for BufferCopyRegion execution.");

        uint32_t narrowCopyOffset(uint64_t value, const char *what)
        {
            if (value > std::numeric_limits<uint32_t>::max())
            {
                if (what != nullptr)
                {
                    throw std::overflow_error("RenderComponentBatchedCopyCommand::add overflowed a BufferCopyRegion offset.");
                }
                throw std::overflow_error("RenderComponentBatchedCopyCommand::add overflowed an internal copy offset.");
            }
            return static_cast<uint32_t>(value);
        }
    } // namespace


    void RenderComponentBatchedCopyCommand::create(GVM::RHI::Device device)
    {
        this->device = device;
        if (!gpuCommandBuffer.isNull())
        {
            this->device->freeBuffer(gpuCommandBuffer);
            gpuCommandBuffer.reset();
        }

        size_t cmdBufferSize = regions.size() * sizeof(GVM::RHI::BufferCopyRegion);

        // 简单起见，这里每次创建。生产环境建议用 RingBuffer 复用。
        gpuCommandBuffer = device->createBuffer({.label = "Copy Region Cmd", .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapWrite, .size = cmdBufferSize});

        bool isMapped = false;
        try
        {
            gpuCommandBuffer->map();
            isMapped = true;
            void *mappedRange = gpuCommandBuffer->getMappedRange(0, cmdBufferSize);
            if (mappedRange == nullptr)
            {
                throw std::runtime_error("RenderComponentBatchedCopyCommand::create failed to acquire a mapped range for the copy-region buffer.");
            }
            memcpy(mappedRange, regions.data(), cmdBufferSize);
            gpuCommandBuffer->unmap();
            isMapped = false;
        }
        catch (...)
        {
            if (isMapped)
            {
                gpuCommandBuffer->unmap();
            }
            throw;
        }
    }

    void RenderComponentBatchedCopyCommand::add(uint32_t tSrcOffset, uint32_t tDstOffset, uint32_t tSize)
    {
        if (tSize == 0)
        {
            return;
        }

        uint64_t srcOffset = tSrcOffset;
        uint64_t dstOffset = tDstOffset;
        uint32_t remainingBytes = tSize;

        while (remainingBytes > 0)
        {
            const uint32_t chunkBytes = eastl::min<uint32_t>(remainingBytes, MaxCopyBytesPerRegion);
            regions.push_back({
                .srcOffset = narrowCopyOffset(srcOffset, "srcOffset"),
                .dstOffset = narrowCopyOffset(dstOffset, "dstOffset"),
                .size = chunkBytes,
            });

            remainingBytes -= chunkBytes;
            srcOffset += chunkBytes;
            dstOffset += chunkBytes;
        }
    }

    void RenderComponentBatchedCopyCommand::destroy()
    {
        regions.clear();
        if (gpuCommandBuffer.isNull() == false)
        {
            if (device != nullptr)
            {
                device->freeBuffer(gpuCommandBuffer);
            }
            gpuCommandBuffer.reset();
        }
        device = nullptr;
    }


} // namespace GVM::Core
