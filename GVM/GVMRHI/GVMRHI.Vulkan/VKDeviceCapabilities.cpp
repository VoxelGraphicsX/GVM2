#include "VKDeviceCapabilities.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"

#include <GVMRHI/GVMLogging.hpp>

#include <cstring>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view DeviceLogCategory = "gvmrhi.vulkan.device";

        uint32_t scorePhysicalDeviceType(vk::PhysicalDeviceType type)
        {
            switch (type)
            {
            case vk::PhysicalDeviceType::eDiscreteGpu: return 500;
            case vk::PhysicalDeviceType::eIntegratedGpu: return 400;
            case vk::PhysicalDeviceType::eVirtualGpu: return 300;
            case vk::PhysicalDeviceType::eCpu: return 200;
            default: return 100;
            }
        }

        /** Infers whether Vulkan device-local memory is also CPU-visible for the selected physical device. */
        DeviceMemoryProperties queryDeviceMemoryProperties(vk::PhysicalDevice physicalDevice)
        {
            const vk::PhysicalDeviceMemoryProperties nativeProperties = physicalDevice.getMemoryProperties();
            bool hasDeviceLocalHostVisibleMemory = false;
            bool hasDeviceLocalHostInvisibleMemory = false;
            for (uint32_t memoryTypeIndex = 0u; memoryTypeIndex < nativeProperties.memoryTypeCount; ++memoryTypeIndex)
            {
                const vk::MemoryPropertyFlags flags = nativeProperties.memoryTypes[memoryTypeIndex].propertyFlags;
                if ((flags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlags{})
                {
                    continue;
                }
                if ((flags & vk::MemoryPropertyFlagBits::eHostVisible) != vk::MemoryPropertyFlags{})
                {
                    hasDeviceLocalHostVisibleMemory = true;
                }
                else
                {
                    hasDeviceLocalHostInvisibleMemory = true;
                }
            }

            DeviceMemoryProperties result = {};
            result.unifiedMemory =
                hasDeviceLocalHostVisibleMemory && !hasDeviceLocalHostInvisibleMemory
                    ? True
                    : False;
            return result;
        }

        bool hasDeviceExtension(const eastl::vector<vk::ExtensionProperties> &extensions, const char *extensionName)
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

        bool supportsRequiredSampledImageDescriptorIndexing(const vk::PhysicalDeviceVulkan12Features &features)
        {
            return features.descriptorIndexing != VK_FALSE &&
                features.shaderSampledImageArrayNonUniformIndexing != VK_FALSE &&
                features.runtimeDescriptorArray != VK_FALSE;
        }

        VulkanCapabilitySurvey queryVulkanCapabilitySurvey(
            vk::PhysicalDevice physicalDevice,
            const eastl::vector<vk::ExtensionProperties> &availableExtensions)
        {
            VulkanCapabilitySurvey survey = {};
            vk::PhysicalDeviceVulkan12Features supportedVulkan12Features = {};
            vk::PhysicalDeviceVulkan13Features supportedVulkan13Features = {};
            vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR supportedBarycentricFeatures = {};
            vk::PhysicalDeviceRobustness2FeaturesEXT supportedRobustness2Features = {};
            vk::PhysicalDeviceFeatures2 supportedFeatures2 = {};
            supportedVulkan12Features.pNext = &supportedVulkan13Features;
            supportedVulkan13Features.pNext = &survey.storage16BitFeatures;
            survey.storage16BitFeatures.pNext = &supportedBarycentricFeatures;
            supportedBarycentricFeatures.pNext = &supportedRobustness2Features;
            supportedFeatures2.pNext = &supportedVulkan12Features;
            physicalDevice.getFeatures2(&supportedFeatures2);
            survey.storage16BitFeatures.pNext = nullptr;
            survey.supportsRequiredVulkan13Features =
                supportedVulkan12Features.bufferDeviceAddress &&
                supportedVulkan13Features.synchronization2 &&
                supportedVulkan12Features.timelineSemaphore &&
                supportedVulkan12Features.descriptorBindingSampledImageUpdateAfterBind &&
                supportedVulkan12Features.descriptorBindingStorageBufferUpdateAfterBind &&
                supportedVulkan12Features.descriptorBindingPartiallyBound;

            vk::PhysicalDeviceVulkan12Properties indexingProperties = {};
            vk::PhysicalDeviceProperties2 properties2 = {};
            properties2.pNext = &indexingProperties;
            physicalDevice.getProperties2(&properties2);
            const vk::PhysicalDeviceProperties &properties = properties2.properties;
            survey.indexingProperties = indexingProperties;
            survey.indexingProperties.pNext = nullptr;
            survey.supportsStorageImageArrayDynamicIndexing = supportedFeatures2.features.shaderStorageImageArrayDynamicIndexing;
            survey.supportsStorageImageArrayNonUniformIndexing = supportedVulkan12Features.shaderStorageImageArrayNonUniformIndexing;
            survey.supportsStorageImageUpdateAfterBind = supportedVulkan12Features.descriptorBindingStorageImageUpdateAfterBind;
            const bool supportsRobustUpdateAfterBind = indexingProperties.robustBufferAccessUpdateAfterBind != VK_FALSE;
            survey.supportsRobustBufferAccess = supportedFeatures2.features.robustBufferAccess != VK_FALSE;
            // Keep the existing buffer-robustness contract when enabling update-after-bind.
            survey.supportsRequiredVulkan13Features &= !survey.supportsRobustBufferAccess || supportsRobustUpdateAfterBind;
            survey.supportsRobustBufferAccess2 =
                supportsRobustUpdateAfterBind && hasDeviceExtension(availableExtensions, Robustness2ExtensionName) &&
                supportedRobustness2Features.robustBufferAccess2 != VK_FALSE;
            survey.supportsSamplerAnisotropy = supportedFeatures2.features.samplerAnisotropy != VK_FALSE;
            survey.supportsIndependentBlend = supportedFeatures2.features.independentBlend != VK_FALSE;
            survey.supportsStorageImageExtendedFormats = supportedFeatures2.features.shaderStorageImageExtendedFormats != VK_FALSE;
            survey.supportsImageGatherExtended = supportedFeatures2.features.shaderImageGatherExtended != VK_FALSE;
            survey.supportsMultiDrawIndirect = supportedFeatures2.features.multiDrawIndirect != VK_FALSE;
            survey.supportsDrawIndirectCount = supportedVulkan12Features.drawIndirectCount != VK_FALSE;
            survey.supportsDrawIndirectFirstInstance = supportedFeatures2.features.drawIndirectFirstInstance != VK_FALSE;
            survey.supportsShaderClipDistance = supportedFeatures2.features.shaderClipDistance != VK_FALSE;
            survey.supportsVertexPipelineStoresAndAtomics = supportedFeatures2.features.vertexPipelineStoresAndAtomics != VK_FALSE;
            survey.supportsFragmentStoresAndAtomics = supportedFeatures2.features.fragmentStoresAndAtomics != VK_FALSE;
            survey.supportsTessellationShaderFeature = supportedFeatures2.features.tessellationShader != VK_FALSE;
            survey.supportsFragmentShaderBarycentric =
                hasDeviceExtension(availableExtensions, FragmentShaderBarycentricExtensionName) &&
                supportedBarycentricFeatures.fragmentShaderBarycentric != VK_FALSE;
            survey.maxSamplerAnisotropy = properties.limits.maxSamplerAnisotropy;

            if (supportedFeatures2.features.shaderSampledImageArrayDynamicIndexing != VK_FALSE)
            {
                if (supportsRequiredSampledImageDescriptorIndexing(supportedVulkan12Features))
                {
                    survey.supportsRequiredSampledImageDescriptorIndexing = true;
                }
            }

            if (survey.supportsRequiredSampledImageDescriptorIndexing)
            {
                survey.supportsShaderFloat16 = supportedVulkan12Features.shaderFloat16 != VK_FALSE;
                survey.supportsShaderSubgroupExtendedTypes = supportedVulkan12Features.shaderSubgroupExtendedTypes != VK_FALSE;
            }

            return survey;
        }
    } // namespace

    const char *physicalDeviceTypeName(vk::PhysicalDeviceType type)
    {
        switch (type)
        {
        case vk::PhysicalDeviceType::eDiscreteGpu: return "DiscreteGpu";
        case vk::PhysicalDeviceType::eIntegratedGpu: return "IntegratedGpu";
        case vk::PhysicalDeviceType::eVirtualGpu: return "VirtualGpu";
        case vk::PhysicalDeviceType::eCpu: return "Cpu";
        default: return "Other";
        }
    }

    const char *descriptorIndexingPathName(const VulkanCapabilitySurvey &survey)
    {
        return survey.supportsRequiredSampledImageDescriptorIndexing ? "core" : "unavailable";
    }

    const char *shaderFloat16PathName(const VulkanCapabilitySurvey &survey)
    {
        return survey.supportsShaderFloat16 ? "core" : "unavailable";
    }

    eastl::string joinDeviceExtensionNames(const eastl::vector<const char *> &extensions)
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

    bool VKDeviceCapabilities::hasSelectedDeviceExtension(const char *extensionName) const
    {
        return hasDeviceExtension(mSelectedDeviceExtensions, extensionName);
    }

    void VKDeviceCapabilities::init(vk::Instance instance, VKDevice *logSubject)
    {
        const eastl::vector<vk::PhysicalDevice> physicalDevices = toEastlVector(instance.enumeratePhysicalDevices());
        if (physicalDevices.empty())
        {
            throw std::runtime_error("VKDevice::init could not find any Vulkan physical devices.");
        }

        uint32_t bestScore = 0;
        bool foundDevice = false;
        for (const vk::PhysicalDevice &physicalDevice : physicalDevices)
        {
            const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
            if (properties.apiVersion < VulkanApiVersion)
            {
                GVMLogWarn(
                    logSubject, DeviceLogCategory,
                    "event=device_candidate_rejected name=\"{}\" reason=vulkan_api_version_too_old api_version={} required_api_version={}",
                    properties.deviceName.data(),
                    properties.apiVersion,
                    VulkanApiVersion);
                continue;
            }
            eastl::vector<vk::ExtensionProperties> availableDeviceExtensions = toEastlVector(physicalDevice.enumerateDeviceExtensionProperties());
            const VulkanCapabilitySurvey capabilitySurvey =
                queryVulkanCapabilitySurvey(physicalDevice, availableDeviceExtensions);
            GVMLogInfo(
                logSubject, DeviceLogCategory,
                "event=device_candidate name=\"{}\" type={} api_version={} required_sampled_image_descriptor_indexing={} descriptor_indexing_path={} robust_buffer_access={} robust_buffer_access2={} sampler_anisotropy={} independent_blend={} multi_draw_indirect={} barycentric={} tessellation={} shader_float16={} shader_float16_path={}",
                properties.deviceName.data(),
                physicalDeviceTypeName(properties.deviceType),
                properties.apiVersion,
                capabilitySurvey.supportsRequiredSampledImageDescriptorIndexing,
                descriptorIndexingPathName(capabilitySurvey),
                capabilitySurvey.supportsRobustBufferAccess,
                capabilitySurvey.supportsRobustBufferAccess2,
                capabilitySurvey.supportsSamplerAnisotropy,
                capabilitySurvey.supportsIndependentBlend,
                capabilitySurvey.supportsMultiDrawIndirect,
                capabilitySurvey.supportsFragmentShaderBarycentric,
                capabilitySurvey.supportsTessellationShaderFeature,
                capabilitySurvey.supportsShaderFloat16,
                shaderFloat16PathName(capabilitySurvey));
            if (!capabilitySurvey.supportsRequiredSampledImageDescriptorIndexing ||
                !capabilitySurvey.supportsRequiredVulkan13Features)
            {
                continue;
            }

            eastl::vector<vk::QueueFamilyProperties> queueFamilies = toEastlVector(physicalDevice.getQueueFamilyProperties());
            for (uint32_t familyIndex = 0; familyIndex < queueFamilies.size(); ++familyIndex)
            {
                if ((queueFamilies[familyIndex].queueFlags & vk::QueueFlagBits::eGraphics) == vk::QueueFlags{})
                {
                    continue;
                }

                const uint32_t score = scorePhysicalDeviceType(properties.deviceType);
                if (!foundDevice || score > bestScore)
                {
                    bestScore = score;
                    foundDevice = true;
                    mPhysicalDevice = physicalDevice;
                    mPhysicalDeviceProperties = properties;
                    mQueueFamilyProperties = eastl::move(queueFamilies);
                    mSurvey = capabilitySurvey;
                    mSelectedDeviceExtensions = eastl::move(availableDeviceExtensions);
                    mGraphicsQueueFamilyIndex = familyIndex;
                }
                break;
            }
        }

        if (!foundDevice)
        {
            throw std::runtime_error(
                "VKDevice::init could not find a Vulkan 1.3 graphics-capable device exposing the required core sampled-image descriptor indexing feature set "
                "(descriptor indexing, sampled-image/storage-buffer update-after-bind, partially bound arrays, timeline semaphores, buffer device address, synchronization2, and compatible robust buffer access).");
        }

        for (size_t index = 1u; index < mTextureFormatProperties.size(); ++index)
        {
            mTextureFormatProperties[index] = mPhysicalDevice.getFormatProperties(translateTextureFormat(static_cast<TextureFormat>(index)));
        }
        mMemoryProperties = queryDeviceMemoryProperties(mPhysicalDevice);
        const uint32_t selectedTimestampValidBits =
            mGraphicsQueueFamilyIndex < mQueueFamilyProperties.size()
                ? mQueueFamilyProperties[mGraphicsQueueFamilyIndex].timestampValidBits
                : 0u;
        mTimestampQuerySupport.supported = selectedTimestampValidBits != 0u ? True : False;
        mTimestampQuerySupport.validBits = selectedTimestampValidBits;
        mTimestampQuerySupport.tickPeriodNs = mPhysicalDeviceProperties.limits.timestampPeriod;
        mTimestampQuerySupport.passTimestampWritesSupported = mTimestampQuerySupport.supported;
        GVMLogInfo(
            logSubject, DeviceLogCategory,
            "event=device_selected name=\"{}\" type={} queue_family_index={} unified_memory={} timestamp_valid_bits={} timestamp_period_ns={} max_compute_workgroup_count={}x{}x{} max_compute_workgroup_invocations={}",
            mPhysicalDeviceProperties.deviceName.data(),
            physicalDeviceTypeName(mPhysicalDeviceProperties.deviceType),
            mGraphicsQueueFamilyIndex,
            mMemoryProperties.unifiedMemory != False,
            selectedTimestampValidBits,
            mPhysicalDeviceProperties.limits.timestampPeriod,
            mPhysicalDeviceProperties.limits.maxComputeWorkGroupCount[0],
            mPhysicalDeviceProperties.limits.maxComputeWorkGroupCount[1],
            mPhysicalDeviceProperties.limits.maxComputeWorkGroupCount[2],
            mPhysicalDeviceProperties.limits.maxComputeWorkGroupInvocations);
    }

    bool VKDeviceCapabilities::enablePassCounterQuery(
        eastl::vector<const char *> &enabledExtensions,
        vk::PhysicalDevicePerformanceQueryFeaturesKHR &outFeatures)
    {
        mPassCounterQuerySupport = {};
        mPassCounterQuerySupport.backendName = "vulkan";
        mPassCounterQuerySupport.supported = False;
        if (!hasDeviceExtension(mSelectedDeviceExtensions, PerformanceQueryExtensionName))
        {
            mPassCounterQuerySupport.unsupportedReason = "VK_KHR_performance_query is not exposed by the selected Vulkan device.";
            return false;
        }

        vk::PhysicalDevicePerformanceQueryFeaturesKHR supportedPerformanceQueryFeatures = {};
        vk::PhysicalDeviceFeatures2 performanceFeatures2 = {};
        performanceFeatures2.pNext = &supportedPerformanceQueryFeatures;
        mPhysicalDevice.getFeatures2(&performanceFeatures2);
        if (supportedPerformanceQueryFeatures.performanceCounterQueryPools == VK_FALSE)
        {
            mPassCounterQuerySupport.unsupportedReason =
                "VK_KHR_performance_query is available, but performanceCounterQueryPools is not supported.";
            return false;
        }

        enabledExtensions.push_back(PerformanceQueryExtensionName);
        outFeatures.performanceCounterQueryPools = VK_TRUE;
        mPassCounterQuerySupport.requiresExclusiveProfilingLock = True;
        mPassCounterQuerySupport.supportsSingleSubmitCounterPass = False;
        mPassCounterQuerySupport.unsupportedReason =
            "VK_KHR_performance_query is available, but GVM has not selected a stable single-submit normalized counter set for this backend yet.";
        return true;
    }

    void VKDeviceCapabilities::noteLogicalDeviceCreated(const vk::PhysicalDeviceFeatures &enabledCoreFeatures)
    {
        mSupportsSamplerAnisotropy = mSurvey.supportsSamplerAnisotropy;
        mSupportsFragmentShaderBarycentric = mSurvey.supportsFragmentShaderBarycentric;
        mSupportsTessellationShaderFeature = mSurvey.supportsTessellationShaderFeature;
        mSupportsShaderFloat16 = mSurvey.supportsShaderFloat16;
        mSupportsShaderSubgroupExtendedTypes = mSurvey.supportsShaderSubgroupExtendedTypes;
        mSupportsDrawIndirectCount = mSurvey.supportsDrawIndirectCount;
        mSupportsMemoryBudget = hasSelectedDeviceExtension(MemoryBudgetExtensionName);
        mDescriptorIndexingProperties = mSurvey.indexingProperties;
        mStorageImageArrayNonUniformIndexing = mSurvey.supportsStorageImageArrayNonUniformIndexing;
        mStorageImageUpdateAfterBind = mSurvey.supportsStorageImageUpdateAfterBind;
        mEnabled16BitStorageFeatures = mSurvey.storage16BitFeatures;
        mEnabledShaderFeatures = enabledCoreFeatures;
        vk::PhysicalDeviceProperties2 subgroupProperties2 = {};
        subgroupProperties2.pNext = &mSubgroupProperties;
        mPhysicalDevice.getProperties2(&subgroupProperties2);
        mMaxSamplerAnisotropy = mSurvey.maxSamplerAnisotropy;
    }
    const vk::FormatProperties &VKDeviceCapabilities::getTextureFormatProperties(TextureFormat format) const
    {
        const size_t index = static_cast<size_t>(format);
        if (index == 0u || index >= mTextureFormatProperties.size())
        {
            throw std::invalid_argument("Vulkan texture format is outside the supported format table.");
        }
        return mTextureFormatProperties[index];
    }
} // namespace GVM::RHI::Vulkan
