#pragma once
#include <EASTL/atomic.h>

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
        /// Returns true when the instance enabled VK_EXT_debug_utils, making object-name and debug-label entry points valid.
        [[nodiscard]] bool supportsDebugUtils() const { return mDebugUtilsEnabled; }

        /** Enables synchronization validation for subsequently created Vulkan instances; must be called before any Vulkan instance is created. Intended for test code. */
        static void enableSynchronizationValidationForTesting();

        /** Counts native validation failures only when the caller explicitly enables backend validation. */
        void recordValidationError() { mValidationErrors.fetch_add(1, eastl::memory_order_relaxed); }
        /** Returns validation failures observed during the instance's lifetime. */
        uint32_t getValidationErrorCount() const { return mValidationErrors.load(eastl::memory_order_relaxed); }

    private:
        /// Carries the extension, layer, and debug-messenger state assembled before vkCreateInstance.
        struct InstanceSelection
        {
            eastl::vector<const char *> enabledExtensions;
            eastl::vector<const char *> enabledLayers;
            vk::InstanceCreateFlags instanceFlags = {};
            vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {};
            vk::ValidationFeaturesEXT validationFeatures = {};
            vk::ValidationFeatureEnableEXT validationFeature = vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
            bool validationEnabled = false;
            bool debugUtilsEnabled = false;
        };

        /** Selects the instance extensions and layers for this platform and, when synchronization
         *  validation is enabled, fills the messenger create-info; throws when validation is unavailable. */
        void selectInstanceLayersAndExtensions(InstanceSelection &selection) const;
        /** Assembles the instance create-info, creates the Vulkan instance, and loads entry points. */
        void createVulkanInstance(InstanceSelection &selection);
        /** Creates the debug messenger when synchronization validation was requested. */
        void createDebugMessenger(InstanceSelection &selection);
        eastl::atomic<uint32_t> mValidationErrors{0};
        vk::UniqueInstance mInstance;
        vk::UniqueDebugUtilsMessengerEXT mDebugMessenger;
        Device mDevice = nullptr;
        Swapchain mSwapchain = nullptr;
        bool mDestroyed = false;
        InstanceDescriptor mDescriptor = {};
        bool mDebugUtilsEnabled = false;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        Logger mLogger;
    };
} // namespace GVM::RHI::Vulkan
