#include "VKDevice.hpp"

#include "VKBindGroup.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKBuffer.hpp"
#include "VKComputePipeline.hpp"
#include "VKDescriptorCacheUtils.hpp"
#include "VKDiagnosticsOverlayShaders.hpp"
#include "VKInstance.hpp"
#include "VKLogging.hpp"
#include "VKPipelineLayout.hpp"
#include "VKQuerySet.hpp"
#include "VKQueue.hpp"
#include "VKRenderPipeline.hpp"
#include "VKSampler.hpp"
#include "VKShaderModule.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include "Private/TimeUtils.hpp"
#include "Private/PublicLoggerAdapter.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>
#include <GVMRHI/Private/RHIDiagnosticsOverlay.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/make_intrusive.h>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <sstream>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view DeviceLogCategory = "gvmrhi.vulkan.device";

        [[nodiscard]]
        double durationMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return Internal::elapsedMilliseconds(startTime);
        }

        /** Rounds a texel extent up to a compressed-format block count. */
        uint64_t divideRoundUp(uint64_t value, uint64_t divisor)
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        /** Estimates storage bytes for a live texture using public texture dimensions and format block metadata. */
        uint64_t estimateTextureStorageBytes(Texture texture)
        {
            if (texture.isNull())
            {
                return 0u;
            }

            const Private::BlockInfo block = Private::getTextureBlockInfo(texture->getFormat());
            uint64_t estimatedBytes = 0u;
            for (uint32_t mipLevel = 0u; mipLevel < texture->getMipLevelCount(); ++mipLevel)
            {
                const uint64_t mipWidth = eastl::max(1u, texture->getWidth() >> mipLevel);
                const uint64_t mipHeight = eastl::max(1u, texture->getHeight() >> mipLevel);
                const uint64_t mipDepth = eastl::max(1u, texture->getDepth() >> mipLevel);
                const uint64_t blockColumns = divideRoundUp(mipWidth, block.width);
                const uint64_t blockRows = divideRoundUp(mipHeight, block.height);
                estimatedBytes += blockColumns * blockRows * mipDepth * texture->getArrayLayerCount() * block.bytes;
            }
            return estimatedBytes;
        }

        /** Adds a live buffer handle to a diagnostics snapshot when the handle is valid. */
        void appendBufferSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Buffer buffer)
        {
            if (buffer.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Buffer;
            entry.label = buffer->getLabelName();
            entry.estimatedBytes = buffer->getStorageSize();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }

        /** Adds a live texture handle to a diagnostics snapshot when the handle is valid. */
        void appendTextureSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Texture texture)
        {
            if (texture.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Texture;
            entry.label = texture->getLabelName();
            entry.estimatedBytes = estimateTextureStorageBytes(texture);
            entry.width = texture->getWidth();
            entry.height = texture->getHeight();
            entry.depth = texture->getDepth();
            entry.mipLevelCount = texture->getMipLevelCount();
            entry.arrayLayerCount = texture->getArrayLayerCount();
            entry.format = texture->getFormat();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }
    } // namespace

    namespace
    {
        constexpr const char *FragmentShaderBarycentricExtensionName = VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME;
        constexpr const char *PerformanceQueryExtensionName = VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME;
        constexpr const char *PortabilitySubsetExtensionName = "VK_KHR_portability_subset";
        constexpr const char *Robustness2ExtensionName = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
        constexpr const char *SwapchainExtensionName = "VK_KHR_swapchain";

        struct VulkanCapabilitySurvey
        {
            bool supportsRequiredSampledImageDescriptorIndexing = false;
            bool supportsRobustBufferAccess = false;
            bool supportsRobustBufferAccess2 = false;
            bool supportsSamplerAnisotropy = false;
            bool supportsIndependentBlend = false;
            bool supportsMultiDrawIndirect = false;
            bool supportsVertexPipelineStoresAndAtomics = false;
            bool supportsFragmentShaderBarycentric = false;
            bool supportsTessellationShaderFeature = false;
            bool supportsShaderFloat16 = false;
            bool supportsShaderSubgroupExtendedTypes = false;
            vk::PhysicalDevice16BitStorageFeatures storage16BitFeatures = {};
            bool supportsStorageImageExtendedFormats = false;
            bool supportsImageGatherExtended = false;
            float maxSamplerAnisotropy = 1.0f;
        };

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

        bool supportsRequiredSampledImageDescriptorIndexing(const vk::PhysicalDeviceVulkan12Features &features)
        {
            return features.descriptorIndexing != VK_FALSE &&
                features.shaderSampledImageArrayNonUniformIndexing != VK_FALSE &&
                features.runtimeDescriptorArray != VK_FALSE;
        }

        const char *descriptorIndexingPathName(const VulkanCapabilitySurvey &survey)
        {
            return survey.supportsRequiredSampledImageDescriptorIndexing ? "core" : "unavailable";
        }

        const char *shaderFloat16PathName(const VulkanCapabilitySurvey &survey)
        {
            return survey.supportsShaderFloat16 ? "core" : "unavailable";
        }

        uint64_t currentSteadyClockMicros()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                                             .count());
        }

        template <typename FeatureStruct>
        void appendFeatureChainStruct(void *&featureChainHead, FeatureStruct &featureStruct)
        {
            featureStruct.pNext = featureChainHead;
            featureChainHead = &featureStruct;
        }

        bool matchesCachedComputePipeline(const ComputePipeline &candidate, const ComputePipelineDescriptor &descriptor)
        {
            return Detail::equalComputePipelineDescriptor(
                static_cast<VKComputePipeline *>(candidate.get())->getDescriptor(),
                descriptor);
        }

        bool matchesCachedRenderPipeline(const RenderPipeline &candidate, const RenderPipelineDescriptor &descriptor)
        {
            return Detail::equalRenderPipelineDescriptor(
                static_cast<VKRenderPipeline *>(candidate.get())->getDescriptor(),
                descriptor);
        }

        bool matchesCachedShaderModule(const ShaderModule &candidate, const ShaderModuleDescriptor &descriptor)
        {
            return Detail::equalShaderModuleDescriptor(
                static_cast<VKShaderModule *>(candidate.get())->getDescriptor(),
                descriptor);
        }

        bool matchesCachedBindGroupLayout(const BindGroupLayout &candidate, const BindGroupLayoutDescriptor &descriptor)
        {
            return Detail::equalBindGroupLayoutDescriptor(
                static_cast<VKBindGroupLayout *>(candidate.get())->getDescriptor(),
                descriptor);
        }

        bool matchesCachedPipelineLayout(const PipelineLayout &candidate, const PipelineLayoutDescriptor &descriptor)
        {
            return Detail::equalPipelineLayoutDescriptor(
                static_cast<VKPipelineLayout *>(candidate.get())->getDescriptor(),
                descriptor);
        }

        template <typename Handle, typename Descriptor>
        Handle findCachedHandle(
            CacheDetail::VKHashBucketCache<Handle> &cache,
            uint64_t descriptorHash,
            const Descriptor &descriptor,
            bool (*matches)(const Handle &candidate, const Descriptor &descriptor))
        {
            const Handle *cached = cache.findMatching(descriptorHash, matches, descriptor);
            if (cached == nullptr)
            {
                return {};
            }
            return *cached;
        }

        template <typename Handle, typename Descriptor>
        Handle findOrCreateCachedHandle(
            VKDevice &device,
            Mutex &cacheMutex,
            CacheDetail::VKHashBucketCache<Handle> &cache,
            uint64_t descriptorHash,
            const Descriptor &descriptor,
            bool (*matches)(const Handle &candidate, const Descriptor &descriptor),
            Handle (*createHandle)(VKDevice &device, const Descriptor &descriptor))
        {
            {
                ScopedLock lock(cacheMutex);
                const Handle cached = findCachedHandle(cache, descriptorHash, descriptor, matches);
                if (cached)
                {
                    return cached;
                }
            }

            Handle created = createHandle(device, descriptor);

            ScopedLock lock(cacheMutex);
            const Handle cached = findCachedHandle(cache, descriptorHash, descriptor, matches);
            if (cached)
            {
                return cached;
            }

            cache.insert(descriptorHash, created);
            return created;
        }

        ComputePipeline createComputePipelineHandle(VKDevice &device, const ComputePipelineDescriptor &descriptor)
        {
            ComputePipeline pipeline = new VKComputePipeline();
            static_cast<VKComputePipeline *>(pipeline.get())->init(device, descriptor);
            return pipeline;
        }

        RenderPipeline createRenderPipelineHandle(VKDevice &device, const RenderPipelineDescriptor &descriptor)
        {
            RenderPipeline pipeline = new VKRenderPipeline();
            static_cast<VKRenderPipeline *>(pipeline.get())->init(device, descriptor);
            return pipeline;
        }

        ShaderModule createShaderModuleHandle(VKDevice &device, const ShaderModuleDescriptor &descriptor)
        {
            ShaderModule shaderModule = new VKShaderModule();
            static_cast<VKShaderModule *>(shaderModule.get())->init(device, descriptor);
            return shaderModule;
        }

        BindGroupLayout createBindGroupLayoutHandle(VKDevice &device, const BindGroupLayoutDescriptor &descriptor)
        {
            BindGroupLayout layout = new VKBindGroupLayout();
            try
            {
                static_cast<VKBindGroupLayout *>(layout.get())->init(device, descriptor);
            }
            catch (const Exception &e)
            {
                throw makeInvalidArgument(
                    "VKDevice::createBindGroupLayout failed for layout '" +
                    eastl::string(descriptor.label.c_str()) +
                    "': " + e.what());
            }
            return layout;
        }

        PipelineLayout createPipelineLayoutHandle(VKDevice &device, const PipelineLayoutDescriptor &descriptor)
        {
            PipelineLayout layout = new VKPipelineLayout();
            try
            {
                static_cast<VKPipelineLayout *>(layout.get())->init(device, descriptor);
            }
            catch (const Exception &e)
            {
                throw makeInvalidArgument(
                    "VKDevice::createPipelineLayout failed for layout '" +
                    eastl::string(descriptor.label.c_str()) +
                    "': " + e.what());
            }
            return layout;
        }

        VulkanCapabilitySurvey queryVulkanCapabilitySurvey(
            vk::PhysicalDevice physicalDevice,
            const eastl::vector<vk::ExtensionProperties> &availableExtensions)
        {
            VulkanCapabilitySurvey survey = {};
            vk::PhysicalDeviceVulkan12Features supportedVulkan12Features = {};
            vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR supportedBarycentricFeatures = {};
            vk::PhysicalDeviceRobustness2FeaturesEXT supportedRobustness2Features = {};
            vk::PhysicalDeviceFeatures2 supportedFeatures2 = {};
            supportedVulkan12Features.pNext = &survey.storage16BitFeatures;
            survey.storage16BitFeatures.pNext = &supportedBarycentricFeatures;
            supportedBarycentricFeatures.pNext = &supportedRobustness2Features;
            supportedFeatures2.pNext = &supportedVulkan12Features;
            physicalDevice.getFeatures2(&supportedFeatures2);
            survey.storage16BitFeatures.pNext = nullptr;

            const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
            survey.supportsRobustBufferAccess = supportedFeatures2.features.robustBufferAccess != VK_FALSE;
            survey.supportsRobustBufferAccess2 =
                hasDeviceExtension(availableExtensions, Robustness2ExtensionName) &&
                supportedRobustness2Features.robustBufferAccess2 != VK_FALSE;
            survey.supportsSamplerAnisotropy = supportedFeatures2.features.samplerAnisotropy != VK_FALSE;
            survey.supportsIndependentBlend = supportedFeatures2.features.independentBlend != VK_FALSE;
            survey.supportsStorageImageExtendedFormats = supportedFeatures2.features.shaderStorageImageExtendedFormats != VK_FALSE;
            survey.supportsImageGatherExtended = supportedFeatures2.features.shaderImageGatherExtended != VK_FALSE;
            survey.supportsMultiDrawIndirect = supportedFeatures2.features.multiDrawIndirect != VK_FALSE;
            survey.supportsVertexPipelineStoresAndAtomics = supportedFeatures2.features.vertexPipelineStoresAndAtomics != VK_FALSE;
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

    void VKDevice::init(VKInstance *instance)
    {
        if (instance == nullptr)
        {
            throw makeInvalidArgument("VKDevice::init requires a valid instance.");
        }

        mInstance = instance;
        mLogContext = instance->getLogContext();
        mLogger = instance->getLogger();
        mDiagnosticsOverlayConfig = instance->getDescriptor().diagnosticsOverlay;
        if (!mLogger && mLogContext)
        {
            mLogger = Internal::createPublicLogger(mLogContext);
        }
        GVMLogInfo(this, DeviceLogCategory, "event=device_init_begin");
        mPresentManager.reset();
        mRetireManager.reset();
        GVMLogInfo(
            this, DeviceLogCategory,
            "event=device_resource_collection_config resource_collect_interval_us={}",
            VKRetireManager::CollectionRetryIntervalUs);

        const eastl::vector<vk::PhysicalDevice> physicalDevices = toEastlVector(mInstance->getNativeInstance().enumeratePhysicalDevices());
        if (physicalDevices.empty())
        {
            throw makeRuntimeError("VKDevice::init could not find any Vulkan physical devices.");
        }

        uint32_t bestScore = 0;
        bool foundDevice = false;
        for (const vk::PhysicalDevice &physicalDevice : physicalDevices)
        {
            const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
            if (properties.apiVersion < VulkanApiVersion)
            {
                GVMLogWarn(
                    this, DeviceLogCategory,
                    "event=device_candidate_rejected name=\"{}\" reason=vulkan_api_version_too_old api_version={} required_api_version={}",
                    properties.deviceName.data(),
                    properties.apiVersion,
                    VulkanApiVersion);
                continue;
            }
            const eastl::vector<vk::ExtensionProperties> availableDeviceExtensions = toEastlVector(physicalDevice.enumerateDeviceExtensionProperties());
            const VulkanCapabilitySurvey capabilitySurvey =
                queryVulkanCapabilitySurvey(physicalDevice, availableDeviceExtensions);
            GVMLogInfo(
                this, DeviceLogCategory,
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
            if (!capabilitySurvey.supportsRequiredSampledImageDescriptorIndexing)
            {
                continue;
            }

            const eastl::vector<vk::QueueFamilyProperties> queueFamilies = toEastlVector(physicalDevice.getQueueFamilyProperties());
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
                    mGraphicsQueueFamilyIndex = familyIndex;
                }
                break;
            }
        }

        if (!foundDevice)
        {
            throw makeRuntimeError(
                "VKDevice::init could not find a Vulkan 1.3 graphics-capable device exposing the required core sampled-image descriptor indexing feature set "
                "(dynamic indexing, non-uniform indexing, and runtime descriptor arrays).");
        }

        const vk::PhysicalDeviceProperties selectedProperties = mPhysicalDevice.getProperties();
        mMemoryProperties = queryDeviceMemoryProperties(mPhysicalDevice);
        const eastl::vector<vk::QueueFamilyProperties> selectedQueueFamilies = toEastlVector(mPhysicalDevice.getQueueFamilyProperties());
        const uint32_t selectedTimestampValidBits =
            mGraphicsQueueFamilyIndex < selectedQueueFamilies.size()
                ? selectedQueueFamilies[mGraphicsQueueFamilyIndex].timestampValidBits
                : 0u;
        mTimestampQuerySupport.supported = selectedTimestampValidBits != 0u ? True : False;
        mTimestampQuerySupport.validBits = selectedTimestampValidBits;
        mTimestampQuerySupport.tickPeriodNs = selectedProperties.limits.timestampPeriod;
        mTimestampQuerySupport.passTimestampWritesSupported = mTimestampQuerySupport.supported;
        GVMLogInfo(
            this, DeviceLogCategory,
            "event=device_selected name=\"{}\" type={} queue_family_index={} unified_memory={} timestamp_valid_bits={} timestamp_period_ns={} max_compute_workgroup_count={}x{}x{} max_compute_workgroup_invocations={}",
            selectedProperties.deviceName.data(),
            physicalDeviceTypeName(selectedProperties.deviceType),
            mGraphicsQueueFamilyIndex,
            mMemoryProperties.unifiedMemory != False,
            selectedTimestampValidBits,
            selectedProperties.limits.timestampPeriod,
            selectedProperties.limits.maxComputeWorkGroupCount[0],
            selectedProperties.limits.maxComputeWorkGroupCount[1],
            selectedProperties.limits.maxComputeWorkGroupCount[2],
            selectedProperties.limits.maxComputeWorkGroupInvocations);

        const float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        const eastl::vector<vk::ExtensionProperties> availableDeviceExtensions = toEastlVector(mPhysicalDevice.enumerateDeviceExtensionProperties());
        eastl::vector<const char *> enabledDeviceExtensions;
        if (hasDeviceExtension(availableDeviceExtensions, PortabilitySubsetExtensionName))
        {
            enabledDeviceExtensions.push_back(PortabilitySubsetExtensionName);
        }
        if (hasDeviceExtension(availableDeviceExtensions, SwapchainExtensionName))
        {
            enabledDeviceExtensions.push_back(SwapchainExtensionName);
            mSupportsSwapchain = true;
        }

        const VulkanCapabilitySurvey capabilitySurvey =
            queryVulkanCapabilitySurvey(mPhysicalDevice, availableDeviceExtensions);
        if (!capabilitySurvey.supportsRequiredSampledImageDescriptorIndexing)
        {
            throw makeRuntimeError(
                "VKDevice::init selected a device that does not expose the required core sampled-image descriptor indexing feature set "
                "(dynamic indexing, non-uniform indexing, and runtime descriptor arrays).");
        }

        vk::PhysicalDeviceVulkan12Features enabledVulkan12Features = {};
        vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR enabledBarycentricFeatures = {};
        vk::PhysicalDevicePerformanceQueryFeaturesKHR enabledPerformanceQueryFeatures = {};
        vk::PhysicalDeviceRobustness2FeaturesEXT enabledRobustness2Features = {};
        vk::PhysicalDeviceFeatures2 enabledFeatures2 = {};
        enabledFeatures2.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
        enabledFeatures2.features.robustBufferAccess = capabilitySurvey.supportsRobustBufferAccess ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.samplerAnisotropy = capabilitySurvey.supportsSamplerAnisotropy ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.independentBlend = capabilitySurvey.supportsIndependentBlend ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.shaderStorageImageExtendedFormats = capabilitySurvey.supportsStorageImageExtendedFormats ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.shaderImageGatherExtended = capabilitySurvey.supportsImageGatherExtended ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.multiDrawIndirect = capabilitySurvey.supportsMultiDrawIndirect ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.vertexPipelineStoresAndAtomics = capabilitySurvey.supportsVertexPipelineStoresAndAtomics ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.tessellationShader = capabilitySurvey.supportsTessellationShaderFeature ? VK_TRUE : VK_FALSE;

        void *featureChainHead = nullptr;
        vk::PhysicalDevice16BitStorageFeatures enabled16BitStorageFeatures = capabilitySurvey.storage16BitFeatures;
        appendFeatureChainStruct(featureChainHead, enabled16BitStorageFeatures);
        enabledVulkan12Features.descriptorIndexing = VK_TRUE;
        enabledVulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        enabledVulkan12Features.runtimeDescriptorArray = VK_TRUE;

        if (capabilitySurvey.supportsFragmentShaderBarycentric)
        {
            enabledDeviceExtensions.push_back(FragmentShaderBarycentricExtensionName);
            enabledBarycentricFeatures.fragmentShaderBarycentric = VK_TRUE;
            appendFeatureChainStruct(featureChainHead, enabledBarycentricFeatures);
        }

        if (capabilitySurvey.supportsShaderFloat16)
        {
            enabledVulkan12Features.shaderFloat16 = VK_TRUE;
        }
        enabledVulkan12Features.shaderSubgroupExtendedTypes = capabilitySurvey.supportsShaderSubgroupExtendedTypes ? VK_TRUE : VK_FALSE;
        mSupportsShaderSubgroupExtendedTypes = capabilitySurvey.supportsShaderSubgroupExtendedTypes;

        mPassCounterQuerySupport = {};
        mPassCounterQuerySupport.backendName = "vulkan";
        mPassCounterQuerySupport.supported = False;
        if (hasDeviceExtension(availableDeviceExtensions, PerformanceQueryExtensionName))
        {
            vk::PhysicalDevicePerformanceQueryFeaturesKHR supportedPerformanceQueryFeatures = {};
            vk::PhysicalDeviceFeatures2 performanceFeatures2 = {};
            performanceFeatures2.pNext = &supportedPerformanceQueryFeatures;
            mPhysicalDevice.getFeatures2(&performanceFeatures2);
            if (supportedPerformanceQueryFeatures.performanceCounterQueryPools != VK_FALSE)
            {
                enabledDeviceExtensions.push_back(PerformanceQueryExtensionName);
                enabledPerformanceQueryFeatures.performanceCounterQueryPools = VK_TRUE;
                appendFeatureChainStruct(featureChainHead, enabledPerformanceQueryFeatures);
                mPassCounterQuerySupport.requiresExclusiveProfilingLock = True;
                mPassCounterQuerySupport.supportsSingleSubmitCounterPass = False;
                mPassCounterQuerySupport.unsupportedReason =
                    "VK_KHR_performance_query is available, but GVM has not selected a stable single-submit normalized counter set for this backend yet.";
            }
            else
            {
                mPassCounterQuerySupport.unsupportedReason =
                    "VK_KHR_performance_query is available, but performanceCounterQueryPools is not supported.";
            }
        }
        else
        {
            mPassCounterQuerySupport.unsupportedReason = "VK_KHR_performance_query is not exposed by the selected Vulkan device.";
        }

        if (capabilitySurvey.supportsRobustBufferAccess2)
        {
            enabledDeviceExtensions.push_back(Robustness2ExtensionName);
            enabledRobustness2Features.robustBufferAccess2 = VK_TRUE;
            appendFeatureChainStruct(featureChainHead, enabledRobustness2Features);
        }

        appendFeatureChainStruct(featureChainHead, enabledVulkan12Features);

        enabledFeatures2.pNext = featureChainHead;

        vk::DeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.pNext = &enabledFeatures2;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();

        GVMLogInfo(
            this, DeviceLogCategory,
            "event=device_create enabled_extension_count={} enabled_extensions=[{}] swapchain_supported={} descriptor_indexing_path={} robust_buffer_access={} robust_buffer_access2={} sampler_anisotropy={} independent_blend={} multi_draw_indirect={} barycentric={} tessellation={} shader_float16={} shader_float16_path={}",
            enabledDeviceExtensions.size(),
            joinDeviceExtensionNames(enabledDeviceExtensions).c_str(),
            mSupportsSwapchain,
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

        mDevice = mPhysicalDevice.createDeviceUnique(deviceCreateInfo);
        volkLoadDevice(static_cast<VkDevice>(mDevice.get()));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(mDevice.get());

        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(mPhysicalDevice);
        allocatorCreateInfo.device = static_cast<VkDevice>(mDevice.get());
        allocatorCreateInfo.instance = static_cast<VkInstance>(mInstance->getNativeInstance());
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorCreateInfo.vulkanApiVersion = VulkanApiVersion;

        if (vmaCreateAllocator(&allocatorCreateInfo, &mAllocator) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKDevice::init failed to create the VMA allocator.");
        }

        mSupportsSamplerAnisotropy = capabilitySurvey.supportsSamplerAnisotropy;
        mSupportsMultiDrawIndirect = capabilitySurvey.supportsMultiDrawIndirect;
        mSupportsVertexPipelineStoresAndAtomics = capabilitySurvey.supportsVertexPipelineStoresAndAtomics;
        mSupportsFragmentShaderBarycentric = capabilitySurvey.supportsFragmentShaderBarycentric;
        mSupportsTessellationShaderFeature = capabilitySurvey.supportsTessellationShaderFeature;
        mSupportsShaderFloat16 = capabilitySurvey.supportsShaderFloat16;
        mEnabled16BitStorageFeatures = capabilitySurvey.storage16BitFeatures;
        mEnabledShaderFeatures = enabledFeatures2.features;
        vk::PhysicalDeviceProperties2 subgroupProperties2 = {};
        subgroupProperties2.pNext = &mSubgroupProperties;
        mPhysicalDevice.getProperties2(&subgroupProperties2);
        mMaxSamplerAnisotropy = capabilitySurvey.maxSamplerAnisotropy;

        mBufferPool.init();
        mTexturePool.init();
        mTextureViewPool.init();
        mSamplerPool.init();
        mDescriptorPoolStore.init(this);
        mRenderPassFramebufferCache.init(mDevice.get());

        mMainQueue = new VKQueue();
        try
        {
            mMainQueue->init(this, mGraphicsQueueFamilyIndex, mDevice->getQueue(mGraphicsQueueFamilyIndex, 0), "MainQueue");
            if (mDiagnosticsOverlayConfig.enabled != False)
            {
                mDiagnosticsOverlayQueue = GVM::RHI::Private::createDiagnosticsOverlayQueue(this, mMainQueue, createVulkanDiagnosticsOverlayShaderProvider());
            }
        }
        catch (...)
        {
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
            delete mMainQueue;
            mMainQueue = nullptr;
            throw;
        }

        mRenderToSwapchainExecutor = eastl::make_intrusive<VKRenderToSwapchainExecutorImpl>();
        mRenderToSwapchainExecutor->init(this);
        GVMLogInfo(
            this, DeviceLogCategory,
            "event=device_init_end queue_family_index={} supports_swapchain={} robust_buffer_access={} robust_buffer_access2={} max_sampler_anisotropy={}",
            mGraphicsQueueFamilyIndex,
            mSupportsSwapchain,
            capabilitySurvey.supportsRobustBufferAccess,
            capabilitySurvey.supportsRobustBufferAccess2,
            mMaxSamplerAnisotropy);
    }

    vk::Device VKDevice::getNativeDevice() const
    {
        return mDevice.get();
    }

    vk::PhysicalDevice VKDevice::getPhysicalDevice() const
    {
        return mPhysicalDevice;
    }

    VKInstance *VKDevice::getInstance() const
    {
        return mInstance;
    }

    const eastl::shared_ptr<Internal::LogContext> &VKDevice::getLogContext() const
    {
        return mLogContext;
    }

    Logger VKDevice::getLogger() const
    {
        return mLogger;
    }

    /** Returns cached memory topology for the selected Vulkan physical device. */
    DeviceMemoryProperties VKDevice::getMemoryProperties() const
    {
        return mMemoryProperties;
    }

    /** Returns the diagnostics overlay configuration captured from the owning Vulkan instance. */
    RuntimeDiagnosticsOverlayConfig VKDevice::getDiagnosticsOverlayConfig() const
    {
        ensureAlive("VKDevice::getDiagnosticsOverlayConfig");
        return mDiagnosticsOverlayConfig;
    }

    /** Returns a snapshot of live Vulkan resources for runtime diagnostics. */
    DiagnosticsResourceSnapshot VKDevice::getDiagnosticsResourceSnapshot() const
    {
        ensureAlive("VKDevice::getDiagnosticsResourceSnapshot");

        DiagnosticsResourceSnapshot snapshot = {};
        ScopedLock lock(mResourceLifetimeMutex);
        snapshot.entries.reserve(mLiveBuffers.size() + mLiveTextures.size());
        for (Buffer buffer : mLiveBuffers)
        {
            appendBufferSnapshotEntry(snapshot, buffer);
        }
        for (Texture texture : mLiveTextures)
        {
            appendTextureSnapshotEntry(snapshot, texture);
        }
        return snapshot;
    }

    VmaAllocator VKDevice::getAllocator() const
    {
        return mAllocator;
    }

    VKQueue *VKDevice::getMainQueueImpl() const
    {
        return mMainQueue;
    }

    bool VKDevice::supportsSwapchain() const
    {
        return mSupportsSwapchain;
    }

    TextureViewPool &VKDevice::getTextureViewPool()
    {
        return mTextureViewPool;
    }

    VKPresentManager &VKDevice::getPresentManager()
    {
        return mPresentManager;
    }

    const VKPresentManager &VKDevice::getPresentManager() const
    {
        return mPresentManager;
    }

    VKRenderToSwapchainExecutor VKDevice::getRenderToSwapchainExecutor() const
    {
        return mRenderToSwapchainExecutor;
    }

    bool VKDevice::supportsSamplerAnisotropy() const
    {
        return mSupportsSamplerAnisotropy;
    }

    float VKDevice::getMaxSamplerAnisotropy() const
    {
        return mMaxSamplerAnisotropy;
    }

    bool VKDevice::supportsVertexPipelineStoresAndAtomics() const
    {
        return mSupportsVertexPipelineStoresAndAtomics;
    }

    bool VKDevice::supportsFragmentShaderBarycentric() const
    {
        return mSupportsFragmentShaderBarycentric;
    }

    bool VKDevice::supportsTessellationShaderFeature() const
    {
        return mSupportsTessellationShaderFeature;
    }

    bool VKDevice::supportsShaderFloat16() const
    {
        return mSupportsShaderFloat16;
    }

    const vk::PhysicalDevice16BitStorageFeatures &VKDevice::getEnabled16BitStorageFeatures() const
    {
        return mEnabled16BitStorageFeatures;
    }

    const vk::PhysicalDeviceFeatures &VKDevice::getEnabledShaderFeatures() const
    {
        return mEnabledShaderFeatures;
    }

    const vk::PhysicalDeviceSubgroupProperties &VKDevice::getSubgroupProperties() const
    {
        return mSubgroupProperties;
    }

    bool VKDevice::supportsMultiDrawIndirect() const
    {
        return mSupportsMultiDrawIndirect;
    }

    uint64_t VKDevice::takeAndResetBindGroupCreateCountForDiagnostics()
    {
        return mBindGroupCreateCount.exchange(0u, std::memory_order_acq_rel);
    }

    uint64_t VKDevice::takeAndResetBindGroupDestroyCountForDiagnostics()
    {
        return mBindGroupDestroyCount.exchange(0u, std::memory_order_acq_rel);
    }

    uint64_t VKDevice::takeAndResetDescriptorSetAllocateCountForDiagnostics()
    {
        return mDescriptorSetAllocateCount.exchange(0u, std::memory_order_acq_rel);
    }

    uint64_t VKDevice::takeAndResetDescriptorSetFreeCountForDiagnostics()
    {
        return mDescriptorSetFreeCount.exchange(0u, std::memory_order_acq_rel);
    }

    void VKDevice::noteBindGroupCreated()
    {
        mBindGroupCreateCount.fetch_add(1u, std::memory_order_relaxed);
    }

    void VKDevice::noteBindGroupDestroyed()
    {
        mBindGroupDestroyCount.fetch_add(1u, std::memory_order_relaxed);
    }

    void VKDevice::noteDescriptorSetAllocated()
    {
        mDescriptorSetAllocateCount.fetch_add(1u, std::memory_order_relaxed);
    }

    void VKDevice::noteDescriptorSetFreed()
    {
        mDescriptorSetFreeCount.fetch_add(1u, std::memory_order_relaxed);
    }

    Queue VKDevice::getMainQueue() const
    {
        ensureAlive("VKDevice::getMainQueue");
        return mDiagnosticsOverlayQueue != nullptr ? mDiagnosticsOverlayQueue : mMainQueue;
    }

    TimestampQuerySupport VKDevice::getTimestampQuerySupport() const
    {
        ensureAlive("VKDevice::getTimestampQuerySupport");
        return mTimestampQuerySupport;
    }

    PassCounterQuerySupport VKDevice::getPassCounterQuerySupport() const
    {
        ensureAlive("VKDevice::getPassCounterQuerySupport");
        return mPassCounterQuerySupport;
    }

    void VKDevice::destroy()
    {
        GVMLogInfo(this, DeviceLogCategory, "event=device_destroy_begin");
        {
            ScopedLock lock(mResourceLifetimeMutex);
            if (mDestroyed)
            {
                return;
            }
            // Publish shutdown before tearing down allocator-backed resources so cross-thread
            // retirement requests stop trying to retire buffers against a partially destroyed
            // Vulkan device/allocator.
            mDestroyed = true;
        }

        if (mDevice)
        {
            GVMLogWarn(this, DeviceLogCategory, "event=device_wait_idle_begin");
            mDevice->waitIdle();
            GVMLogWarn(this, DeviceLogCategory, "event=device_wait_idle_end");
        }

        if (mMainQueue != nullptr)
        {
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
            mMainQueue->destroy();
            delete mMainQueue;
            mMainQueue = nullptr;
        }

        collectReleasedResources();

        {
            ScopedLock lock(mResourceLifetimeMutex);
            while (!mLiveSamplers.empty())
            {
                releaseSamplerHandle(mLiveSamplers.back());
                mLiveSamplers.pop_back();
            }
            while (!mLiveTextures.empty())
            {
                releaseTextureHandle(mLiveTextures.back());
                mLiveTextures.pop_back();
            }
            while (!mLiveBuffers.empty())
            {
                releaseBufferHandle(mLiveBuffers.back());
                mLiveBuffers.pop_back();
            }
        }
        mRetireManager.reset();
        mPresentManager.reset();
        mRenderToSwapchainExecutor = nullptr;

        if (mAllocator != nullptr)
        {
            vmaDestroyAllocator(mAllocator);
            mAllocator = nullptr;
        }

        {
            ScopedLock lock(mCacheMutex);
            mRenderPipelineCache.clear();
            mComputePipelineCache.clear();
            mPipelineLayoutCache.clear();
            mBindGroupLayoutCache.clear();
            mShaderModuleCache.clear();
            mDescriptorPoolStore.destroy();
            mRenderPassFramebufferCache.destroy();
        }
        mDevice.reset();
        mPhysicalDevice = nullptr;
        mInstance = nullptr;
        GVMLogInfo(this, DeviceLogCategory, "event=device_destroy_end");
        if (mLogger != nullptr)
        {
            mLogger->flush();
        }
        mLogger = nullptr;
        mLogContext.reset();
    }

    void VKDevice::ensureAlive(const char *apiName) const
    {
        if (mDestroyed || !mDevice)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on a destroyed Vulkan device.");
        }
    }

    ComputePipeline VKDevice::createComputePipeline(const ComputePipelineDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createComputePipeline");
        return findOrCreateCachedHandle(
            *this,
            mCacheMutex,
            mComputePipelineCache,
            Detail::hashComputePipelineDescriptor(descriptor),
            descriptor,
            matchesCachedComputePipeline,
            createComputePipelineHandle);
    }

    RenderPipeline VKDevice::createRenderPipeline(const RenderPipelineDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createRenderPipeline");
        return findOrCreateCachedHandle(
            *this,
            mCacheMutex,
            mRenderPipelineCache,
            Detail::hashRenderPipelineDescriptor(descriptor),
            descriptor,
            matchesCachedRenderPipeline,
            createRenderPipelineHandle);
    }

    ShaderModule VKDevice::createShaderModule(const ShaderModuleDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createShaderModule");
        return findOrCreateCachedHandle(
            *this,
            mCacheMutex,
            mShaderModuleCache,
            Detail::hashShaderModuleDescriptor(descriptor),
            descriptor,
            matchesCachedShaderModule,
            createShaderModuleHandle);
    }

    BindGroupLayout VKDevice::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createBindGroupLayout");
        return findOrCreateCachedHandle(
            *this,
            mCacheMutex,
            mBindGroupLayoutCache,
            Detail::hashBindGroupLayoutDescriptor(descriptor),
            descriptor,
            matchesCachedBindGroupLayout,
            createBindGroupLayoutHandle);
    }

    PipelineLayout VKDevice::createPipelineLayout(const PipelineLayoutDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createPipelineLayout");
        return findOrCreateCachedHandle(
            *this,
            mCacheMutex,
            mPipelineLayoutCache,
            Detail::hashPipelineLayoutDescriptor(descriptor),
            descriptor,
            matchesCachedPipelineLayout,
            createPipelineLayoutHandle);
    }

    BindGroup VKDevice::createBindGroup(const BindGroupDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createBindGroup");
        const uint64_t descriptorHash = Detail::hashBindGroupDescriptor(descriptor);
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_create_bind_group_no_cache label={} hash={} layout_ptr={}",
            safeLogLabel(descriptor.label),
            descriptorHash,
            static_cast<void *>(descriptor.layout.get()));
        BindGroup bindGroup = new VKBindGroup();
        {
            static_cast<VKBindGroup *>(bindGroup.get())->init(this, descriptor);
        }
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_create_bind_group_end label={} hash={} bind_group_ptr={}",
            safeLogLabel(descriptor.label),
            descriptorHash,
            static_cast<void *>(bindGroup.get()));
        noteBindGroupCreated();
        return bindGroup;
    }

    vk::DescriptorSet VKDevice::allocateDescriptorSet(
        vk::DescriptorSetLayout layout,
        const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
        vk::DescriptorPool &owningPool)
    {
        ensureAlive("VKDevice::allocateDescriptorSet");
        ScopedLock lock(mCacheMutex);
        const vk::DescriptorSet descriptorSet = mDescriptorPoolStore.allocateDescriptorSet(
            layout,
            poolSizesPerSet,
            owningPool,
            getDescriptorPoolReuseRetiredSubmissionId());
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_allocate_descriptor_set_end descriptor_set_ptr={} owning_pool_ptr={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(owningPool)));
        noteDescriptorSetAllocated();
        return descriptorSet;
    }

    void VKDevice::freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet)
    {
        if (mDestroyed)
        {
            GVMLogWarn(
                this, DeviceLogCategory,
                "event=device_free_descriptor_set_skip_destroyed pool_ptr={} descriptor_set_ptr={}",
                reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
                reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)));
            return;
        }
        GVMLogTrace(
            this, DeviceLogCategory,
            "event=device_free_descriptor_set_lock_wait_begin pool_ptr={} descriptor_set_ptr={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)));
        ScopedLock lock(mCacheMutex);
        GVMLogTrace(
            this, DeviceLogCategory,
            "event=device_free_descriptor_set_lock_acquired pool_ptr={} descriptor_set_ptr={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)));
        const uint64_t retiredSubmissionId = getDescriptorPoolReuseRetiredSubmissionId();
        GVMLogTrace(
            this, DeviceLogCategory,
            "event=device_free_descriptor_set_driver_free_begin pool_ptr={} descriptor_set_ptr={} retired_submission_id={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            retiredSubmissionId);
        mDescriptorPoolStore.freeDescriptorSet(pool, descriptorSet, retiredSubmissionId);
        GVMLogTrace(
            this, DeviceLogCategory,
            "event=device_free_descriptor_set_driver_free_end pool_ptr={} descriptor_set_ptr={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)));
        noteDescriptorSetFreed();
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_free_descriptor_set_end pool_ptr={} descriptor_set_ptr={} retired_submission_id={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            retiredSubmissionId);
    }

    uint64_t VKDevice::getDescriptorPoolReuseRetiredSubmissionId() const
    {
        const VKQueue *mainQueue = mMainQueue;
        return mainQueue != nullptr ? mainQueue->getLastRetiredSubmissionId() : 0u;
    }

    vk::RenderPass VKDevice::getOrCreateRenderPass(const RenderPassDescriptor &descriptor)
    {
        ensureAlive("VKDevice::getOrCreateRenderPass");
        ScopedLock lock(mCacheMutex);
        const vk::RenderPass renderPass = mRenderPassFramebufferCache.getOrCreateRenderPass(descriptor);
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_get_or_create_render_pass_end label={} render_pass_ptr={}",
            safeLogLabel(descriptor.label),
            reinterpret_cast<void *>(static_cast<VkRenderPass>(renderPass)));
        return renderPass;
    }

    vk::RenderPass VKDevice::getOrCreatePipelineRenderPass(const RenderPipelineDescriptor &descriptor)
    {
        ensureAlive("VKDevice::getOrCreatePipelineRenderPass");
        ScopedLock lock(mCacheMutex);
        return mRenderPassFramebufferCache.getOrCreatePipelineRenderPass(descriptor);
    }

    VKDevice::FramebufferHandle VKDevice::getOrCreateFramebuffer(
        vk::RenderPass renderPass,
        const eastl::vector<vk::ImageView> &attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers)
    {
        ensureAlive("VKDevice::getOrCreateFramebuffer");
        ScopedLock lock(mCacheMutex);
        const FramebufferHandle framebufferHandle = mRenderPassFramebufferCache.getOrCreateFramebuffer(renderPass, attachments, width, height, layers);
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_get_or_create_framebuffer_end render_pass_ptr={} framebuffer_ptr={} owner_ptr={}",
            reinterpret_cast<void *>(static_cast<VkRenderPass>(renderPass)),
            reinterpret_cast<void *>(static_cast<VkFramebuffer>(framebufferHandle.framebuffer)),
            static_cast<void *>(framebufferHandle.owner.get()));
        return framebufferHandle;
    }

    size_t VKDevice::getFramebufferCacheSizeForTesting() const
    {
        ScopedLock lock(mCacheMutex);
        return mRenderPassFramebufferCache.getFramebufferCacheSize();
    }

    size_t VKDevice::getRenderPassCacheSizeForTesting() const
    {
        ScopedLock lock(mCacheMutex);
        return mRenderPassFramebufferCache.getRenderPassCacheSize();
    }

    size_t VKDevice::getPipelineCompatibleRenderPassCacheSizeForTesting() const
    {
        ScopedLock lock(mCacheMutex);
        return mRenderPassFramebufferCache.getPipelineCompatibleRenderPassCacheSize();
    }

    void VKDevice::invalidateFramebufferCacheForImageView(vk::ImageView imageView)
    {
        if (imageView == vk::ImageView{})
        {
            return;
        }

        ScopedLock lock(mCacheMutex);
        mRenderPassFramebufferCache.invalidateFramebufferCacheForImageView(imageView);
    }

    namespace
    {
        template <typename Handle>
        void eraseTrackedHandle(eastl::vector<Handle> &handles, Handle handle)
        {
            for (auto it = handles.begin(); it != handles.end(); ++it)
            {
                if (*it == handle)
                {
                    handles.erase(it);
                    return;
                }
            }
        }

        // Releases one collected ready-batch vector through the matching VKDevice member function.
        template <typename Item>
        void releaseReadyItems(VKDevice &device, const eastl::vector<Item> &items, void (VKDevice::*releaseItem)(Item))
        {
            for (const Item &item : items)
            {
                (device.*releaseItem)(item);
            }
        }

        // Resolves a typed RHI handle and invokes one reference-count member while preserving the existing invalid-handle checks.
        template <typename Impl, typename Handle>
        void invokeCheckedHandleMember(Handle handle, const char *invalidHandleMessage, void (Impl::*member)())
        {
            if (handle.isNull())
            {
                return;
            }

            auto *resourceImpl = static_cast<Impl *>(handle.get());
            if (resourceImpl == nullptr)
            {
                throw makeInvalidArgument(invalidHandleMessage);
            }

            (resourceImpl->*member)();
        }

        // Invokes one reference-count member only when the resource pointer is present.
        template <typename Impl>
        void invokeResourceMemberIfPresent(Impl *resource, void (Impl::*member)())
        {
            if (resource == nullptr)
            {
                return;
            }

            (resource->*member)();
        }

        void enqueuePendingDestroy(VKRetireManager &retireManager, Buffer buffer)
        {
            retireManager.enqueueBuffer(buffer);
        }

        void enqueuePendingDestroy(VKRetireManager &retireManager, Texture texture)
        {
            retireManager.enqueueTexture(texture);
        }

        void enqueuePendingDestroy(VKRetireManager &retireManager, Sampler sampler)
        {
            retireManager.enqueueSampler(sampler);
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKBuffer &buffer)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_buffer_skip_already_requested label={} total_refs={} bind_group_refs={} command_refs={}",
                safeLogLabel(buffer.getLabelName()),
                buffer.getTotalReferenceCount(),
                buffer.getBindGroupReferenceCount(),
                buffer.getCommandReferenceCount());
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKTexture &texture)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_texture_skip_already_requested label={} total_refs={} bind_group_refs={} command_refs={}",
                safeLogLabel(texture.getLabelName()),
                texture.getTotalReferenceCount(),
                texture.getBindGroupReferenceCount(),
                texture.getCommandReferenceCount());
        }

        void logFreeAlreadyRequested(VKDevice *device, const VKSampler &sampler)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_sampler_skip_already_requested label={} total_refs={} bind_group_refs={}",
                safeLogLabel(sampler.getLabelName()),
                sampler.getTotalReferenceCount(),
                sampler.getBindGroupReferenceCount());
        }

        void logFreeQueued(VKDevice *device, const VKBuffer &buffer, const VKRetireManager &retireManager, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_buffer_queued label={} pending_buffers={} live_buffers={}",
                safeLogLabel(buffer.getLabelName()),
                retireManager.getPendingDestroyBufferCount(),
                liveCount);
        }

        void logFreeQueued(VKDevice *device, const VKTexture &texture, const VKRetireManager &retireManager, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_texture_queued label={} pending_textures={} live_textures={}",
                safeLogLabel(texture.getLabelName()),
                retireManager.getPendingDestroyTextureCount(),
                liveCount);
        }

        void logFreeQueued(VKDevice *device, const VKSampler &sampler, const VKRetireManager &retireManager, size_t liveCount)
        {
            GVMLogDebug(
                device, DeviceLogCategory,
                "event=device_free_sampler_queued label={} pending_samplers={} live_samplers={}",
                safeLogLabel(sampler.getLabelName()),
                retireManager.getPendingDestroySamplerCount(),
                liveCount);
        }

        // Runs the shared user-destroy -> untrack -> retire-manager enqueue flow for one Vulkan resource handle type.
        template <typename Handle, typename Impl>
        void freeTrackedResource(
            VKDevice *device,
            Handle handle,
            const vk::UniqueDevice &nativeDevice,
            VmaAllocator &allocator,
            bool requiresAllocator,
            Mutex &resourceLifetimeMutex,
            eastl::vector<Handle> &liveHandles,
            VKRetireManager &retireManager,
            const char *invalidHandleMessage,
            const char *deviceGoneEvent)
        {
            if (handle.isNull())
            {
                return;
            }

            auto *resourceImpl = static_cast<Impl *>(handle.get());
            if (resourceImpl == nullptr)
            {
                throw makeInvalidArgument(invalidHandleMessage);
            }

            ScopedLock lock(resourceLifetimeMutex);
            if (!nativeDevice || (requiresAllocator && allocator == nullptr))
            {
                GVMLogWarn(
                    device, DeviceLogCategory,
                    "event={} label={}",
                    deviceGoneEvent,
                    safeLogLabel(resourceImpl->getLabelName()));
                return;
            }

            if (!resourceImpl->requestUserDestroy())
            {
                logFreeAlreadyRequested(device, *resourceImpl);
                return;
            }

            eraseTrackedHandle(liveHandles, handle);
            enqueuePendingDestroy(retireManager, handle);
            logFreeQueued(device, *resourceImpl, retireManager, liveHandles.size());
        }
    } // namespace

    Buffer VKDevice::createBuffer(const BufferDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createBuffer");

        auto *bufferImpl = new VKBuffer();
        try
        {
            bufferImpl->init(*this, descriptor);
            Buffer handle = mBufferPool.alloc(bufferImpl);
            trackBuffer(handle);
            return handle;
        }
        catch (...)
        {
            bufferImpl->destroy();
            delete bufferImpl;
            throw;
        }
    }

    QuerySet VKDevice::createQuerySet(const QuerySetDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createQuerySet");
        QuerySet querySet = new VKQuerySet();
        static_cast<VKQuerySet *>(querySet.get())->init(*this, descriptor);
        return querySet;
    }

    Texture VKDevice::createTexture(const TextureDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createTexture");

        auto *textureImpl = new VKTexture();
        try
        {
            textureImpl->init(this, descriptor);
            Texture handle = mTexturePool.alloc(textureImpl);
            trackTexture(handle);
            return handle;
        }
        catch (...)
        {
            textureImpl->destroy();
            delete textureImpl;
            throw;
        }
    }

    Texture VKDevice::createSwapchainImageTexture(const TextureDescriptor &descriptor, vk::Image image)
    {
        ensureAlive("VKDevice::createSwapchainImageTexture");

        auto *textureImpl = new VKTexture();
        try
        {
            textureImpl->initExternalImage(this, descriptor, image);
            textureImpl->setSteadyStateLayout(vk::ImageLayout::ePresentSrcKHR);
            Texture handle = mTexturePool.alloc(textureImpl);
            trackTexture(handle);
            return handle;
        }
        catch (...)
        {
            textureImpl->destroy();
            delete textureImpl;
            throw;
        }
    }

    Sampler VKDevice::createSampler(const SamplerDescriptor &descriptor)
    {
        ensureAlive("VKDevice::createSampler");

        auto *samplerImpl = new VKSampler();
        try
        {
            samplerImpl->init(*this, descriptor);
            Sampler handle = mSamplerPool.alloc(samplerImpl);
            trackSampler(handle);
            return handle;
        }
        catch (...)
        {
            samplerImpl->destroy();
            delete samplerImpl;
            throw;
        }
    }

    void VKDevice::freeBuffer(Buffer buffer)
    {
        freeTrackedResource<Buffer, VKBuffer>(
            this,
            buffer,
            mDevice,
            mAllocator,
            true,
            mResourceLifetimeMutex,
            mLiveBuffers,
            mRetireManager,
            "VKDevice::freeBuffer received a non-Vulkan buffer handle.",
            "device_free_buffer_skip_device_gone");
    }

    void VKDevice::freeTexture(Texture texture)
    {
        freeTrackedResource<Texture, VKTexture>(
            this,
            texture,
            mDevice,
            mAllocator,
            true,
            mResourceLifetimeMutex,
            mLiveTextures,
            mRetireManager,
            "VKDevice::freeTexture received a non-Vulkan texture handle.",
            "device_free_texture_skip_device_gone");
    }

    void VKDevice::freeSampler(Sampler sampler)
    {
        freeTrackedResource<Sampler, VKSampler>(
            this,
            sampler,
            mDevice,
            mAllocator,
            false,
            mResourceLifetimeMutex,
            mLiveSamplers,
            mRetireManager,
            "VKDevice::freeSampler received a non-Vulkan sampler handle.",
            "device_free_sampler_skip_device_gone");
    }

    void VKDevice::collectReleasedResources()
    {
        const bool forceRelease = mDestroyed;
        const uint64_t collectTimeUs = currentSteadyClockMicros();
        if (!mRetireManager.shouldCollect(forceRelease, collectTimeUs))
        {
            return;
        }

        const auto collectStartTime = std::chrono::steady_clock::now();
        VKRetireReadyBatch readyBatch = mRetireManager.collectReady(forceRelease);
        const size_t releasedBindGroupBufferReferenceCount = readyBatch.bindGroupBufferReferenceReleases.size();
        const size_t releasedBindGroupTextureReferenceCount = readyBatch.bindGroupTextureReferenceReleases.size();
        const size_t releasedBindGroupSamplerReferenceCount = readyBatch.bindGroupSamplerReferenceReleases.size();
        const size_t releasedSamplerCount = readyBatch.samplers.size();
        const size_t releasedTextureCount = readyBatch.textures.size();
        const size_t releasedBufferCount = readyBatch.buffers.size();

        releaseReadyItems(
            *this,
            readyBatch.bindGroupBufferReferenceReleases,
            static_cast<void (VKDevice::*)(VKBuffer *)>(&VKDevice::releaseBindGroupBufferReference));
        releaseReadyItems(
            *this,
            readyBatch.bindGroupTextureReferenceReleases,
            &VKDevice::releaseBindGroupTextureReference);
        releaseReadyItems(
            *this,
            readyBatch.bindGroupSamplerReferenceReleases,
            static_cast<void (VKDevice::*)(VKSampler *)>(&VKDevice::releaseBindGroupSamplerReference));
        releaseReadyItems(*this, readyBatch.samplers, &VKDevice::releaseSamplerHandle);
        releaseReadyItems(*this, readyBatch.textures, &VKDevice::releaseTextureHandle);
        releaseReadyItems(*this, readyBatch.buffers, &VKDevice::releaseBufferHandle);

        const size_t totalReleasedCount =
            releasedBindGroupBufferReferenceCount +
            releasedBindGroupTextureReferenceCount +
            releasedBindGroupSamplerReferenceCount +
            releasedSamplerCount +
            releasedTextureCount +
            releasedBufferCount;
        const bool madeProgress = totalReleasedCount != 0u;
        mRetireManager.finishCollection(forceRelease, madeProgress, currentSteadyClockMicros());

        if (forceRelease || madeProgress)
        {
            GVMLogDebug(
                this, DeviceLogCategory,
                "event=collect_released_resources_end force_release={} released_bind_group_buffer_refs={} released_bind_group_texture_refs={} released_bind_group_sampler_refs={} released_samplers={} released_textures={} released_buffers={} pending_resources_remaining={}",
                forceRelease,
                releasedBindGroupBufferReferenceCount,
                releasedBindGroupTextureReferenceCount,
                releasedBindGroupSamplerReferenceCount,
                releasedSamplerCount,
                releasedTextureCount,
                releasedBufferCount,
                readyBatch.pendingAfterCollect);
        }

        GVMLogTrace(
            this, DeviceLogCategory,
            "event=collect_released_resources_trace force_release={} made_progress={} duration_ms={:.3f} released_bind_group_buffer_refs={} released_bind_group_texture_refs={} released_bind_group_sampler_refs={} released_samplers={} released_textures={} released_buffers={} pending_resources_remaining={}",
            forceRelease,
            madeProgress,
            durationMilliseconds(collectStartTime),
            releasedBindGroupBufferReferenceCount,
            releasedBindGroupTextureReferenceCount,
            releasedBindGroupSamplerReferenceCount,
            releasedSamplerCount,
            releasedTextureCount,
            releasedBufferCount,
            readyBatch.pendingAfterCollect);
    }

    void VKDevice::destroyTextureViewHandle(TextureView view)
    {
        if (view.isNull())
        {
            return;
        }

        if (auto *viewImpl = static_cast<VKTextureView *>(view.get()); viewImpl != nullptr)
        {
            invalidateFramebufferCacheForImageView(viewImpl->getNativeImageView());
            viewImpl->destroy();
            GVMLogDebug(
                this, DeviceLogCategory,
                "event=device_destroy_texture_view_end label={} image_view_ptr={}",
                safeLogLabel(viewImpl->getLabelName()),
                reinterpret_cast<void *>(static_cast<VkImageView>(viewImpl->getNativeImageView())));
        }
        mTextureViewPool.freeByHandle(view);
    }

    size_t VKDevice::getPendingDestroyBufferCountForTesting() const
    {
        return mRetireManager.getPendingDestroyBufferCount();
    }

    uint32_t VKDevice::getBufferTotalReferenceCountForTesting(Buffer buffer) const
    {
        auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
        return bufferImpl != nullptr ? bufferImpl->getTotalReferenceCount() : 0u;
    }

    uint32_t VKDevice::getBufferBindGroupReferenceCountForTesting(Buffer buffer) const
    {
        auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
        return bufferImpl != nullptr ? bufferImpl->getBindGroupReferenceCount() : 0u;
    }

    bool VKDevice::isBufferHandleDestroyedForTesting(Buffer buffer) const
    {
        auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
        return bufferImpl == nullptr || bufferImpl->isDestroyed();
    }

    void VKDevice::trackBuffer(Buffer buffer)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveBuffers.push_back(buffer);
    }

    void VKDevice::trackTexture(Texture texture)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveTextures.push_back(texture);
    }

    void VKDevice::trackSampler(Sampler sampler)
    {
        ScopedLock lock(mResourceLifetimeMutex);
        mLiveSamplers.push_back(sampler);
    }

    void VKDevice::retainBindGroupBufferReference(Buffer buffer)
    {
        invokeCheckedHandleMember<VKBuffer>(
            buffer,
            "VKDevice::retainBindGroupBufferReference received a non-Vulkan buffer handle.",
            &VKBuffer::retainBindGroupReference);
    }

    void VKDevice::retainBindGroupTextureReference(VKTexture *texture)
    {
        invokeResourceMemberIfPresent(texture, &VKTexture::retainBindGroupReference);
    }

    void VKDevice::retainBindGroupSamplerReference(Sampler sampler)
    {
        invokeCheckedHandleMember<VKSampler>(
            sampler,
            "VKDevice::retainBindGroupSamplerReference received a non-Vulkan sampler handle.",
            &VKSampler::retainBindGroupReference);
    }

    void VKDevice::releaseBindGroupBufferReference(VKBuffer *buffer)
    {
        invokeResourceMemberIfPresent(buffer, &VKBuffer::releaseBindGroupReference);
    }

    void VKDevice::releaseBindGroupTextureReference(VKTexture *texture)
    {
        invokeResourceMemberIfPresent(texture, &VKTexture::releaseBindGroupReference);
    }

    void VKDevice::releaseBindGroupSamplerReference(VKSampler *sampler)
    {
        invokeResourceMemberIfPresent(sampler, &VKSampler::releaseBindGroupReference);
    }

    void VKDevice::enqueueBindGroupReferenceReleases(
        eastl::vector<VKBuffer *> buffers,
        eastl::vector<VKTexture *> textures,
        eastl::vector<VKSampler *> samplers)
    {
        const uint32_t queuedReleaseCount = static_cast<uint32_t>(buffers.size() + textures.size() + samplers.size());
        if (queuedReleaseCount == 0u)
        {
            return;
        }

        ScopedLock lock(mResourceLifetimeMutex);
        if (mDestroyed)
        {
            for (VKBuffer *buffer : buffers)
            {
                releaseBindGroupBufferReference(buffer);
            }
            for (VKTexture *texture : textures)
            {
                releaseBindGroupTextureReference(texture);
            }
            for (VKSampler *sampler : samplers)
            {
                releaseBindGroupSamplerReference(sampler);
            }
            return;
        }

        mRetireManager.enqueueBindGroupReferenceReleases(
            eastl::move(buffers),
            eastl::move(textures),
            eastl::move(samplers));
    }

    void VKDevice::retainPendingCommandBufferReference(Buffer buffer)
    {
        invokeCheckedHandleMember<VKBuffer>(
            buffer,
            "VKDevice::retainPendingCommandBufferReference received a non-Vulkan buffer handle.",
            &VKBuffer::retainCommandReference);
    }

    void VKDevice::retainPendingCommandTextureReference(Texture texture)
    {
        invokeCheckedHandleMember<VKTexture>(
            texture,
            "VKDevice::retainPendingCommandTextureReference received a non-Vulkan texture handle.",
            &VKTexture::retainCommandReference);
    }

    void VKDevice::releasePendingCommandBufferReference(Buffer buffer)
    {
        invokeCheckedHandleMember<VKBuffer>(
            buffer,
            "VKDevice::releasePendingCommandBufferReference received a non-Vulkan buffer handle.",
            &VKBuffer::releaseCommandReference);
    }

    void VKDevice::releasePendingCommandTextureReference(Texture texture)
    {
        invokeCheckedHandleMember<VKTexture>(
            texture,
            "VKDevice::releasePendingCommandTextureReference received a non-Vulkan texture handle.",
            &VKTexture::releaseCommandReference);
    }

    Texture VKDevice::findTextureHandle(const VKTexture *texture) const
    {
        if (texture == nullptr)
        {
            return {};
        }

        {
            ScopedLock lock(mResourceLifetimeMutex);
            for (const Texture &handle : mLiveTextures)
            {
                if (handle.get() == texture)
                {
                    return handle;
                }
            }
        }
        return mRetireManager.findPendingTextureHandle(texture);
    }

    Texture VKDevice::findTextureHandle(const VKTexture &texture) const
    {
        return findTextureHandle(&texture);
    }

    void VKDevice::releaseBufferHandle(Buffer buffer)
    {
        if (buffer.isNull())
        {
            return;
        }
        if (auto *bufferImpl = static_cast<VKBuffer *>(buffer.get()); bufferImpl != nullptr)
        {
            if (mMainQueue != nullptr)
            {
                mMainQueue->getResourceStateDB().discardBufferState(*bufferImpl);
            }
            bufferImpl->destroy();
        }
        mBufferPool.freeByHandle(buffer);
    }

    void VKDevice::releaseTextureHandle(Texture texture)
    {
        if (texture.isNull())
        {
            return;
        }
        if (auto *textureImpl = static_cast<VKTexture *>(texture.get()); textureImpl != nullptr)
        {
            if (mMainQueue != nullptr)
            {
                mMainQueue->getResourceStateDB().discardTextureState(*textureImpl);
            }
            textureImpl->destroy();
        }
        mTexturePool.freeByHandle(texture);
    }

    void VKDevice::releaseSamplerHandle(Sampler sampler)
    {
        if (sampler.isNull())
        {
            return;
        }
        if (auto *samplerImpl = static_cast<VKSampler *>(sampler.get()); samplerImpl != nullptr)
        {
            samplerImpl->destroy();
        }
        mSamplerPool.freeByHandle(sampler);
    }
} // namespace GVM::RHI::Vulkan
