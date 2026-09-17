#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <chrono>

namespace GVM::RHI::Vulkan
{
    class VKSwapchain final : public SwapchainImpl
    {
    public:
        VKSwapchain() = default;

        struct SwapchainImageState
        {
            vk::UniqueSemaphore acquireSemaphore;
            vk::UniqueSemaphore renderCompleteSemaphore;
            vk::ImageLayout layout = vk::ImageLayout::eUndefined;
            vk::PipelineStageFlags stageMask = {};
            vk::AccessFlags accessMask = {};
            uint64_t renderCompleteGeneration = 0u;
            uint64_t lastSignalSubmissionId = 0u;
            uint64_t lastPresentedSubmissionId = 0u;
            uint64_t lastAcquireGeneration = 0u;
            uint64_t lastPresentSequence = 0u;
            uint32_t lastAcquireFrameSlot = 0u;
        };

        void init(VKDevice *device, const SwapchainDescriptor &descriptor);
        MultipleElements<TextureFormat> getSupportedFormats() const override;
        TextureFormat getPreferredFormat() const override;
        SwapchainQueryResult queryNextTexture() override;
        void present() override;
        void destroy() override;
        Logger getLogger() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

    private:
        struct PresentFrame
        {
            vk::CommandBuffer commandBuffer;
            vk::UniqueSemaphore imageAvailableSemaphore;
            VKSubmissionCompletion submissionCompletion;
            uint64_t imageAvailableGeneration = 0u;
            uint64_t lastSubmittedSubmissionId = 0u;
            uint64_t lastPresentSequence = 0u;
            uint32_t lastAcquiredImageIndex = 0u;
        };

        struct SurfaceConfig
        {
            TextureFormat format = TextureFormat::Undefined;
            vk::ColorSpaceKHR colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
            vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
            vk::Extent2D extent = {};
            uint32_t imageCount = 0;
            vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            bool directImageCopySrcSupported = false;
        };

        SurfaceConfig querySurfaceConfig() const;
        void recreateSwapchainIfNeeded();
        void recreateSwapchain(const SurfaceConfig &config);
        void recreatePresentTexture(TextureFormat format, const vk::Extent2D &extent);
        void recreateDirectSwapchainTextures(TextureFormat format, const vk::Extent2D &extent, bool copySrcSupported);
        void destroyDirectSwapchainTextures();
        vk::UniqueSurfaceKHR createSurface(const SwapchainDescriptor &descriptor);
        void recordPresentCopy(vk::CommandBuffer commandBuffer, uint32_t imageIndex);
        uint32_t acquirePresentFrameSlot(bool directPresent);
        SwapchainQueryResult queryNextTextureDirect();
        void presentDirectSwapchainImage();
        void waitForAllPresentFrames();

        VKDevice *mDevice = nullptr;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        VKQueue *mQueue = nullptr;
        SwapchainDescriptor mDescriptor = {};
        vk::UniqueSurfaceKHR mSurface;
        vk::UniqueSwapchainKHR mSwapchain;
        vk::Format mSwapchainFormat = vk::Format::eUndefined;
        TextureFormat mSwapchainTextureFormat = TextureFormat::Undefined;
        vk::Extent2D mSwapchainExtent = {};
        eastl::vector<vk::Image> mSwapchainImages;
        Texture mPresentTexture;
        eastl::vector<Texture> mDirectSwapchainTextures;
        SwapchainQueryResult mCurrentResult = {};
        uint32_t mCurrentImageIndex = 0;
        bool mDestroyed = false;
        mutable bool mRenderDirectToAcquiredSwapchainImage = false;
        bool mHasOutstandingDirectPresentImage = false;

        vk::UniqueCommandPool mPresentCommandPool;
        eastl::vector<PresentFrame> mPresentFrames;
        eastl::vector<SwapchainImageState> mSwapchainImageStates;
        uint32_t mCurrentFrameSlot = 0u;
        uint32_t mNextFrameSlotCursor = 0u;
        uint64_t mPresentSequence = 0u;
    };
} // namespace GVM::RHI::Vulkan
