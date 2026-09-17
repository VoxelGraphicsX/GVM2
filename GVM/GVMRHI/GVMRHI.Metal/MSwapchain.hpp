#pragma once
#include "MDefines.hpp"
#include "MUtilShaders/MQuadShaderExecutor.hpp"
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
        void createNewTexture(uint32_t w, uint32_t h);
        CA::MetalDrawable *queryTrueSwapchainImage();
        MTL::Texture *mTrueSwapchainTexture = nullptr;
        MTL::Drawable *mCurrentDrawable = nullptr;
        CA::MetalDrawable *mCurrentCADrawble = nullptr;
        CA::MetalLayer *mNativeSwapchain = nullptr;

        Texture mTempTexture;

        MDevice *mDevice = nullptr;
        SwapchainQueryResult mCurrentResult;
        TextureFormat mPreferredFormat = TextureFormat::BGRA8Unorm;

        MQuadShaderExecutor mQuadShaderExecutor = nullptr;
        dispatch_group_t mPresentCompletionGroup = nullptr;

        // std::future<CA::MetalDrawable *> mTrueSwapchainFuture;
    };

} // namespace GVM::RHI::Metal
