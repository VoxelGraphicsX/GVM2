#include "VKRetireManager.hpp"

#include "VKBuffer.hpp"
#include "VKSampler.hpp"
#include "VKTexture.hpp"

namespace GVM::RHI::Vulkan
{
    namespace
    {
        template <typename Handle, typename Impl>
        void collectReadyHandles(eastl::vector<Handle> &pendingHandles, eastl::vector<Handle> &readyHandles, bool forceRelease)
        {
            size_t writeIndex = 0u;
            for (size_t readIndex = 0u; readIndex < pendingHandles.size(); ++readIndex)
            {
                Handle &handle = pendingHandles[readIndex];
                auto *impl = static_cast<Impl *>(handle.get());
                if (forceRelease || (impl != nullptr && impl->isReadyForDestroy()))
                {
                    if (impl != nullptr)
                    {
                        impl->clearPendingDestroyQueued();
                    }
                    readyHandles.push_back(eastl::move(handle));
                    continue;
                }

                if (writeIndex != readIndex)
                {
                    pendingHandles[writeIndex] = eastl::move(handle);
                }
                ++writeIndex;
            }
            pendingHandles.erase(pendingHandles.begin() + writeIndex, pendingHandles.end());
        }
    } // namespace

    void VKRetireManager::reset()
    {
        ScopedLock lock(mMutex);
        mPendingDestroyBuffers.clear();
        mPendingDestroyTextures.clear();
        mPendingDestroySamplers.clear();
        mPendingBindGroupBufferReferenceReleases.clear();
        mPendingBindGroupTextureReferenceReleases.clear();
        mPendingBindGroupSamplerReferenceReleases.clear();
        mPendingResourceCount.store(0u, std::memory_order_relaxed);
        mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
    }

    bool VKRetireManager::shouldCollect(bool forceRelease, uint64_t currentTimeUs) const
    {
        if (forceRelease)
        {
            return true;
        }
        if (mPendingResourceCount.load(std::memory_order_relaxed) == 0u)
        {
            return false;
        }

        const uint64_t nextAttemptUs = mNextCollectionAttemptUs.load(std::memory_order_relaxed);
        return nextAttemptUs == 0u || currentTimeUs >= nextAttemptUs;
    }

    size_t VKRetireManager::getPendingDestroyBufferCount() const
    {
        ScopedLock lock(mMutex);
        return mPendingDestroyBuffers.size();
    }

    size_t VKRetireManager::getPendingDestroyTextureCount() const
    {
        ScopedLock lock(mMutex);
        return mPendingDestroyTextures.size();
    }

    size_t VKRetireManager::getPendingDestroySamplerCount() const
    {
        ScopedLock lock(mMutex);
        return mPendingDestroySamplers.size();
    }

    Texture VKRetireManager::findPendingTextureHandle(const VKTexture *texture) const
    {
        if (texture == nullptr)
        {
            return {};
        }

        ScopedLock lock(mMutex);
        for (const Texture &handle : mPendingDestroyTextures)
        {
            if (handle.get() == texture)
            {
                return handle;
            }
        }
        return {};
    }

    void VKRetireManager::enqueueBuffer(Buffer buffer)
    {
        auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
        if (bufferImpl == nullptr || !bufferImpl->markPendingDestroyQueued())
        {
            return;
        }

        ScopedLock lock(mMutex);
        mPendingDestroyBuffers.push_back(buffer);
        mPendingResourceCount.fetch_add(1u, std::memory_order_relaxed);
        mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
    }

    void VKRetireManager::enqueueTexture(Texture texture)
    {
        auto *textureImpl = static_cast<VKTexture *>(texture.get());
        if (textureImpl == nullptr || !textureImpl->markPendingDestroyQueued())
        {
            return;
        }

        ScopedLock lock(mMutex);
        mPendingDestroyTextures.push_back(texture);
        mPendingResourceCount.fetch_add(1u, std::memory_order_relaxed);
        mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
    }

    void VKRetireManager::enqueueSampler(Sampler sampler)
    {
        auto *samplerImpl = static_cast<VKSampler *>(sampler.get());
        if (samplerImpl == nullptr || !samplerImpl->markPendingDestroyQueued())
        {
            return;
        }

        ScopedLock lock(mMutex);
        mPendingDestroySamplers.push_back(sampler);
        mPendingResourceCount.fetch_add(1u, std::memory_order_relaxed);
        mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
    }

    void VKRetireManager::enqueueBindGroupReferenceReleases(
        eastl::vector<VKBuffer *> buffers,
        eastl::vector<VKTexture *> textures,
        eastl::vector<VKSampler *> samplers)
    {
        const uint32_t queuedReleaseCount = static_cast<uint32_t>(buffers.size() + textures.size() + samplers.size());
        if (queuedReleaseCount == 0u)
        {
            return;
        }

        ScopedLock lock(mMutex);
        mPendingBindGroupBufferReferenceReleases.insert(
            mPendingBindGroupBufferReferenceReleases.end(),
            buffers.begin(),
            buffers.end());
        mPendingBindGroupTextureReferenceReleases.insert(
            mPendingBindGroupTextureReferenceReleases.end(),
            textures.begin(),
            textures.end());
        mPendingBindGroupSamplerReferenceReleases.insert(
            mPendingBindGroupSamplerReferenceReleases.end(),
            samplers.begin(),
            samplers.end());
        mPendingResourceCount.fetch_add(queuedReleaseCount, std::memory_order_relaxed);
        mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
    }

    VKRetireReadyBatch VKRetireManager::collectReady(bool forceRelease)
    {
        VKRetireReadyBatch readyBatch;
        ScopedLock lock(mMutex);

        readyBatch.bindGroupBufferReferenceReleases = eastl::move(mPendingBindGroupBufferReferenceReleases);
        readyBatch.bindGroupTextureReferenceReleases = eastl::move(mPendingBindGroupTextureReferenceReleases);
        readyBatch.bindGroupSamplerReferenceReleases = eastl::move(mPendingBindGroupSamplerReferenceReleases);
        mPendingBindGroupBufferReferenceReleases.clear();
        mPendingBindGroupTextureReferenceReleases.clear();
        mPendingBindGroupSamplerReferenceReleases.clear();

        collectReadyHandles<Sampler, VKSampler>(mPendingDestroySamplers, readyBatch.samplers, forceRelease);
        collectReadyHandles<Texture, VKTexture>(mPendingDestroyTextures, readyBatch.textures, forceRelease);
        collectReadyHandles<Buffer, VKBuffer>(mPendingDestroyBuffers, readyBatch.buffers, forceRelease);

        readyBatch.pendingAfterCollect = refreshPendingResourceCountLocked();
        return readyBatch;
    }

    void VKRetireManager::finishCollection(bool forceRelease, bool madeProgress, uint64_t currentTimeUs)
    {
        if (forceRelease)
        {
            return;
        }

        ScopedLock lock(mMutex);
        const uint32_t pendingResourceCount = mPendingResourceCount.load(std::memory_order_relaxed);
        if (pendingResourceCount == 0u || madeProgress)
        {
            mNextCollectionAttemptUs.store(0u, std::memory_order_relaxed);
            return;
        }

        mNextCollectionAttemptUs.store(currentTimeUs + CollectionRetryIntervalUs, std::memory_order_relaxed);
    }

    uint32_t VKRetireManager::refreshPendingResourceCountLocked()
    {
        const uint32_t pendingResourceCount = static_cast<uint32_t>(
            mPendingBindGroupBufferReferenceReleases.size() +
            mPendingBindGroupTextureReferenceReleases.size() +
            mPendingBindGroupSamplerReferenceReleases.size() +
            mPendingDestroySamplers.size() +
            mPendingDestroyTextures.size() +
            mPendingDestroyBuffers.size());
        mPendingResourceCount.store(pendingResourceCount, std::memory_order_relaxed);
        return pendingResourceCount;
    }
} // namespace GVM::RHI::Vulkan
