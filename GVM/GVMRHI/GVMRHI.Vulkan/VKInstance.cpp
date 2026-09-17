#include "VKInstance.hpp"

#include "Private/LoggingCore.hpp"
#include "Private/PublicLoggerAdapter.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKSwapchain.hpp"

#include <cstdlib>
#include <cctype>
#include <cstring>
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

        struct ValidationConfig
        {
            bool enabled = false;
            bool verbose = false;
            bool synchronizationValidation = true;
            bool bestPractices = true;
        };

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

        eastl::string joinValidationFeatureNames(const eastl::vector<vk::ValidationFeatureEnableEXT> &features)
        {
            eastl::string text;
            for (size_t index = 0; index < features.size(); ++index)
            {
                if (index != 0u)
                {
                    text += ",";
                }

                switch (features[index])
                {
                case vk::ValidationFeatureEnableEXT::eSynchronizationValidation:
                    text += "synchronization_validation";
                    break;
                case vk::ValidationFeatureEnableEXT::eBestPractices:
                    text += "best_practices";
                    break;
                default:
                    text += "other";
                    break;
                }
            }
            return text;
        }

        bool parseBoolValue(const char *value, bool defaultValue)
        {
            if (value == nullptr)
            {
                return defaultValue;
            }

            eastl::string normalized;
            normalized.reserve(strlen(value));
            for (const char *cursor = value; *cursor != '\0'; ++cursor)
            {
                normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*cursor))));
            }

            if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
            {
                return true;
            }
            if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
            {
                return false;
            }
            return defaultValue;
        }

        bool readEnvBool(const char *name, bool defaultValue = false)
        {
            return parseBoolValue(std::getenv(name), defaultValue);
        }

        ValidationConfig readValidationConfig()
        {
            const bool validationEnabled = readEnvBool("GVM_VULKAN_VALIDATION") || readEnvBool("GVM_TEST_VULKAN_VALIDATION");
            ValidationConfig config = {};
            config.enabled = validationEnabled;
            if (!validationEnabled)
            {
                return config;
            }

            config.verbose = readEnvBool("GVM_VULKAN_VALIDATION_VERBOSE");
            config.synchronizationValidation = readEnvBool("GVM_VULKAN_VALIDATION_SYNC", true);
            config.bestPractices = readEnvBool("GVM_VULKAN_VALIDATION_BEST_PRACTICES", true);
            return config;
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

            const char *messageIdName = callbackData->pMessageIdName != nullptr ? callbackData->pMessageIdName : "";
            const char *message = callbackData->pMessage != nullptr ? callbackData->pMessage : "";

            if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                GVMLogError(instance, VulkanBackendLogCategory, "event=vulkan_validation severity=error type={} id_name={} id={} message=\"{}\"", describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message);
            }
            else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                GVMLogWarn(instance, VulkanBackendLogCategory, "event=vulkan_validation severity=warning type={} id_name={} id={} message=\"{}\"", describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message);
            }
            else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
            {
                GVMLogInfo(instance, VulkanBackendLogCategory, "event=vulkan_validation severity=info type={} id_name={} id={} message=\"{}\"", describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message);
            }
            else
            {
                GVMLogDebug(instance, VulkanBackendLogCategory, "event=vulkan_validation severity=verbose type={} id_name={} id={} message=\"{}\"", describeValidationMessageType(messageTypes).c_str(), messageIdName, callbackData->messageIdNumber, message);
            }

            return VK_FALSE;
        }

    } // namespace

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
            throw makeRuntimeError("VKInstance::init failed to initialize volk.");
        }

        uint32_t supportedInstanceVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            vkEnumerateInstanceVersion(&supportedInstanceVersion);
        }
        if (supportedInstanceVersion < VulkanApiVersion)
        {
            throw makeRuntimeError(
                "VKInstance::init requires Vulkan API " +
                formatVulkanApiVersion(VulkanApiVersion) +
                ", but the loader only reports " +
                formatVulkanApiVersion(supportedInstanceVersion) + ".");
        }

        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
        const eastl::vector<vk::ExtensionProperties> availableExtensions = toEastlVector(vk::enumerateInstanceExtensionProperties());
        const eastl::vector<vk::LayerProperties> availableLayers = toEastlVector(vk::enumerateInstanceLayerProperties());
        const ValidationConfig validationConfig = readValidationConfig();

        eastl::vector<const char *> enabledExtensions;
        eastl::vector<const char *> enabledLayers;
        vk::InstanceCreateFlags instanceFlags{};

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
        eastl::vector<vk::ValidationFeatureEnableEXT> enabledValidationFeatures;
        vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {};
        vk::ValidationFeaturesEXT validationFeatures = {};
        if (validationConfig.enabled)
        {
            if (!hasInstanceLayer(availableLayers, ValidationLayerName))
            {
                throw makeRuntimeError(
                    eastl::string("VKInstance::init requested Vulkan validation, but the required layer is unavailable: ") +
                    ValidationLayerName +
                    ". Available layers=[" +
                    joinLayerNames(availableLayers) +
                    "]");
            }
            if (!hasInstanceExtension(availableExtensions, DebugUtilsExtensionName))
            {
                throw makeRuntimeError(
                    eastl::string("VKInstance::init requested Vulkan validation, but the required instance extension is unavailable: ") +
                    DebugUtilsExtensionName);
            }

            enabledLayers.push_back(ValidationLayerName);
            enabledExtensions.push_back(DebugUtilsExtensionName);
            if (hasInstanceExtension(availableExtensions, ValidationFeaturesExtensionName))
            {
                enabledExtensions.push_back(ValidationFeaturesExtensionName);
                if (validationConfig.synchronizationValidation)
                {
                    enabledValidationFeatures.push_back(vk::ValidationFeatureEnableEXT::eSynchronizationValidation);
                }
                if (validationConfig.bestPractices)
                {
                    enabledValidationFeatures.push_back(vk::ValidationFeatureEnableEXT::eBestPractices);
                }
            }
            else if (validationConfig.synchronizationValidation || validationConfig.bestPractices)
            {
                GVMLogWarn(
                    this, VulkanBackendLogCategory,
                    "event=instance_validation_features_unavailable extension={} sync_requested={} best_practices_requested={}",
                    ValidationFeaturesExtensionName,
                    validationConfig.synchronizationValidation,
                    validationConfig.bestPractices);
            }

            debugMessengerCreateInfo.messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
            if (validationConfig.verbose)
            {
                debugMessengerCreateInfo.messageSeverity |=
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
            }
            debugMessengerCreateInfo.messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
            debugMessengerCreateInfo.pfnUserCallback = handleValidationMessage;
            debugMessengerCreateInfo.pUserData = this;
        }

        vk::ApplicationInfo appInfo = {};
        appInfo.pApplicationName = "GVMRHI";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "GVM";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VulkanApiVersion;

        vk::InstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.flags = instanceFlags;
        instanceCreateInfo.pApplicationInfo = &appInfo;
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = enabledLayers.data();

        if (validationConfig.enabled)
        {
            const void *instanceCreatePNext = nullptr;
            if (!enabledValidationFeatures.empty())
            {
                validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledValidationFeatures.size());
                validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
                validationFeatures.pNext = instanceCreatePNext;
                instanceCreatePNext = &validationFeatures;
            }

            debugMessengerCreateInfo.pNext = instanceCreatePNext;
            instanceCreatePNext = &debugMessengerCreateInfo;
            instanceCreateInfo.pNext = instanceCreatePNext;
        }

        GVMLogInfo(
            this, VulkanBackendLogCategory,
            "event=instance_create enabled_extension_count={} enabled_extensions=[{}] enabled_layer_count={} enabled_layers=[{}]",
            enabledExtensions.size(),
            joinExtensionNames(enabledExtensions).c_str(),
            enabledLayers.size(),
            joinExtensionNames(enabledLayers).c_str());

        mInstance = vk::createInstanceUnique(instanceCreateInfo);
        volkLoadInstance(static_cast<VkInstance>(mInstance.get()));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance.get());
        if (validationConfig.enabled)
        {
            mDebugMessenger = mInstance->createDebugUtilsMessengerEXTUnique(debugMessengerCreateInfo);
            GVMLogInfo(
                this, VulkanBackendLogCategory,
                "event=instance_validation_enabled verbose={} enabled_features=[{}]",
                validationConfig.verbose,
                joinValidationFeatureNames(enabledValidationFeatures).c_str());
        }
        GVMLogInfo(this, VulkanBackendLogCategory, "event=instance_init_end");
    }

    Device VKInstance::createDevice()
    {
        if (mDestroyed)
        {
            throw makeRuntimeError("VKInstance::createDevice was called after the instance was destroyed.");
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
            throw makeRuntimeError("VKInstance::createSwapchain was called after the instance was destroyed.");
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
