#include "GQueue.hpp"
#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/GVMLogging.hpp>
#include <EASTL/make_intrusive.h>
#include <algorithm>
#include <stdexcept>
namespace GVM::Core
{
    namespace
    {
        constexpr eastl::string_view QueueLogCategory = "gvmcore.queue";

        /** Computes the mip extent used by Core queue texture upload and readback helpers. */
        GVM::RHI::Extent3D computeMipExtent(GVM::RHI::Texture texture, uint32_t mipLevel)
        {
            return {
                .width = std::max(1u, texture->getWidth() >> mipLevel),
                .height = std::max(1u, texture->getHeight() >> mipLevel),
                .depth = std::max(1u, texture->getDepth() >> mipLevel),
            };
        }

        /** Builds the copy origin for QueueProxy texture helpers while keeping 3D depth separate from array layers. */
        GVM::RHI::Origin3D resolveTextureCopyOrigin(GVM::RHI::Texture texture, uint32_t arrayLayerOffset)
        {
            const uint32_t arrayLayerCount = texture->getArrayLayerCount();
            if (arrayLayerOffset >= arrayLayerCount)
            {
                throw std::out_of_range("QueueProxyImpl texture copy array layer offset exceeds the texture array layer count.");
            }

            return {
                .x = 0u,
                .y = 0u,
                .z = arrayLayerCount > 1u ? arrayLayerOffset : 0u,
            };
        }
    } // namespace


    QueueProxyImpl::QueueProxyImpl(GVM::RHI::Queue queue, GVM::RHI::Logger logger)
        : QueueProxyImpl(nullptr, queue, logger)
    {
    }

    QueueProxyImpl::QueueProxyImpl(GVM::RHI::Device, GVM::RHI::Queue queue, GVM::RHI::Logger logger)
    {
        mQueue = queue;
        mLogger = logger;
        // mCommandEncoder = mQueue->createCommandEncoder();
    }

    QueueProxyImpl *QueueProxyImpl::writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            QueueLogCategory,
            "QueueProxyImpl::writeBuffer",
            "size={} offset={} range_size={}",
            size,
            buffer.offset,
            buffer.size);
        if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=write_buffer_invalid_buffer reason=null_destination_buffer");
            throw std::invalid_argument("QueueProxyImpl::writeBuffer received a null destination buffer.");
        }

        if (size == 0u)
        {
            return this;
        }

        if (data == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=write_buffer_invalid_source reason=null_source_data");
            throw std::invalid_argument("QueueProxyImpl::writeBuffer received null source data for a non-zero upload.");
        }

        const uint64_t storageSize = buffer.buffer->getStorageSize();
        const uint64_t remainingBytes = buffer.offset >= storageSize ? 0u : (storageSize - buffer.offset);
        const uint64_t writableBytes = std::min(buffer.size, remainingBytes);
        if (size > writableBytes)
        {
            GVMLogError(
                mLogger,
                QueueLogCategory,
                "event=write_buffer_out_of_range requested_bytes={} writable_bytes={}",
                size,
                writableBytes);
            throw std::out_of_range("QueueProxyImpl::writeBuffer upload size exceeds destination BufferRange.");
        }

        mQueue->writeBuffer(buffer, data, size);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::readBuffer(GVM::RHI::BufferRange buffer, void *data, uint64_t size)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            QueueLogCategory,
            "QueueProxyImpl::readBuffer",
            "size={} offset={} range_size={}",
            size,
            buffer.offset,
            buffer.size);
        if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=read_buffer_invalid_buffer reason=null_source_buffer");
            throw std::invalid_argument("QueueProxyImpl::readBuffer received a null source buffer.");
        }

        if (size == 0u)
        {
            return this;
        }

        if (data == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=read_buffer_invalid_destination reason=null_destination_data");
            throw std::invalid_argument("QueueProxyImpl::readBuffer received null destination data for a non-zero readback.");
        }

        const uint64_t storageSize = buffer.buffer->getStorageSize();
        const uint64_t remainingBytes = buffer.offset >= storageSize ? 0u : (storageSize - buffer.offset);
        const uint64_t readableBytes = std::min(buffer.size, remainingBytes);
        if (size > readableBytes)
        {
            GVMLogError(
                mLogger,
                QueueLogCategory,
                "event=read_buffer_out_of_range requested_bytes={} readable_bytes={}",
                size,
                readableBytes);
            throw std::out_of_range("QueueProxyImpl::readBuffer readback size exceeds source BufferRange.");
        }

        mQueue->readBuffer(buffer, data, size);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::fillBuffer(GVM::RHI::BufferRange source, uint32_t data)
    {
        mQueue->fillBuffer(source, data);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::writeTexture(GVM::RHI::Texture destination, void const *data, uint64_t dataSize, uint32_t miplevelOffset, uint32_t arrayLayerOffset)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            QueueLogCategory,
            "QueueProxyImpl::writeTexture",
            "data_size={} mip_level={} array_layer={}",
            dataSize,
            miplevelOffset,
            arrayLayerOffset);
        if (destination.isNull() || destination.get() == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=write_texture_invalid_texture reason=null_destination_texture");
            throw std::invalid_argument("QueueProxyImpl::writeTexture received a null destination texture.");
        }
        if (miplevelOffset >= destination->getMipLevelCount())
        {
            GVMLogError(mLogger, QueueLogCategory, "event=write_texture_invalid_mip reason=mip_level_out_of_range");
            throw std::out_of_range("QueueProxyImpl::writeTexture mip level exceeds the texture mip count.");
        }

        if (data == nullptr && dataSize != 0u)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=write_texture_invalid_source reason=null_source_data");
            throw std::invalid_argument("QueueProxyImpl::writeTexture received null source data for a non-zero upload.");
        }

        const auto mipExtent = computeMipExtent(destination, miplevelOffset);
        const auto copyOrigin = resolveTextureCopyOrigin(destination, arrayLayerOffset);
        mQueue->writeTexture({.texture = destination, .mipLevel = miplevelOffset, .origin = copyOrigin}, data, dataSize, {}, mipExtent);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::readTexture(GVM::RHI::Texture source, void *data, uint64_t dataSize, uint32_t miplevelOffset, uint32_t arrayLayerOffset)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            QueueLogCategory,
            "QueueProxyImpl::readTexture",
            "data_size={} mip_level={} array_layer={}",
            dataSize,
            miplevelOffset,
            arrayLayerOffset);
        if (source.isNull() || source.get() == nullptr)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=read_texture_invalid_texture reason=null_source_texture");
            throw std::invalid_argument("QueueProxyImpl::readTexture received a null source texture.");
        }
        if (miplevelOffset >= source->getMipLevelCount())
        {
            GVMLogError(mLogger, QueueLogCategory, "event=read_texture_invalid_mip reason=mip_level_out_of_range");
            throw std::out_of_range("QueueProxyImpl::readTexture mip level exceeds the texture mip count.");
        }

        if (data == nullptr && dataSize != 0u)
        {
            GVMLogError(mLogger, QueueLogCategory, "event=read_texture_invalid_destination reason=null_destination_data");
            throw std::invalid_argument("QueueProxyImpl::readTexture received null destination data for a non-zero readback.");
        }

        const auto mipExtent = computeMipExtent(source, miplevelOffset);
        const auto copyOrigin = resolveTextureCopyOrigin(source, arrayLayerOffset);
        mQueue->readTexture({.texture = source, .mipLevel = miplevelOffset, .origin = copyOrigin}, data, dataSize, {}, mipExtent);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::resolveTimestampProfiler(GVM::RHI::GpuTimestampFrameProfiler &profiler)
    {
        if (profiler.getUsedQueryCount() == 0u)
        {
            return this;
        }
        if (mCommandEncoder == nullptr)
        {
            mCommandEncoder = mQueue->createCommandEncoder();
        }
        profiler.resolve(mCommandEncoder);
        return this;
    }

    QueueProxyImpl *QueueProxyImpl::resolvePassCounterProfiler(GVM::RHI::GpuPassCounterFrameProfiler &profiler)
    {
        if (profiler.getUsedQueryCount() == 0u)
        {
            return this;
        }
        if (mCommandEncoder == nullptr)
        {
            mCommandEncoder = mQueue->createCommandEncoder();
        }
        profiler.resolve(mCommandEncoder);
        return this;
    }


    void QueueProxyImpl::submit()
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            QueueLogCategory,
            "QueueProxyImpl::submit",
            "has_command_encoder={}",
            mCommandEncoder != nullptr);
        GVMLogDebug(mLogger, QueueLogCategory, "event=queue_submit_begin has_command_encoder={}", mCommandEncoder != nullptr);

        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        if (mCommandEncoder != nullptr)
        {
            mCommandEncoder->end();
            encoders.push_back(mCommandEncoder);
        }
        mQueue->submit(encoders);
        mCommandEncoder = nullptr;

        GVMLogDebug(mLogger, QueueLogCategory, "event=queue_submit_end submitted_encoder_count={}", encoders.size());
    }

    BlitPassTaskDescriptor fillBuffer(GVM::RHI::BufferRange source, uint32_t data)
    {
        BlitPassTaskDescriptor desp;
        desp.blitFn = [=](GVM::RHI::BlitPassEncoder passEncoder) {
            passEncoder->fillBuffer(source, data);
        };
        return desp;
    }

    /* BlitPassTaskDescriptor writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size)
    {
        BlitPassTaskDescriptor desp;
        desp.blitFn = [=](GVM::RHI::BlitPassEncoder passEncoder) {
            passEncoder->(buffer, data, size);
        };
        return desp;
    } */

    BlitPassTaskDescriptor copyBufferToBuffer(GVM::RHI::Buffer src, uint64_t srcOffsetBytes, GVM::RHI::Buffer dst, uint64_t dstOffsetBytes, uint64_t copyBytes)
    {
        BlitPassTaskDescriptor desp;
        desp.blitFn = [=](GVM::RHI::BlitPassEncoder passEncoder) {
            passEncoder->copyBufferToBuffer(RHI::BufferRange(src, srcOffsetBytes, copyBytes), RHI::BufferRange(dst, dstOffsetBytes, copyBytes));
        };
        return desp;
    }

} // namespace GVM::Core
