#include "VKInstance.hpp"

#include "Private/LoggingCore.hpp"
#include "Private/PublicLoggerAdapter.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKSwapchain.hpp"

#include <cstring>
#include <cstdio>
#include <string>
#include <stdexcept>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
        namespace
    {
        constexpr eastl::string_view VulkanBackendLogCategory = "gvmrhi.vulkan.instance";
    } // namespace

namespace
    {
        constexpr const char *SurfaceExtensionName = "VK_KHR_surface";
        constexpr const char *MetalSurfaceExtensionName = "VK_EXT_metal_surface";
        constexpr const char *Win32SurfaceExtensionName = "VK_KHR_win32_surface";
        constexpr const char *AndroidSurfaceExtensionName = "VK_KHR_android_surface";
        constexpr const char *OhosSurfaceExtensionName = "VK_OHOS_surface";
        constexpr const char *WaylandSurfaceExtensionName = "VK_KHR_wayland_surface";
        constexpr const char *XcbSurfaceExtensionName = "VK_KHR_xcb_surface";
        constexpr const char *XlibSurfaceExtensionName = "VK_KHR_xlib_surface";
        constexpr const char *DebugUtilsExtensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        constexpr const char *ValidationFeaturesExtensionName = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
        constexpr const char *ValidationLayerName = "VK_LAYER_KHRONOS_validation";
        bool gSynchronizationValidationEnabled = false;

        bool hasInstanceExtension(const eastl::vector<vk::ExtensionProperties> &extensions, const char *extensionName)
        {
            for (const vk::ExtensionProperties &extension : extensions)
            {
                if (strcmp(extension.extensionName, extensionName) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        bool hasInstanceLayer(const eastl::vector<vk::LayerProperties> &layers, const char *layerName)
        {
            for (const vk::LayerProperties &layer : layers)
            {
                if (strcmp(layer.layerName, layerName) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        eastl::string joinExtensionNames(const eastl::vector<const char *> &extensions)
        {
            eastl::string text;
            for (size_t index = 0; index < extensions.size(); ++index)
            {
                if (index != 0u)
                {
                    text += ",";
                }
                text += extensions[index];
            }
            return text;
        }

        eastl::string joinLayerNames(const eastl::vector<vk::LayerProperties> &layers)
        {
            eastl::string text;
            for (size_t index = 0; index < layers.size(); ++index)
            {
                if (index != 0u)
                {
                    text += ",";
                }
                text += layers[index].layerName;
            }
            return text;
        }

        /// Formats a Vulkan API version for diagnostics.
        eastl::string formatVulkanApiVersion(uint32_t version)
        {
            return eastl::to_string(VK_API_VERSION_MAJOR(version)) + "." +
                eastl::to_string(VK_API_VERSION_MINOR(version)) + "." +
                eastl::to_string(VK_API_VERSION_PATCH(version));
        }

        eastl::string describeValidationMessageType(VkDebugUtilsMessageTypeFlagsEXT messageTypes)
        {
            eastl::string text;
            auto appendType = [&text](const char *name)
            {
                if (!text.empty())
                {
                    text += "|";
                }
                text += name;
            };

            if ((messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0)
            {
                appendType("general");
            }
            if ((messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0)
            {
                appendType("validation");
            }
            if ((messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0)
            {
                appendType("performance");
            }
#ifdef VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT
            if ((messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) != 0)
            {
                appendType("device_address_binding");
            }
#endif
            if (text.empty())
            {
                text = "unknown";
            }
            return text;
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL handleValidationMessage(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageTypes,
            const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
            void *userData)
        {
            auto *instance = static_cast<VKInstance *>(userData);
            if (instance == nullptr || callbackData == nullptr)
            {
                return VK_FALSE;
            }

            if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                instance->recordValidationError();
            }
            const char *messageIdName = callbackData->pMessageIdName != nullptr ? callbackData->pMessageIdName : "";
            const char *message = callbackData->pMessage != nullptr ? callbackData->pMessage : "";

            const LogLevel level = (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
                ? LogLevel::Error : (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0
                ? LogLevel::Warn : (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0
                ? LogLevel::Info : LogLevel::Debug;
            const char *severity = level == LogLevel::Error ? "error" : level == LogLevel::Warn ? "warning"
                : level == LogLevel::Info ? "info" : "verbose";
#if GVM_LOGGING_ENABLED
            const Logger logger = instance->getLogger();
            if (shouldLog(logger, level))
            {
                logMessage(logger, level, VulkanBackendLogCategory, Logging::formatLogMessage(
                    "event=vulkan_validation severity={} type={} id_name={} id={} message=\"{}\"",
                    severity, describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message));
                return VK_FALSE;
            }
#endif
            // Explicit validation must remain observable when normal engine logging is disabled.
            std::fprintf(stderr, "[gvm] event=vulkan_validation severity=%s type=%s id_name=%s id=%d message=\"%s\"\n",
                severity, describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message);
            return VK_FALSE;
        }

    } // namespace

    void VKInstance::enableSynchronizationValidationForTesting()
    {
        gSynchronizationValidationEnabled = true;
    }

    GraphicsBackend VKInstance::getBackend() const
    {
        return GraphicsBackend::Vulkan;
    }

    VKInstance::VKInstance() = default;

    vk::Instance VKInstance::getNativeInstance() const
    {
        return mInstance.get();
    }

    const eastl::shared_ptr<Internal::LogContext> &VKInstance::getLogContext() const
    {
        return mLogContext;
    }

    const InstanceDescriptor &VKInstance::getDescriptor() const
    {
        return mDescriptor;
    }

    void VKInstance::init(const InstanceDescriptor &descriptor)
    {
        if (mInstance)
        {
            return;
        }

        mDescriptor = descriptor;
        mLogContext = eastl::make_shared<Internal::LogContext>(GraphicsBackend::Vulkan);
        mLogger = Internal::createPublicLogger(mLogContext);
        if (descriptor.hasLoggingConfig != False)
        {
            mLogContext->configure(descriptor.logging);
        }

        GVMLogInfo(this, VulkanBackendLogCategory, "event=instance_init_begin");

        if (volkInitialize() != VK_SUCCESS)
        {
            throw std::runtime_error("VKInstance::init failed to initialize volk.");
        }

        uint32_t supportedInstanceVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            vkEnumerateInstanceVersion(&supportedInstanceVersion);
        }
        if (supportedInstanceVersion < VulkanApiVersion)
        {
            throw std::runtime_error((
                "VKInstance::init requires Vulkan API " +
                formatVulkanApiVersion(VulkanApiVersion) +
                ", but the loader only reports " +
                formatVulkanApiVersion(supportedInstanceVersion) + ".").c_str());
        }

        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        InstanceSelection selection = {};
        selectInstanceLayersAndExtensions(selection);
        createVulkanInstance(selection);
        createDebugMessenger(selection);
        GVMLogInfo(this, VulkanBackendLogCategory, "event=instance_init_end");
    }

    void VKInstance::selectInstanceLayersAndExtensions(InstanceSelection &selection) const
    {
        const eastl::vector<vk::ExtensionProperties> availableExtensions = toEastlVector(vk::enumerateInstanceExtensionProperties());
        const eastl::vector<vk::LayerProperties> availableLayers = toEastlVector(vk::enumerateInstanceLayerProperties());
        const bool validationEnabled = gSynchronizationValidationEnabled;
        selection.validationEnabled = validationEnabled;

        eastl::vector<const char *> &enabledExtensions = selection.enabledExtensions;
        eastl::vector<const char *> &enabledLayers = selection.enabledLayers;
        vk::InstanceCreateFlags &instanceFlags = selection.instanceFlags;

        if (hasInstanceExtension(availableExtensions, SurfaceExtensionName))
        {
            enabledExtensions.push_back(SurfaceExtensionName);
        }
        if (hasInstanceExtension(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            instanceFlags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
        }
        if (hasInstanceExtension(availableExtensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        {
            enabledExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
#if defined(__APPLE__)
        if (hasInstanceExtension(availableExtensions, MetalSurfaceExtensionName))
        {
            enabledExtensions.push_back(MetalSurfaceExtensionName);
        }
#endif
#if defined(_WIN32)
        if (hasInstanceExtension(availableExtensions, Win32SurfaceExtensionName))
        {
            enabledExtensions.push_back(Win32SurfaceExtensionName);
        }
#endif
#if defined(__ANDROID__)
        if (hasInstanceExtension(availableExtensions, AndroidSurfaceExtensionName))
        {
            enabledExtensions.push_back(AndroidSurfaceExtensionName);
        }
#endif
#if defined(__OHOS__)
        if (hasInstanceExtension(availableExtensions, OhosSurfaceExtensionName))
        {
            enabledExtensions.push_back(OhosSurfaceExtensionName);
        }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
        if (hasInstanceExtension(availableExtensions, WaylandSurfaceExtensionName))
        {
            enabledExtensions.push_back(WaylandSurfaceExtensionName);
        }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
        if (hasInstanceExtension(availableExtensions, XcbSurfaceExtensionName))
        {
            enabledExtensions.push_back(XcbSurfaceExtensionName);
        }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        if (hasInstanceExtension(availableExtensions, XlibSurfaceExtensionName))
        {
            enabledExtensions.push_back(XlibSurfaceExtensionName);
        }
#endif
        const bool debugUtilsAvailable = hasInstanceExtension(availableExtensions, DebugUtilsExtensionName);
        if (debugUtilsAvailable)
        {
            enabledExtensions.push_back(DebugUtilsExtensionName);
        }
        selection.debugUtilsEnabled = debugUtilsAvailable;
        vk::DebugUtilsMessengerCreateInfoEXT &debugMessengerCreateInfo = selection.debugMessengerCreateInfo;
        if (validationEnabled)
        {
            if (!hasInstanceLayer(availableLayers, ValidationLayerName))
            {
                throw std::runtime_error((
                    eastl::string("VKInstance::init requested Vulkan validation, but the required layer is unavailable: ") +
                    ValidationLayerName +
                    ". Available layers=[" +
                    joinLayerNames(availableLayers) +
                    "]").c_str());
            }
            if (!hasInstanceExtension(availableExtensions, DebugUtilsExtensionName))
            {
                throw std::runtime_error((
                    eastl::string("VKInstance::init requested Vulkan validation, but the required instance extension is unavailable: ") +
                    DebugUtilsExtensionName).c_str());
            }

            enabledLayers.push_back(ValidationLayerName);
            const auto layerExtensions = toEastlVector(vk::enumerateInstanceExtensionProperties(std::string{ValidationLayerName}));
            if (hasInstanceExtension(availableExtensions, ValidationFeaturesExtensionName) ||
                hasInstanceExtension(layerExtensions, ValidationFeaturesExtensionName))
            {
                enabledExtensions.push_back(ValidationFeaturesExtensionName);
            }
            else
            {
                throw std::runtime_error("Synchronization validation requires VK_EXT_validation_features.");
            }

            debugMessengerCreateInfo.messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
            debugMessengerCreateInfo.messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
            debugMessengerCreateInfo.pfnUserCallback = handleValidationMessage;
        }
    }

    void VKInstance::createVulkanInstance(InstanceSelection &selection)
    {
        vk::ApplicationInfo appInfo = {};
        appInfo.pApplicationName = "GVMRHI";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "GVM";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VulkanApiVersion;

        vk::InstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.flags = selection.instanceFlags;
        instanceCreateInfo.pApplicationInfo = &appInfo;
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(selection.enabledExtensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = selection.enabledExtensions.data();
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(selection.enabledLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = selection.enabledLayers.data();

        if (selection.validationEnabled)
        {
            selection.validationFeatures.enabledValidationFeatureCount = 1;
            selection.validationFeatures.pEnabledValidationFeatures = &selection.validationFeature;
            selection.debugMessengerCreateInfo.pNext = &selection.validationFeatures;
            instanceCreateInfo.pNext = &selection.debugMessengerCreateInfo;
        }

        GVMLogInfo(
            this, VulkanBackendLogCategory,
            "event=instance_create enabled_extension_count={} enabled_extensions=[{}] enabled_layer_count={} enabled_layers=[{}]",
            selection.enabledExtensions.size(),
            joinExtensionNames(selection.enabledExtensions).c_str(),
            selection.enabledLayers.size(),
            joinExtensionNames(selection.enabledLayers).c_str());

        mInstance = vk::createInstanceUnique(instanceCreateInfo);
        mDebugUtilsEnabled = selection.debugUtilsEnabled;
        volkLoadInstance(static_cast<VkInstance>(mInstance.get()));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance.get());
    }

    void VKInstance::createDebugMessenger(InstanceSelection &selection)
    {
        if (selection.validationEnabled)
        {
            selection.debugMessengerCreateInfo.pUserData = this;
            mDebugMessenger = mInstance->createDebugUtilsMessengerEXTUnique(selection.debugMessengerCreateInfo);
            GVMLogInfo(
                this, VulkanBackendLogCategory,
                "event=instance_validation_enabled enabled_features=[synchronization_validation]");
        }
    }

    Device VKInstance::createDevice()
    {
        if (mDestroyed)
        {
            throw std::runtime_error("VKInstance::createDevice was called after the instance was destroyed.");
        }
        if (!mInstance)
        {
            init();
        }
        if (mDevice == nullptr)
        {
            GVMLogInfo(this, VulkanBackendLogCategory, "event=create_device_begin");
            auto *device = new VKDevice();
            try
            {
                device->init(this);
                mDevice = device;
                GVMLogInfo(this, VulkanBackendLogCategory, "event=create_device_end device_created=true");
            }
            catch (...)
            {
                delete device;
                GVMLogError(this, VulkanBackendLogCategory, "event=create_device_end device_created=false");
                throw;
            }
        }
        return mDevice;
    }

    Swapchain VKInstance::createSwapchain(const SwapchainDescriptor &descriptor)
    {
        if (mDestroyed)
        {
            throw std::runtime_error("VKInstance::createSwapchain was called after the instance was destroyed.");
        }
        auto *device = static_cast<VKDevice *>(createDevice());
        if (mSwapchain == nullptr)
        {
            GVMLogInfo(this, VulkanBackendLogCategory, "event=create_swapchain_begin");
            auto *swapchain = new VKSwapchain();
            try
            {
                swapchain->init(device, descriptor);
                mSwapchain = swapchain;
                GVMLogInfo(this, VulkanBackendLogCategory, "event=create_swapchain_end swapchain_created=true");
            }
            catch (...)
            {
                delete swapchain;
                GVMLogError(this, VulkanBackendLogCategory, "event=create_swapchain_end swapchain_created=false");
                throw;
            }
        }
        return mSwapchain;
    }

    void VKInstance::setLoggingConfig(const LoggingConfig &config)
    {
        if (mDestroyed)
        {
            throw std::runtime_error("VKInstance::setLoggingConfig was called after the instance was destroyed.");
        }
        if (!mLogContext)
        {
            mLogContext = eastl::make_shared<Internal::LogContext>(GraphicsBackend::Vulkan);
            mLogger = Internal::createPublicLogger(mLogContext);
        }
        else if (!mLogger)
        {
            mLogger = Internal::createPublicLogger(mLogContext);
        }
        mLogContext->configure(config);
        mDescriptor.hasLoggingConfig = True;
        mDescriptor.logging = config;
    }

    LoggingConfig VKInstance::getLoggingConfig() const
    {
        return mLogContext ? mLogContext->getConfig() : LoggingConfig{};
    }

    Logger VKInstance::getLogger() const
    {
        return mLogger;
    }

    void VKInstance::destroy()
    {
        if (mDestroyed)
        {
            return;
        }

        GVMLogInfo(this, VulkanBackendLogCategory, "event=instance_destroy_begin has_swapchain={} has_device={}", mSwapchain != nullptr, mDevice != nullptr);

        if (mSwapchain != nullptr)
        {
            mSwapchain->destroy();
            delete mSwapchain;
            mSwapchain = nullptr;
        }
        if (mDevice != nullptr)
        {
            mDevice->destroy();
            delete mDevice;
            mDevice = nullptr;
        }
        mDebugMessenger.reset();
        mInstance.reset();
        mDestroyed = true;
        GVMLogInfo(this, VulkanBackendLogCategory, "event=instance_destroy_end");
        if (mLogger != nullptr)
        {
            mLogger->flush();
        }
        mLogger = nullptr;
        mLogContext.reset();
    }
} // namespace GVM::RHI::Vulkan
