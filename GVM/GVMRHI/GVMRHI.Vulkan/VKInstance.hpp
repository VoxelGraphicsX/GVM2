#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/shared_ptr.h>

namespace GVM::RHI::Vulkan
{
    class VKInstance final : public InstanceImpl
    {
    public:
        VKInstance();

        void init(const InstanceDescriptor &descriptor = {});
        GraphicsBackend getBackend() const override;

        Device createDevice() override;
        Swapchain createSwapchain(const SwapchainDescriptor &descriptor) override;
        void setLoggingConfig(const LoggingConfig &config) override;
        LoggingConfig getLoggingConfig() const override;
        Logger getLogger() const override;
        void destroy() override;

        vk::Instance getNativeInstance() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;
        /// Returns the descriptor used to initialize this Vulkan instance.
        const InstanceDescriptor &getDescriptor() const;

    private:
        vk::UniqueInstance mInstance;
        vk::UniqueDebugUtilsMessengerEXT mDebugMessenger;
        Device mDevice = nullptr;
        Swapchain mSwapchain = nullptr;
        bool mDestroyed = false;
        InstanceDescriptor mDescriptor = {};
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        Logger mLogger;
    };
} // namespace GVM::RHI::Vulkan
