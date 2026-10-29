#pragma once
#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include <future>
namespace GVM::RHI::Metal
{

    class MSwapchain final : public SwapchainImpl
    {
    public:
        MSwapchain();
        void init(MDevice *device, const SwapchainDescriptor &descriptor);
        MultipleElements<TextureFormat> getSupportedFormats() const override;
        TextureFormat getPreferredFormat() const override;
        virtual SwapchainQueryResult queryNextTexture() override;
        virtual void present() override;
        virtual void destroy() override;

    private:
        /** Recreates the fallback texture at the given size; used only when the native swapchain
            cannot produce a drawable for this acquire. */
        void recreateFallbackTexture(uint32_t w, uint32_t h);
        /** Releases the drawable texture wrapper and the held native texture reference. */
        void releaseDrawableTexture();
        CA::MetalLayer *mNativeSwapchain = nullptr;

        /** Fallback texture used when nextDrawable yields nil; freed when a real drawable is acquired. */
        Texture mFallbackTexture;
        /** Acquired drawable, retained from acquire until present commits it; empty for fallback frames. */
        NS::SharedPtr<CA::MetalDrawable> mAcquiredDrawable;
        /** The acquired drawable's native texture retained for the wrapper's lifetime. */
        NS::SharedPtr<MTL::Texture> mAcquiredNativeTexture;
        /** GVM texture wrapping the acquired drawable; released on the next acquire. */
        Texture mDrawableTexture;
        /** True once queryNextTexture has handed out a texture that has not yet been presented. */
        bool mHasOutstandingAcquire = false;

        MDevice *mDevice = nullptr;
        SwapchainQueryResult mCurrentResult;
        TextureFormat mPreferredFormat = TextureFormat::BGRA8Unorm;

        dispatch_group_t mPresentCompletionGroup = nullptr;
    };

} // namespace GVM::RHI::Metal
