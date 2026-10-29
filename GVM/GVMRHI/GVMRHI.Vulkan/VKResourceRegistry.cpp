#include "VKResourceRegistry.hpp"

#include "VKBuffer.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKResourceLifetime.hpp"
#include "VKSampler.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include "Private/TimeUtils.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <chrono>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view DeviceLogCategory = "gvmrhi.vulkan.device";

        [[nodiscard]]
        double durationMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return Internal::elapsedMilliseconds(startTime);
        }

        /** Rounds a texel extent up to a compressed-format block count. */
        uint64_t divideRoundUp(uint64_t value, uint64_t divisor)
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        /** Estimates storage bytes for a live texture using public texture dimensions and format block metadata. */
        uint64_t estimateTextureStorageBytes(Texture texture)
        {
            if (texture.isNull())
            {
                return 0u;
            }

            const Private::BlockInfo block = Private::getTextureBlockInfo(texture->getFormat());
            uint64_t estimatedBytes = 0u;
            for (uint32_t mipLevel = 0u; mipLevel < texture->getMipLevelCount(); ++mipLevel)
            {
                const uint64_t mipWidth = eastl::max(1u, texture->getWidth() >> mipLevel);
                const uint64_t mipHeight = eastl::max(1u, texture->getHeight() >> mipLevel);
                const uint64_t mipDepth = eastl::max(1u, texture->getDepth() >> mipLevel);
                const uint64_t blockColumns = divideRoundUp(mipWidth, block.width);
                const uint64_t blockRows = divideRoundUp(mipHeight, block.height);
                estimatedBytes += blockColumns * blockRows * mipDepth * texture->getArrayLayerCount() * block.bytes;
            }
            return estimatedBytes;
        }

        /** Adds a live buffer handle to a diagnostics snapshot when the handle is valid. */
        void appendBufferSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Buffer buffer)
        {
            if (buffer.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Buffer;
            entry.label = buffer->getLabelName();
            entry.estimatedBytes = buffer->getStorageSize();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }

        /** Adds a live texture handle to a diagnostics snapshot when the handle is valid. */
        void appendTextureSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Texture texture)
        {
            if (texture.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Texture;
            entry.label = texture->getLabelName();
            entry.estimatedBytes = estimateTextureStorageBytes(texture);
            entry.width = texture->getWidth();
            entry.height = texture->getHeight();
            entry.depth = texture->getDepth();
            entry.mipLevelCount = texture->getMipLevelCount();
            entry.arrayLayerCount = texture->getArrayLayerCount();
            entry.format = texture->getFormat();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }

        template <typename Handle>
        void eraseTrackedHandle(eastl::vector<Handle> &handles, Handle handle)
        {
            for (auto it = handles.begin(); it != handles.end(); ++it)
            {
                if (*it == handle)
                {
                    handles.erase(it);
                    return;
                }
            }
        }

        // Destroys each enqueued handle whose lifetime reached zero, then removes it from the live list.
        template <typename Handle, typename Impl>
        size_t releaseCollectedHandles(
            VKResourceRegistry *registry,
            Mutex &resourceLifetimeMutex,
            eastl::vector<Handle> &handles,
            eastl::vector<Handle> &liveHandles,
            void (VKResourceRegistry::*releaseHandle)(Handle))
        {
            size_t released = 0u;
            for (Handle &handle : handles)
            {
                auto *impl = static_cast<Impl *>(handle.get());
                if (impl == nullptr || !impl->isDestroyable())
                {
                    continue;
                }
                (registry->*releaseHandle)(handle);
                {
                    ScopedLock lock(resourceLifetimeMutex);
                    eraseTrackedHandle(liveHandles, handle);
                }
                ++released;
            }
            return released;
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKBuffer &buffer)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_buffer_skip_already_requested label={} total_refs={}",
                safeLogLabel(buffer.getLabelName()),
                buffer.getReferenceCount());
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKTexture &texture)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_texture_skip_already_requested label={} total_refs={}",
                safeLogLabel(texture.getLabelName()),
                texture.getReferenceCount());
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKSampler &sampler)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_sampler_skip_already_requested label={} total_refs={}",
                safeLogLabel(sampler.getLabelName()),
                sampler.getReferenceCount());
        }

        void logFreeQueued(VKDevice *device, const VKBuffer &buffer, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_buffer_queued label={} live_buffers={}",
                safeLogLabel(buffer.getLabelName()),
                liveCount);
        }

        void logFreeQueued(VKDevice *device, const VKTexture &texture, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_texture_queued label={} live_textures={}",
                safeLogLabel(texture.getLabelName()),
                liveCount);
        }

        void logFreeQueued(VKDevice *device, const VKSampler &sampler, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_sampler_queued label={} live_samplers={}",
                safeLogLabel(sampler.getLabelName()),
                liveCount);
        }

        // Runs the shared user-destroy -> untrack -> retire-manager enqueue flow for one Vulkan resource handle type.
        template <typename Handle, typename Impl>
        void freeTrackedResource(
            VKDevice *device,
            Handle handle,
            vk::Device nativeDevice,
            VmaAllocator allocator,
            bool requiresAllocator,
            Mutex &resourceLifetimeMutex,
            eastl::vector<Handle> &liveHandles,
            VKRetireManager &retireManager,
            const char *invalidHandleMessage,
            const char *deviceGoneEvent)
        {
            if (handle.isNull())
            {
                return;
            }

            auto *resourceImpl = static_cast<Impl *>(handle.get());
            if (resourceImpl == nullptr)
            {
                throw std::invalid_argument(invalidHandleMessage);
            }

            ScopedLock lock(resourceLifetimeMutex);
            if (!nativeDevice || (requiresAllocator && allocator == nullptr))
            {
                GVMLogWarn(
                    device, DeviceLogCategory,
                    "event={} label={}",
                    deviceGoneEvent,
                    safeLogLabel(resourceImpl->getLabelName()));
                return;
            }

            const VKUserReleaseResult releaseResult = resourceImpl->releaseUserReference();
            if (releaseResult == VKUserReleaseResult::AlreadyReleased)
            {
                logFreeAlreadyRequested(device, *resourceImpl);
                return;
            }

            if (releaseResult == VKUserReleaseResult::LastReference)
            {
                retireManager.enqueue(handle);
            }
            logFreeQueued(device, *resourceImpl, liveHandles.size());
        }
    } // namespace

    VKResourceRegistry::VKResourceRegistry(VKDevice &device)
        : mDevice(device)
    {
    }

    void VKResourceRegistry::init()
    {
        mRetireManager.reset();
        mBufferPool.init();
        mTexturePool.init();
        mTextureViewPool.init();
        mSamplerPool.init();
        mDestroyed = false;
    }

    Buffer VKResourceRegistry::adoptBuffer(VKBuffer &bufferImpl)
    {
        Buffer handle = mBufferPool.alloc(&bufferImpl);
        bufferImpl.setHandle(handle);
        trackBuffer(handle);
        return handle;
    }

    Texture VKResourceRegistry::adoptTexture(VKTexture &textureImpl)
    {
        Texture handle = mTexturePool.alloc(&textureImpl);
        textureImpl.setHandle(handle);
        trackTexture(handle);
        return handle;
    }

    Sampler VKResourceRegistry::adoptSampler(VKSampler &samplerImpl)
    {
        Sampler handle = mSamplerPool.alloc(&samplerImpl);
        samplerImpl.setHandle(handle);
        trackSampler(handle);
        return handle;
    }

    void VKResourceRegistry::freeBuffer(Buffer buffer)
    {
        freeTrackedResource<Buffer, VKBuffer>(
            &mDevice,
            buffer,
            mDevice.getNativeDevice(),
            mDevice.getAllocator(),
            true,
            mResourceLifetimeMutex,
            mLiveBuffers,
            mRetireManager,
            "VKDevice::freeBuffer received a non-Vulkan buffer handle.",
            "device_free_buffer_skip_device_gone");
    }

    void VKResourceRegistry::freeTexture(Texture texture)
    {
        freeTrackedResource<Texture, VKTexture>(
            &mDevice,
            texture,
            mDevice.getNativeDevice(),
            mDevice.getAllocator(),
            true,
            mResourceLifetimeMutex,
            mLiveTextures,
            mRetireManager,
            "VKDevice::freeTexture received a non-Vulkan texture handle.",
            "device_free_texture_skip_device_gone");
    }

    void VKResourceRegistry::freeSampler(Sampler sampler)
    {
        freeTrackedResource<Sampler, VKSampler>(
            &mDevice,
            sampler,
            mDevice.getNativeDevice(),
            mDevice.getAllocator(),
            false,
            mResourceLifetimeMutex,
            mLiveSamplers,
            mRetireManager,
            "VKDevice::freeSampler received a non-Vulkan sampler handle.",
            "device_free_sampler_skip_device_gone");
    }

    void VKResourceRegistry::enqueueRetiredBuffer(Buffer buffer)
    {
        mRetireManager.enqueue(buffer);
    }

    void VKResourceRegistry::enqueueRetiredTexture(Texture texture)
    {
        mRetireManager.enqueue(texture);
    }

    void VKResourceRegistry::enqueueRetiredSampler(Sampler sampler)
    {
        mRetireManager.enqueue(sampler);
    }

    void VKResourceRegistry::collectReleasedResources()
    {
        const bool forceRelease = mDestroyed;
        if (mRetireManager.isEmpty())
        {
            return;
        }

        const auto collectStartTime = std::chrono::steady_clock::now();
        VKRetireReadyBatch readyBatch = mRetireManager.takeReady();
        const size_t releasedSamplerCount = releaseCollectedHandles<Sampler, VKSampler>(
            this, mResourceLifetimeMutex, readyBatch.samplers, mLiveSamplers, &VKResourceRegistry::releaseSamplerHandle);
        const size_t releasedTextureCount = releaseCollectedHandles<Texture, VKTexture>(
            this, mResourceLifetimeMutex, readyBatch.textures, mLiveTextures, &VKResourceRegistry::releaseTextureHandle);
        const size_t releasedBufferCount = releaseCollectedHandles<Buffer, VKBuffer>(
            this, mResourceLifetimeMutex, readyBatch.buffers, mLiveBuffers, &VKResourceRegistry::releaseBufferHandle);

        const size_t totalReleasedCount = releasedSamplerCount + releasedTextureCount + releasedBufferCount;
        const bool madeProgress = totalReleasedCount != 0u;

        if (forceRelease || madeProgress)
        {
            GVMLogDebug(
                &mDevice, DeviceLogCategory,
                "event=collect_released_resources_end force_release={} released_samplers={} released_textures={} released_buffers={}",
                forceRelease,
                releasedSamplerCount,
                releasedTextureCount,
                releasedBufferCount);
        }

        GVMLogTrace(
            &mDevice, DeviceLogCategory,
            "event=collect_released_resources_trace force_release={} made_progress={} duration_ms={:.3f} released_samplers={} released_textures={} released_buffers={}",
            forceRelease,
            madeProgress,
            durationMilliseconds(collectStartTime),
            releasedSamplerCount,
            releasedTextureCount,
            releasedBufferCount);
    }

    void VKResourceRegistry::destroyTextureViewHandle(TextureView view)
    {
        if (view.isNull())
        {
            return;
        }

        if (auto *viewImpl = static_cast<VKTextureView *>(view.get()); viewImpl != nullptr)
        {
            mDevice.getRenderPassFramebufferCache().invalidateFramebufferCacheForImageView(viewImpl->getNativeImageView());
            viewImpl->destroy();
            GVMLogDebug(
                &mDevice, DeviceLogCategory,
                "event=device_destroy_texture_view_end label={} image_view_ptr={}",
                safeLogLabel(viewImpl->getLabelName()),
                reinterpret_cast<void *>(static_cast<VkImageView>(viewImpl->getNativeImageView())));
        }
        mTextureViewPool.freeByHandle(view);
    }

    TextureViewPool &VKResourceRegistry::getTextureViewPool()
    {
        return mTextureViewPool;
    }

    DiagnosticsResourceSnapshot VKResourceRegistry::snapshot() const
    {
        DiagnosticsResourceSnapshot snapshot = {};
        ScopedLock lock(mResourceLifetimeMutex);
        snapshot.entries.reserve(mLiveBuffers.size() + mLiveTextures.size());
        for (Buffer buffer : mLiveBuffers)
        {
            auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
            if (bufferImpl == nullptr || bufferImpl->isDestroyRequested())
            {
                continue;
            }
            appendBufferSnapshotEntry(snapshot, buffer);
        }
        for (Texture texture : mLiveTextures)
        {
            auto *textureImpl = static_cast<VKTexture *>(texture.get());
            if (textureImpl == nullptr || textureImpl->isDestroyRequested())
            {
                continue;
            }
            appendTextureSnapshotEntry(snapshot, texture);
        }
        return snapshot;
    }

    void VKResourceRegistry::releaseAll()
    {
        eastl::vector<Sampler> liveSamplers;
        eastl::vector<Texture> liveTextures;
        eastl::vector<Buffer> liveBuffers;
        {
            ScopedLock lock(mResourceLifetimeMutex);
            liveSamplers.swap(mLiveSamplers);
            liveTextures.swap(mLiveTextures);
            liveBuffers.swap(mLiveBuffers);
        }
        for (Sampler sampler : liveSamplers)
        {
            releaseSamplerHandle(sampler);
        }
        for (Texture texture : liveTextures)
        {
            releaseTextureHandle(texture);
        }
        for (Buffer buffer : liveBuffers)
        {
            releaseBufferHandle(buffer);
        }
        mRetireManager.reset();
    }

    bool VKResourceRegistry::beginDestroy()
    {
        ScopedLock lock(mResourceLifetimeMutex);
        if (mDestroyed)
        {
            return true;
        }
        // Publish shutdown before tearing down allocator-backed resources so cross-thread
        // retirement requests stop trying to retire buffers against a partially destroyed
        // Vulkan device/allocator.
        mDestroyed = true;
        return false;
    }

    bool VKResourceRegistry::isDestroyed() const
    {
        return mDestroyed;
    }

    void VKResourceRegistry::trackBuffer(Buffer buffer)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveBuffers.push_back(buffer);
    }

    void VKResourceRegistry::trackTexture(Texture texture)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveTextures.push_back(texture);
    }

    void VKResourceRegistry::trackSampler(Sampler sampler)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveSamplers.push_back(sampler);
    }

    void VKResourceRegistry::releaseBufferHandle(Buffer buffer)
    {
        if (buffer.isNull())
        {
            return;
        }
        if (auto *bufferImpl = static_cast<VKBuffer *>(buffer.get()); bufferImpl != nullptr)
        {
            if (mDevice.getMainQueueImpl() != nullptr)
            {
                mDevice.getMainQueueImpl()->getResourceStateDB().discardBufferState(*bufferImpl);
            }
            bufferImpl->destroy();
        }
        mBufferPool.freeByHandle(buffer);
    }

    void VKResourceRegistry::releaseTextureHandle(Texture texture)
    {
        if (texture.isNull())
        {
            return;
        }
        if (auto *textureImpl = static_cast<VKTexture *>(texture.get()); textureImpl != nullptr)
        {
            if (mDevice.getMainQueueImpl() != nullptr)
            {
                mDevice.getMainQueueImpl()->getResourceStateDB().discardTextureState(*textureImpl);
            }
            textureImpl->destroy();
        }
        mTexturePool.freeByHandle(texture);
    }

    void VKResourceRegistry::releaseSamplerHandle(Sampler sampler)
    {
        if (sampler.isNull())
        {
            return;
        }
        if (auto *samplerImpl = static_cast<VKSampler *>(sampler.get()); samplerImpl != nullptr)
        {
            samplerImpl->destroy();
        }
        mSamplerPool.freeByHandle(sampler);
    }
} // namespace GVM::RHI::Vulkan
