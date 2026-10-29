#include "VKRetireManager.hpp"

namespace GVM::RHI::Vulkan
{
    void VKRetireManager::reset()
    {
        ScopedLock lock(mMutex);
        mBuffers.clear();
        mTextures.clear();
        mSamplers.clear();
        mReadyCount.store(0u, eastl::memory_order_relaxed);
    }

    bool VKRetireManager::isEmpty() const
    {
        return mReadyCount.load(eastl::memory_order_relaxed) == 0u;
    }

    void VKRetireManager::enqueue(Buffer buffer)
    {
        ScopedLock lock(mMutex);
        mBuffers.push_back(buffer);
        mReadyCount.fetch_add(1u, eastl::memory_order_relaxed);
    }

    void VKRetireManager::enqueue(Texture texture)
    {
        ScopedLock lock(mMutex);
        mTextures.push_back(texture);
        mReadyCount.fetch_add(1u, eastl::memory_order_relaxed);
    }

    void VKRetireManager::enqueue(Sampler sampler)
    {
        ScopedLock lock(mMutex);
        mSamplers.push_back(sampler);
        mReadyCount.fetch_add(1u, eastl::memory_order_relaxed);
    }

    VKRetireReadyBatch VKRetireManager::takeReady()
    {
        ScopedLock lock(mMutex);
        VKRetireReadyBatch batch;
        batch.samplers.swap(mSamplers);
        batch.textures.swap(mTextures);
        batch.buffers.swap(mBuffers);
        mReadyCount.store(0u, eastl::memory_order_relaxed);
        return batch;
    }
} // namespace GVM::RHI::Vulkan
