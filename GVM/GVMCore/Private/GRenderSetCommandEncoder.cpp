#include "GRenderSetCommandEncoder.hpp"
#include "GRenderSet.hpp"
#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/GVMLogging.hpp>
#include <EASTL/numeric_limits.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
    namespace
    {
        constexpr eastl::string_view RenderSetEncoderLogCategory = "gvmcore.render_set_encoder";

        uint32_t narrowToUint32(uint64_t value, const char *what)
        {
            if (value > eastl::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error(std::string("RenderSetCommandEncoder overflow while encoding ") + what + ".");
            }
            return static_cast<uint32_t>(value);
        }
    }

    // ======================================================================================
    // 实现部分
    // ======================================================================================

    void RenderSetCommandEncoderImpl::create(GVM::RHI::Device device, RenderSet *renderSet)
    {
        GVMCpuProbeScopeDetail(
            device,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::create",
            "has_render_set={}",
            renderSet != nullptr);
        mDevice = device;
        mLogger = device != nullptr ? device->getLogger() : GVM::RHI::Logger{};
        mRenderSet = renderSet;
        // 预估一个大小，避免 vector 频繁扩容
        mCPUStaging.reserve(1024 * 1024 * 4);

        GVMLogDebug(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_create");
    }

    /** Returns the highest RenderEntityCMDParams element uploaded by this encoder. */
    uint32_t RenderSetCommandEncoderImpl::getPublishableCMDParamsCount() const
    {
        return mPublishableCMDParamsCount;
    }

    // --------------------------------------------------------------------------------------
    // 用户线程逻辑：Alloc Entity
    // 优化点：在这里直接完成 "数据序列化" 和 "指令录制"。
    // 之前是：计算大小 -> 分配 -> 拷贝 -> (UserThread Loop) -> 再次计算 -> 再次拷贝。
    // 现在是：计算并拷贝到线性 buffer -> 记录最终 GPU 偏移。
    // --------------------------------------------------------------------------------------
    RenderEntityIndex RenderSetCommandEncoderImpl::allocEntity(const RenderSetAllocInfo &info)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::allocEntity",
            "instance_count={} buffer_infos={} texture_infos={}",
            info.instanceCount,
            info.bufferInfos.size(),
            info.textureInfos.size());
        std::lock_guard lock(mMTX);
        RenderEntityIndex entity = mRenderSet->alloc(info);


        // 2. 处理 Buffer 组件的数据录制
        for (const auto &bufferInfo : info.bufferInfos)
        {
            auto component = mRenderSet->getBufferComponentByHandle(bufferInfo.bufferComponentHandle);
            if (bufferInfo.value != nullptr)
            {
                {
                    // A. 将用户数据“流送”进线性分配器，获得在 Staging Buffer 中的相对偏移 (srcOffset)
                    uint64_t srcOffset = mCPUStaging.appendRaw(bufferInfo.value, bufferInfo.dataStorageSize, mAlign);

                    uint64_t dstOffset = component->getDataBufferCopyOffset(entity, 0);

                    mComponentDataBufferPendingCopies[bufferInfo.bufferComponentHandle].targetComponent = bufferInfo.bufferComponentHandle;
                    mComponentDataBufferPendingCopies[bufferInfo.bufferComponentHandle].add(srcOffset, dstOffset, bufferInfo.dataStorageSize);
                }
            }
            {
                const auto allocInfo = component->getComponentIndexAllocInfo(entity, mCPUStaging);
                mComponentIndexBufferPendingCopies[bufferInfo.bufferComponentHandle].targetComponent = bufferInfo.bufferComponentHandle;
                recordIndexCopyCommand(allocInfo, mComponentIndexBufferPendingCopies[bufferInfo.bufferComponentHandle]);
            }
        }

        // 3. 处理 Texture 组件的数据录制
        for (const auto &texInfo : info.textureInfos)
        {
            auto component = mRenderSet->getTextureComponentByHandle(texInfo.textureComponentHandle);
            for (const auto &srcTexture : texInfo.textures)
            {
                if (srcTexture.data != nullptr)
                {
                    const uint64_t srcOffset = mCPUStaging.appendRaw(srcTexture.data, srcTexture.dataStorageBytes, mAlign);

                    RenderSetTextureComponentTextureCopyCommand dstTexture = {};
                    dstTexture.textureName = srcTexture.textureName;
                    dstTexture.format = srcTexture.format;
                    dstTexture.height = srcTexture.height;
                    dstTexture.width = srcTexture.width;
                    dstTexture.copyByteSize = narrowToUint32(srcTexture.dataStorageBytes, "texture upload byte size");
                    dstTexture.stagingBufferOffset = narrowToUint32(srcOffset, "texture upload staging offset");
                    // Keep mip offsets relative to the texture payload. The upload path
                    // already adds stagingBufferOffset/bufferRange.offset when recording
                    // the backend copy, so rebasing here would double-apply srcOffset.
                    dstTexture.mipmapOffsetBytes = srcTexture.mipmapOffsetBytes;
                    mComponentTexturePendingCopies[texInfo.textureComponentHandle].textureComponentHandle = texInfo.textureComponentHandle;
                    mComponentTexturePendingCopies[texInfo.textureComponentHandle].textures.push_back(dstTexture);
                }
            }
            {
                const auto allocInfo = component->getComponentIndexAllocInfo(entity, mCPUStaging);
                mComponentIndexBufferPendingCopies[texInfo.textureComponentHandle].targetComponent = texInfo.textureComponentHandle;
                recordIndexCopyCommand(allocInfo, mComponentIndexBufferPendingCopies[texInfo.textureComponentHandle]);
            }
        }

        {
            const auto allocInfo = mRenderSet->getRenderEntityInfoAllocInfo(entity, mCPUStaging);
            recordIndexCopyCommand(allocInfo, mRenderEntityInfoPendingCopies);
        }
        {

            const auto allocInfo = mRenderSet->getRenderEntityCMDParamsAllocInfo(entity, mCPUStaging);
            recordIndexCopyCommand(allocInfo, mRenderEntityCMDParamsPendingCopies);
            if (allocInfo.size != 0)
            {
                const uint32_t cmdParamsEnd = narrowToUint32(
                    (uint64_t(allocInfo.GPUBufferDstOffset) + allocInfo.size) / sizeof(RenderEntityCMDParam),
                    "publishable cmd params count");
                if (mPublishableCMDParamsCount < cmdParamsEnd)
                {
                    mPublishableCMDParamsCount = cmdParamsEnd;
                }
            }
        }

        return entity;
    }

    /** Records one bounded BufferComponent update using the allocation established by allocEntity. */
    void RenderSetCommandEncoderImpl::setBufferComponentData(
        RenderEntityIndex entity,
        RenderComponentHandle componentHandle,
        const void *value,
        uint64_t dataStorageSize,
        uint32_t instanceStartIndex,
        uint32_t instanceCount)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::setBufferComponentData",
            "entity={} component={} instance_start={} instance_count={} storage_bytes={}",
            entity,
            componentHandle,
            instanceStartIndex,
            instanceCount,
            dataStorageSize);
        std::lock_guard lock(mMTX);
        if (value == nullptr)
        {
            throw std::invalid_argument("RenderSet BufferComponent update requires non-null data.");
        }
        if (instanceCount == 0u)
        {
            throw std::invalid_argument("RenderSet BufferComponent update requires a positive instance count.");
        }
        if (mRenderSet->check(entity) == false || mPendingRemoves.find(entity) != mPendingRemoves.end())
        {
            throw std::invalid_argument("RenderSet BufferComponent update requires a live entity.");
        }

        const auto component = mRenderSet->getBufferComponentByHandle(componentHandle);
        if (component->check(entity) == false)
        {
            throw std::invalid_argument("RenderSet BufferComponent update requires an allocated entity component.");
        }
        const uint64_t elementStorageSize = component->mElementStorageSize;
        if (elementStorageSize == 0u || uint64_t(instanceCount) > eastl::numeric_limits<uint64_t>::max() / elementStorageSize)
        {
            throw std::overflow_error("RenderSet BufferComponent update size overflowed uint64_t.");
        }
        const uint64_t requiredStorageSize = uint64_t(instanceCount) * elementStorageSize;
        if (dataStorageSize != requiredStorageSize)
        {
            throw std::invalid_argument("RenderSet BufferComponent update payload must exactly match its element range.");
        }
        const auto &componentUseInfo = component->mEntities.read(entity);
        if (uint64_t(instanceStartIndex) > eastl::numeric_limits<uint64_t>::max() / elementStorageSize)
        {
            throw std::overflow_error("RenderSet BufferComponent update offset overflowed uint64_t.");
        }
        const uint64_t updateByteOffset = uint64_t(instanceStartIndex) * elementStorageSize;
        if (updateByteOffset > componentUseInfo.bufferStorageSize ||
            dataStorageSize > componentUseInfo.bufferStorageSize - updateByteOffset)
        {
            throw std::out_of_range("RenderSet BufferComponent update exceeds the entity allocation.");
        }

        const uint64_t srcOffset = mCPUStaging.appendRaw(value, dataStorageSize, mAlign);
        const uint64_t dstOffset = component->getDataBufferCopyOffset(entity, instanceStartIndex);
        mComponentDataBufferPendingCopies[componentHandle].targetComponent = componentHandle;
        mComponentDataBufferPendingCopies[componentHandle].add(srcOffset, dstOffset, dataStorageSize);
    }

    void RenderSetCommandEncoderImpl::removeEntity(RenderEntityIndex entity)
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::removeEntity",
            "entity={}",
            entity);
        std::lock_guard lock(mMTX);
        if (mRenderSet->check(entity) == false || mPendingRemoves.find(entity) != mPendingRemoves.end())
        {
            return;
        }
        mPendingRemoves.emplace(entity);

        {

            const auto allocInfo = mRenderSet->getRenderEntityCMDParamsRemoveInfo(entity, mCPUStaging);
            recordIndexCopyCommand(allocInfo, mRenderEntityCMDParamsPendingCopies);
        }
        {
            const auto allocInfo = mRenderSet->getRenderEntityInfoRemoveInfo(entity, mCPUStaging);
            recordIndexCopyCommand(allocInfo, mRenderEntityInfoPendingCopies);
        }

        for (int componentIndex = 0; componentIndex < mRenderSet->mComponentNameList.size(); componentIndex++)
        {
            const RenderComponentHandle componentHandler = mRenderSet->createRenderComponentHandle(componentIndex);
            auto component = mRenderSet->getComponentByHandle(componentHandler);
            const auto allocInfo = component->getComponentIndexRemoveInfo(entity, mCPUStaging);
            mComponentIndexBufferPendingCopies[componentHandler].targetComponent = componentHandler;
            recordIndexCopyCommand(allocInfo, mComponentIndexBufferPendingCopies[componentHandler]);
        }
    }

    // --------------------------------------------------------------------------------------
    // User Thread 阶段：将数据提交到 GPU Staging
    // 此时不再做任何逻辑计算，仅仅是内存搬运 (Memcpy)
    // --------------------------------------------------------------------------------------
    void RenderSetCommandEncoderImpl::processInUserThread()
    {
        if (mCPUStaging.empty())
        {
            return;
        }
        uint64_t totalSize = mCPUStaging.size();
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::processInUserThread",
            "cpu_staging_bytes={} pending_remove_count={}",
            totalSize,
            mPendingRemoves.size());
        GVMCpuProbeValueU64(mLogger, RenderSetEncoderLogCategory, "render_set_encoder.cpu_staging_bytes", totalSize);

        if (mGPUStagingBuffer.isNull() || mGPUStagingBuffer->getStorageSize() < totalSize)
        {
            if (!mGPUStagingBuffer.isNull())
            {
                mDevice->freeBuffer(mGPUStagingBuffer);
                mGPUStagingBuffer.reset();
            }
            mGPUStagingBuffer = mDevice->createBuffer({.label = "RenderSet Staging Buffer", .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapWrite | GVM::RHI::BufferUsage::Storage, .size = totalSize});
            GVMLogDebug(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_staging_resize size_bytes={}", totalSize);
        }

        bool stagingMapped = false;
        try
        {
            mGPUStagingBuffer->map();
            stagingMapped = true;
            void *dstPtr = mGPUStagingBuffer->getMappedRange(0, totalSize);
            if (dstPtr == nullptr)
            {
                GVMLogError(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_staging_map_failed reason=null_mapped_range");
                throw std::runtime_error("RenderSetCommandEncoderImpl::processInternal failed to map the GPU staging buffer.");
            }
            memcpy(dstPtr, mCPUStaging.data(), totalSize);
            mGPUStagingBuffer->unmap();
            stagingMapped = false;
        }
        catch (...)
        {
            if (stagingMapped)
            {
                mGPUStagingBuffer->unmap();
            }
            throw;
        }

        for (auto &[handle, batchCmd] : mComponentDataBufferPendingCopies)
        {
            if (batchCmd.isEmpty() == false)
            {
                batchCmd.create(mDevice);
            }
        }
        for (auto &[handle, batchCmd] : mComponentIndexBufferPendingCopies)
        {
            if (batchCmd.isEmpty() == false)
            {
                batchCmd.create(mDevice);
            }
        }


        if (mRenderEntityInfoPendingCopies.isEmpty() == false)
        {
            mRenderEntityInfoPendingCopies.create(mDevice);
        }
        if (mRenderEntityCMDParamsPendingCopies.isEmpty() == false)
        {
            mRenderEntityCMDParamsPendingCopies.create(mDevice);
        }
        mRenderSet->resizeBuffer();

        GVMLogTrace(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_process_user_thread_end cpu_staging_bytes={}", totalSize);
    }

    // --------------------------------------------------------------------------------------
    // Render Main Thread 阶段：执行 RHI 指令
    // --------------------------------------------------------------------------------------
    void RenderSetCommandEncoderImpl::processInternal()
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::processInternal",
            "pending_remove_count={} cpu_staging_bytes={}",
            mPendingRemoves.size(),
            mCPUStaging.size());
        GVMLogTrace(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_process_internal_begin pending_remove_count={}", mPendingRemoves.size());

        for (auto &[handle, batchCmd] : mComponentDataBufferPendingCopies)
        {
            if (batchCmd.isEmpty() == false)
            {
                executeBufferCopyCommand(batchCmd, mRenderSet->getBufferComponentByHandle(handle)->getDataBuffer());
            }
        }
        for (auto &[handle, batchCmd] : mComponentIndexBufferPendingCopies)
        {
            if (batchCmd.isEmpty() == false)
            {
                executeBufferCopyCommand(batchCmd, mRenderSet->getComponentByHandle(handle)->getComponentIndexBuffer());
            }
        }
        for (auto &[handle, batchCmd] : mComponentTexturePendingCopies)
        {
            if (batchCmd.textures.empty())
            {
                continue;
            }
            auto component = mRenderSet->getTextureComponentByHandle(handle);
            {
                auto thisTextureComponent = mRenderSet->getTextureComponentByHandle(handle);
                for (auto textureCopy : batchCmd.textures)
                {
                    textureCopy.stagingBuffer = mGPUStagingBuffer;
                    thisTextureComponent->performTextureCopy(textureCopy);
                }
            }
        }


        {
            executeBufferCopyCommand(mRenderEntityInfoPendingCopies, mRenderSet->getRenderEntityInfoBuffer());
        }
        {
            executeBufferCopyCommand(mRenderEntityCMDParamsPendingCopies, mRenderSet->getRenderEntityCMDParamsBuffer());
        }

        for (const auto &entity : mPendingRemoves)
        {
            mRenderSet->remove(entity);
        }
        mPendingRemoves.clear();

        mCPUStaging.reset();

        GVMLogTrace(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_process_internal_end");
    }

    void RenderSetCommandEncoderImpl::destroy()
    {
        GVMCpuProbeScopeDetail(
            mLogger,
            RenderSetEncoderLogCategory,
            "RenderSetCommandEncoderImpl::destroy",
            "pending_remove_count={} has_gpu_staging_buffer={}",
            mPendingRemoves.size(),
            !mGPUStagingBuffer.isNull());
        GVMLogDebug(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_destroy_begin");

        for (const auto &e : mPendingRemoves)
        {
            if (mRenderSet->check(e) == false)
            {
                continue;
            }
            mRenderSet->remove(e);
        }
        if (!mGPUStagingBuffer.isNull())
        {
            mDevice->freeBuffer(mGPUStagingBuffer);
            mGPUStagingBuffer.reset();
        }
        for (auto &pair : mComponentDataBufferPendingCopies)
        {
            pair.second.destroy();
        }
        for (auto &pair : mComponentIndexBufferPendingCopies)
        {
            pair.second.destroy();
        }
        mPendingRemoves.clear();
        mRenderEntityInfoPendingCopies.destroy();
        mRenderEntityCMDParamsPendingCopies.destroy();
        mComponentTexturePendingCopies.clear();
        GVMLogDebug(mLogger, RenderSetEncoderLogCategory, "event=render_set_encoder_destroy_end");
        mLogger = nullptr;
    }

    void RenderSetCommandEncoderImpl::recordIndexCopyCommand(const RenderComponentIndexCopyInfo &allocInfo, RenderComponentBatchedCopyCommand &copyCMD)
    {
        if (allocInfo.size == 0)
        {
            return;
        }


        copyCMD.add(allocInfo.CPUDataSrcOffset, allocInfo.GPUBufferDstOffset, allocInfo.size);
    }

    void RenderSetCommandEncoderImpl::executeBufferCopyCommand(RenderComponentBatchedCopyCommand &copyCommand, RHI::Buffer dstBuffer)
    {
        if (copyCommand.regions.empty())
        {
            return;
        }

        mDevice->getMainQueue()->copyBufferToBufferMultipleRegion(mGPUStagingBuffer, dstBuffer, copyCommand.gpuCommandBuffer, copyCommand.regions.size());
    }


} // namespace GVM::Core
