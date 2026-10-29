#pragma once

#include "VKCommon.hpp"
#include "VKRetireManager.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKDevice;
    class VKBuffer;
    class VKTexture;
    class VKSampler;

    /** Owns the Vulkan resource pools, live-handle tracking, and retire-manager drain path
        that VKDevice previously kept inline. A VKDevice back-reference supplies logging,
        main-queue state-DB discard, and framebuffer-cache invalidation for released views. */
    class VKResourceRegistry final
    {
    public:
        /** Binds the registry to its owning device. */
        explicit VKResourceRegistry(VKDevice &device);

        /** Initializes the resource pools and clears the retire queue and destroyed flag. */
        void init();

        /** Publishes a fully initialized buffer implementation into the pool and live list. */
        Buffer adoptBuffer(VKBuffer &bufferImpl);
        /** Publishes a fully initialized texture implementation into the pool and live list. */
        Texture adoptTexture(VKTexture &textureImpl);
        /** Publishes a fully initialized sampler implementation into the pool and live list. */
        Sampler adoptSampler(VKSampler &samplerImpl);

        /** Releases the user reference on one tracked buffer, queueing retirement on the last reference. */
        void freeBuffer(Buffer buffer);
        /** Releases the user reference on one tracked texture, queueing retirement on the last reference. */
        void freeTexture(Texture texture);
        /** Releases the user reference on one tracked sampler, queueing retirement on the last reference. */
        void freeSampler(Sampler sampler);

        /** Enqueues a user-released buffer whose last holder reference was dropped; the retire manager drains it on the next collection. */
        void enqueueRetiredBuffer(Buffer buffer);
        /** Enqueues a user-released texture whose last holder reference was dropped; the retire manager drains it on the next collection. */
        void enqueueRetiredTexture(Texture texture);
        /** Enqueues a user-released sampler whose last holder reference was dropped; the retire manager drains it on the next collection. */
        void enqueueRetiredSampler(Sampler sampler);

        /** Drains every retired handle whose lifetime reached zero and destroys its implementation. */
        void collectReleasedResources();
        /** Destroys one texture view implementation and drops it from the view pool. */
        void destroyTextureViewHandle(TextureView view);
        /** Returns the texture-view pool used by texture view creation and destruction. */
        [[nodiscard]] TextureViewPool &getTextureViewPool();
        /** Returns a snapshot of live buffers and textures for runtime diagnostics display. */
        [[nodiscard]] DiagnosticsResourceSnapshot snapshot() const;
        /** Destroys every still-tracked handle and resets the retire manager during device teardown. */
        void releaseAll();

        /** Publishes the destroyed state under the lifetime mutex; returns true when already destroyed so the caller exits early. */
        bool beginDestroy();
        /** Reports whether device teardown was published by beginDestroy. */
        [[nodiscard]] bool isDestroyed() const;

    private:
        void trackBuffer(Buffer buffer);
        void trackTexture(Texture texture);
        void trackSampler(Sampler sampler);
        void releaseBufferHandle(Buffer buffer);
        void releaseTextureHandle(Texture texture);
        void releaseSamplerHandle(Sampler sampler);

        VKDevice &mDevice;
        BufferPool mBufferPool;
        TexturePool mTexturePool;
        TextureViewPool mTextureViewPool;
        SamplerPool mSamplerPool;
        eastl::vector<Buffer> mLiveBuffers;
        eastl::vector<Texture> mLiveTextures;
        eastl::vector<Sampler> mLiveSamplers;
        VKRetireManager mRetireManager;
        mutable Mutex mResourceLifetimeMutex;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
