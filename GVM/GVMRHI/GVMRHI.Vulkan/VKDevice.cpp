#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"

#include "VKBindGroup.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKBindingUtils.hpp"
#include "VKBuffer.hpp"
#include "VKComputePipeline.hpp"
#include "VKDiagnosticsOverlayShaders.hpp"
#include "VKInstance.hpp"
#include "VKLogging.hpp"
#include "VKPipelineLayout.hpp"
#include "VKQuerySet.hpp"
#include "VKQueue.hpp"
#include "VKRenderPipeline.hpp"
#include "VKRenderToSwapchainShaders.hpp"
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
#include <EASTL/atomic.h>
#include <sstream>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view DeviceLogCategory = "gvmrhi.vulkan.device";

    } // namespace

    namespace
    {
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
                throw std::invalid_argument((
                    "VKDevice::createBindGroupLayout failed for layout '" +
                    eastl::string(descriptor.label.c_str()) +
                    "': " + e.what()).c_str());
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
                throw std::invalid_argument((
                    "VKDevice::createPipelineLayout failed for layout '" +
                    eastl::string(descriptor.label.c_str()) +
                    "': " + e.what()).c_str());
            }
            return layout;
        }
    } // namespace

    void VKDevice::setDebugObjectName(vk::ObjectType type, uint64_t handle, const char *name) const
    {
        if (mInstance == nullptr || !mInstance->supportsDebugUtils() || handle == 0 || name == nullptr || name[0] == '\0')
        {
            return;
        }
        VkDebugUtilsObjectNameInfoEXT info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = static_cast<VkObjectType>(type);
        info.objectHandle = handle;
        info.pObjectName = name;
        vkSetDebugUtilsObjectNameEXT(static_cast<VkDevice>(mDevice.get()), &info);
    }

    void VKDevice::init(VKInstance *instance)
    {
        if (instance == nullptr)
        {
            throw std::invalid_argument("VKDevice::init requires a valid instance.");
        }

        mInstance = instance;
        mLogContext = instance->getLogContext();
        mLostState.setCallback(instance->getDescriptor().deviceLostCallback);
        mLogger = instance->getLogger();
        mDiagnosticsOverlayConfig = instance->getDescriptor().diagnosticsOverlay;
        if (!mLogger && mLogContext)
        {
            mLogger = Internal::createPublicLogger(mLogContext);
        }
        GVMLogInfo(this, DeviceLogCategory, "event=device_init_begin");
        mPresentManager.reset();

        selectAndSurveyPhysicalDevice();
        createNativeDevice();
        createVulkanMemoryAllocator();
        initResourcePoolsAndCaches();
        createMainQueueAndOverlay();
        initFullscreenTextureExecutor();
        logDeviceInitEnd();
    }

    void VKDevice::selectAndSurveyPhysicalDevice()
    {
        mCapabilities.init(mInstance->getNativeInstance(), this);
    }

    void VKDevice::createNativeDevice()
    {
        const VulkanCapabilitySurvey &capabilitySurvey = mCapabilities.getSurvey();
        const float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.queueFamilyIndex = mCapabilities.getGraphicsQueueFamilyIndex();
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        eastl::vector<const char *> enabledDeviceExtensions;
        if (mCapabilities.hasSelectedDeviceExtension(PortabilitySubsetExtensionName))
        {
            enabledDeviceExtensions.push_back(PortabilitySubsetExtensionName);
        }
        if (mCapabilities.hasSelectedDeviceExtension(SwapchainExtensionName))
        {
            enabledDeviceExtensions.push_back(SwapchainExtensionName);
            mCapabilities.setSupportsSwapchain(true);
        }


        vk::PhysicalDeviceVulkan12Features enabledVulkan12Features = {};
        vk::PhysicalDeviceVulkan13Features enabledVulkan13Features = {};
        enabledVulkan13Features.synchronization2 = VK_TRUE;
        vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR enabledBarycentricFeatures = {};
        vk::PhysicalDevicePerformanceQueryFeaturesKHR enabledPerformanceQueryFeatures = {};
        vk::PhysicalDeviceRobustness2FeaturesEXT enabledRobustness2Features = {};
        vk::PhysicalDeviceFeatures2 enabledFeatures2 = {};
        enabledFeatures2.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
        enabledFeatures2.features.shaderStorageImageArrayDynamicIndexing = capabilitySurvey.supportsStorageImageArrayDynamicIndexing;
        enabledFeatures2.features.robustBufferAccess = capabilitySurvey.supportsRobustBufferAccess ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.samplerAnisotropy = capabilitySurvey.supportsSamplerAnisotropy ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.independentBlend = capabilitySurvey.supportsIndependentBlend ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.shaderStorageImageExtendedFormats = capabilitySurvey.supportsStorageImageExtendedFormats ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.shaderImageGatherExtended = capabilitySurvey.supportsImageGatherExtended ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.multiDrawIndirect = capabilitySurvey.supportsMultiDrawIndirect ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.drawIndirectFirstInstance = capabilitySurvey.supportsDrawIndirectFirstInstance ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.shaderClipDistance = capabilitySurvey.supportsShaderClipDistance ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.vertexPipelineStoresAndAtomics = capabilitySurvey.supportsVertexPipelineStoresAndAtomics ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.fragmentStoresAndAtomics = capabilitySurvey.supportsFragmentStoresAndAtomics ? VK_TRUE : VK_FALSE;
        enabledFeatures2.features.tessellationShader = capabilitySurvey.supportsTessellationShaderFeature ? VK_TRUE : VK_FALSE;

        void *featureChainHead = nullptr;
        vk::PhysicalDevice16BitStorageFeatures enabled16BitStorageFeatures = capabilitySurvey.storage16BitFeatures;
        appendFeatureChainStruct(featureChainHead, enabled16BitStorageFeatures);
        enabledVulkan12Features.descriptorIndexing = VK_TRUE;
        enabledVulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        enabledVulkan12Features.runtimeDescriptorArray = VK_TRUE;
        enabledVulkan12Features.timelineSemaphore = VK_TRUE;
        enabledVulkan12Features.bufferDeviceAddress = VK_TRUE;
        enabledVulkan12Features.drawIndirectCount = capabilitySurvey.supportsDrawIndirectCount;
        enabledVulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        enabledVulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        enabledVulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
        enabledVulkan12Features.shaderStorageImageArrayNonUniformIndexing = capabilitySurvey.supportsStorageImageArrayNonUniformIndexing;
        enabledVulkan12Features.descriptorBindingStorageImageUpdateAfterBind = capabilitySurvey.supportsStorageImageUpdateAfterBind;

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

        if (mCapabilities.enablePassCounterQuery(enabledDeviceExtensions, enabledPerformanceQueryFeatures))
        {
            appendFeatureChainStruct(featureChainHead, enabledPerformanceQueryFeatures);
        }

        if (capabilitySurvey.supportsRobustBufferAccess2)
        {
            enabledDeviceExtensions.push_back(Robustness2ExtensionName);
            enabledRobustness2Features.robustBufferAccess2 = VK_TRUE;
            appendFeatureChainStruct(featureChainHead, enabledRobustness2Features);
        }

        appendFeatureChainStruct(featureChainHead, enabledVulkan12Features);
        appendFeatureChainStruct(featureChainHead, enabledVulkan13Features);

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
            mCapabilities.supportsSwapchain(),
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

        mDevice = mCapabilities.getPhysicalDevice().createDeviceUnique(deviceCreateInfo);
        volkLoadDevice(static_cast<VkDevice>(mDevice.get()));
        VULKAN_HPP_DEFAULT_DISPATCHER.init(mDevice.get());
        mCapabilities.noteLogicalDeviceCreated(enabledFeatures2.features);
    }

    void VKDevice::createVulkanMemoryAllocator()
    {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(mCapabilities.getPhysicalDevice());
        allocatorCreateInfo.device = static_cast<VkDevice>(mDevice.get());
        allocatorCreateInfo.instance = static_cast<VkInstance>(mInstance->getNativeInstance());
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorCreateInfo.vulkanApiVersion = VulkanApiVersion;
        allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
            (mCapabilities.supportsMemoryBudget() ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0u);

        if (vmaCreateAllocator(&allocatorCreateInfo, &mAllocator) != VK_SUCCESS)
        {
            throw std::runtime_error("VKDevice::init failed to create the VMA allocator.");
        }

        mArgumentResources.createPagePool();
    }

    void VKDevice::initResourcePoolsAndCaches()
    {
        mResourceRegistry.init();
        mDescriptorPoolStore.init(this);
        mRenderPassFramebufferCache.init(mDevice.get(), mLogger);
        const eastl::vector<uint8_t> &initialPipelineCacheData = mInstance->getDescriptor().pipelineCache.initialData;
        mPipelineCache.init(mDevice.get(), mCapabilities.getPhysicalDeviceProperties(),
            eastl::span<const uint8_t>(initialPipelineCacheData.data(), initialPipelineCacheData.size()));
        GVMLogInfo(this, DeviceLogCategory, "event=pipeline_cache_init initial_bytes={} loaded={}",
            initialPipelineCacheData.size(), mPipelineCache.loadedInitialData());
    }

    void VKDevice::createMainQueueAndOverlay()
    {
        mMainQueue = new VKQueue();
        try
        {
            mMainQueue->init(this, mCapabilities.getGraphicsQueueFamilyIndex(), mDevice->getQueue(mCapabilities.getGraphicsQueueFamilyIndex(), 0), "MainQueue");
            if (mDiagnosticsOverlayConfig.enabled != False)
            {
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
                mDiagnosticsOverlayQueue = GVM::RHI::Private::createDiagnosticsOverlayQueue(this, mMainQueue, createVulkanDiagnosticsOverlayShaderProvider());
#else
                throw std::runtime_error("Diagnostics overlay was requested, but this build was compiled with GVM_DIAGNOSTICS_OVERLAY_ENABLED=0.");
#endif
            }
        }
        catch (...)
        {
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
#endif
            delete mMainQueue;
            mMainQueue = nullptr;
            throw;
        }
    }

    void VKDevice::initFullscreenTextureExecutor()
    {
        const ShaderModule fullscreenVertexShader = createShaderModule({
            .label = "FullscreenTextureVertexShader",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainVertexShaderSpirv,
                Detail::RenderToSwapchainVertexShaderSpirv + Detail::RenderToSwapchainVertexShaderSpirvWordCount),
        });
        const ShaderModule fullscreenFragmentShader = createShaderModule({
            .label = "FullscreenTextureFragmentShader",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainFragmentShaderSpirv,
                Detail::RenderToSwapchainFragmentShaderSpirv + Detail::RenderToSwapchainFragmentShaderSpirvWordCount),
        });
        const ShaderModule fullscreenFlippedFragmentShader = createShaderModule({
            .label = "FullscreenTextureFragmentShaderFlipped",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainFragmentFlippedShaderSpirv,
                Detail::RenderToSwapchainFragmentFlippedShaderSpirv + Detail::RenderToSwapchainFragmentFlippedShaderSpirvWordCount),
        });
        mFullscreenTextureExecutor.init(*this, {
            .vertex = fullscreenVertexShader,
            .vertexEntryPoint = "vertexMain",
            .fragment = fullscreenFragmentShader,
            .fragmentEntryPoint = "fragmentMain",
            .flippedFragment = fullscreenFlippedFragmentShader,
            .flippedFragmentEntryPoint = "fragmentMainFlipped",
        });
    }

    void VKDevice::logDeviceInitEnd()
    {
        const VulkanCapabilitySurvey &capabilitySurvey = mCapabilities.getSurvey();
        GVMLogInfo(
            this, DeviceLogCategory,
            "event=device_init_end queue_family_index={} supports_swapchain={} robust_buffer_access={} robust_buffer_access2={} max_sampler_anisotropy={}",
            mCapabilities.getGraphicsQueueFamilyIndex(),
            mCapabilities.supportsSwapchain(),
            capabilitySurvey.supportsRobustBufferAccess,
            capabilitySurvey.supportsRobustBufferAccess2,
            mCapabilities.getMaxSamplerAnisotropy());
    }

    vk::Device VKDevice::getNativeDevice() const
    {
        return mDevice.get();
    }

    vk::PhysicalDevice VKDevice::getPhysicalDevice() const
    {
        return mCapabilities.getPhysicalDevice();
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
        return mCapabilities.getMemoryProperties();
    }

    DeviceMemoryBudget VKDevice::getMemoryBudget() const
    {
        ensureAlive("VKDevice::getMemoryBudget");
        const vk::PhysicalDeviceMemoryProperties memoryProperties = mCapabilities.getPhysicalDevice().getMemoryProperties();
        DeviceMemoryBudget budget = {};
        budget.reportedByDriver = mCapabilities.supportsMemoryBudget();
        if (mAllocator == nullptr || memoryProperties.memoryHeapCount == 0u)
        {
            return budget;
        }
        eastl::vector<VmaBudget> heapBudgets(memoryProperties.memoryHeapCount);
        vmaGetHeapBudgets(mAllocator, heapBudgets.data());
        budget.heaps.resize(memoryProperties.memoryHeapCount);
        for (uint32_t index = 0u; index < memoryProperties.memoryHeapCount; ++index)
        {
            MemoryHeapBudget &heap = budget.heaps[index];
            heap.budgetBytes = heapBudgets[index].budget;
            heap.usageBytes = heapBudgets[index].usage;
            heap.deviceLocal = (memoryProperties.memoryHeaps[index].flags & vk::MemoryHeapFlagBits::eDeviceLocal) != vk::MemoryHeapFlags{};
        }
        return budget;
    }

    /** Returns the diagnostics overlay configuration captured from the owning Vulkan instance. */
    RuntimeDiagnosticsOverlayConfig VKDevice::getDiagnosticsOverlayConfig() const
    {
        ensureAlive("VKDevice::getDiagnosticsOverlayConfig");
        return mDiagnosticsOverlayConfig;
    }

    /** Returns a snapshot of live Vulkan resources for runtime diagnostics. */
    eastl::vector<uint8_t> VKDevice::getPipelineCacheData() const
    {
        return mPipelineCache.serialize();
    }

    vk::PipelineCache VKDevice::getNativePipelineCache() const
    {
        return mPipelineCache.getNativePipelineCache();
    }

    DiagnosticsResourceSnapshot VKDevice::getDiagnosticsResourceSnapshot() const
    {
        ensureAlive("VKDevice::getDiagnosticsResourceSnapshot");
        return mResourceRegistry.snapshot();
    }

    VmaAllocator VKDevice::getAllocator() const
    {
        return mAllocator;
    }

    VKQueue *VKDevice::getMainQueueImpl() const
    {
        return mMainQueue;
    }

    VKResourceRegistry &VKDevice::getResourceRegistry()
    {
        return mResourceRegistry;
    }

    VKRenderPassFramebufferCache &VKDevice::getRenderPassFramebufferCache()
    {
        return mRenderPassFramebufferCache;
    }

    VKPresentManager &VKDevice::getPresentManager()
    {
        return mPresentManager;
    }

    const VKPresentManager &VKDevice::getPresentManager() const
    {
        return mPresentManager;
    }

    Private::FullscreenTextureExecutor &VKDevice::getFullscreenTextureExecutor()
    {
        return mFullscreenTextureExecutor;
    }













    Queue VKDevice::getMainQueue() const
    {
        ensureAlive("VKDevice::getMainQueue");
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
        return mDiagnosticsOverlayQueue != nullptr ? mDiagnosticsOverlayQueue : mMainQueue;
#else
        return mMainQueue;
#endif
    }

    TimestampQuerySupport VKDevice::getTimestampQuerySupport() const
    {
        ensureAlive("VKDevice::getTimestampQuerySupport");
        return mCapabilities.getTimestampQuerySupport();
    }

    PassCounterQuerySupport VKDevice::getPassCounterQuerySupport() const
    {
        ensureAlive("VKDevice::getPassCounterQuerySupport");
        return mCapabilities.getPassCounterQuerySupport();
    }

    void VKDevice::destroy()
    {
        GVMLogInfo(this, DeviceLogCategory, "event=device_destroy_begin");
        if (mResourceRegistry.beginDestroy())
        {
            return;
        }

        if (mDevice)
        {
            GVMLogWarn(this, DeviceLogCategory, "event=device_wait_idle_begin");
            try
            {
                mDevice->waitIdle();
            }
            catch (const vk::DeviceLostError &error)
            {
                mLostState.markLost(DeviceLostReason::GpuFault, error.what());
                GVMLogWarn(this, DeviceLogCategory, "event=device_wait_idle_device_lost reason={}", error.what());
            }
            GVMLogWarn(this, DeviceLogCategory, "event=device_wait_idle_end");
        }

        if (mMainQueue != nullptr)
        {
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
#endif
            mMainQueue->destroy();
            delete mMainQueue;
            mMainQueue = nullptr;
        }

        mResourceRegistry.collectReleasedResources();
        mResourceRegistry.releaseAll();
        mPresentManager.reset();
        mFullscreenTextureExecutor = Private::FullscreenTextureExecutor{};

        // Drain pooled Args pages while the VMA allocator is still alive so they return
        // their buffers before allocator teardown; snapshots may keep the pool alive longer.
        mArgumentResources.destroyFreePages();

        if (mAllocator != nullptr)
        {
            vmaDestroyAllocator(mAllocator);
            mAllocator = nullptr;
        }

        mObjectCache.clear();
        {
            ScopedLock lock(mDescriptorPoolMutex);
            mDescriptorPoolStore.destroy();
        }
        mRenderPassFramebufferCache.destroy();
        mPipelineCache.destroy();
        mArgumentResources.destroyTable();
        mDevice.reset();
        mCapabilities = VKDeviceCapabilities{};
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
        if (mResourceRegistry.isDestroyed() || !mDevice)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on a destroyed Vulkan device.").c_str());
        }
    }

    void VKDevice::ensureUsable(const char *apiName) const
    {
        ensureAlive(apiName);
        mLostState.throwIfLost(apiName);
    }

    DeviceLostState &VKDevice::getLostState()
    {
        return mLostState;
    }

    eastl::optional<DeviceLostInfo> VKDevice::getLostInfo() const
    {
        return mLostState.getInfo();
    }

    ComputePipeline VKDevice::createComputePipeline(const ComputePipelineDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createComputePipeline");
        if (ComputePipeline cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        return mObjectCache.insert(descriptor, createComputePipelineHandle(*this, descriptor));
    }

    RenderPipeline VKDevice::createRenderPipeline(const RenderPipelineDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createRenderPipeline");
        if (RenderPipeline cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        return mObjectCache.insert(descriptor, createRenderPipelineHandle(*this, descriptor));
    }

    ShaderModule VKDevice::createShaderModule(const ShaderModuleDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createShaderModule");
        if (ShaderModule cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        return mObjectCache.insert(descriptor, createShaderModuleHandle(*this, descriptor));
    }

    BindGroupLayout VKDevice::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createBindGroupLayout");
        if (BindGroupLayout cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        return mObjectCache.insert(descriptor, createBindGroupLayoutHandle(*this, descriptor));
    }

    PipelineLayout VKDevice::createPipelineLayout(const PipelineLayoutDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createPipelineLayout");
        const auto &arguments = descriptor.arguments;
        if (arguments.byteSize == 0 && (!arguments.buffers.empty() || !arguments.resources.empty()))
        {
            throw std::invalid_argument("A zero-byte Vulkan Args layout cannot contain shader resource fields.");
        }
        if (arguments.byteSize != 0)
        {
            if (!descriptor.bindGroupLayouts.empty() || arguments.byteSize > 1024u * 1024u || arguments.byteSize % 8 != 0)
            {
                throw std::invalid_argument("Vulkan Args require a bounded root layout without legacy BindGroup slots.");
            }
            for (const auto &field : arguments.buffers)
            {
                if (field.addressOffset > arguments.byteSize || arguments.byteSize - field.addressOffset < 8 ||
                    field.lengthOffset > arguments.byteSize || arguments.byteSize - field.lengthOffset < 8 ||
                    field.addressOffset % 8 != 0 || field.lengthOffset % 8 != 0 || field.addressOffset == field.lengthOffset ||
                    (field.type != BufferBindingType::Uniform && field.type != BufferBindingType::Storage && field.type != BufferBindingType::ReadOnlyStorage))
                {
                    throw std::invalid_argument("Vulkan Args buffer metadata is invalid.");
                }
                if (field.type == BufferBindingType::Storage &&
                    (((field.visibility & ShaderStage::Vertex) != 0 && !mCapabilities.getEnabledShaderFeatures().vertexPipelineStoresAndAtomics) ||
                     ((field.visibility & ShaderStage::Fragment) != 0 && !mCapabilities.getEnabledShaderFeatures().fragmentStoresAndAtomics)))
                {
                    throw std::invalid_argument("Writable Args buffers require storage writes to be enabled for every visible graphics stage.");
                }
            }
            for (const auto &field : arguments.resources)
            {
                const auto &entry = field.entry;
                const uint32_t kinds = uint32_t(entry.sampler.type != SamplerBindingType::Undefined) +
                    uint32_t(entry.texture.sampleType != TextureSampleType::Undefined) +
                    uint32_t(entry.storageTexture.access != StorageTextureAccess::Undefined);
                if (field.offset > arguments.byteSize || arguments.byteSize - field.offset < 8 || field.offset % 8 != 0 ||
                    entry.buffer.type != BufferBindingType::Undefined || kinds != 1 || entry.maxCount != 1)
                {
                    throw std::invalid_argument("Vulkan Args texture or sampler metadata is invalid.");
                }
            }
        }
        if (descriptor.bindGroupLayouts.size() > VKMaxBindGroupCount)
        {
            throw std::invalid_argument("VKDevice::createPipelineLayout supports at most 32 bind group layouts.");
        }
        if (PipelineLayout cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        return mObjectCache.insert(descriptor, createPipelineLayoutHandle(*this, descriptor));
    }

    Arguments VKDevice::createArguments(const ArgumentsDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createArguments");
        auto snapshot = eastl::make_intrusive<VKArguments>();
        snapshot->init(*this, descriptor);
        return snapshot;
    }

    void VKDevice::setArgumentMemoryBudget(uint64_t bytes) { mArgumentResources.setBudget(bytes); }

    VKArgumentResources &VKDevice::getArgumentResources()
    {
        return mArgumentResources;
    }

    BindGroup VKDevice::createBindGroup(const BindGroupDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createBindGroup");

        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_create_bind_group_no_cache label={} layout_ptr={}",
            safeLogLabel(descriptor.label),
            static_cast<void *>(descriptor.layout.get()));
        BindGroup bindGroup = new VKBindGroup();
        {
            static_cast<VKBindGroup *>(bindGroup.get())->init(this, descriptor);
        }
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_create_bind_group_end label={} bind_group_ptr={}",
            safeLogLabel(descriptor.label),
            static_cast<void *>(bindGroup.get()));
        mDescriptorChurnCounters.recordBindGroupCreated();
        return bindGroup;
    }

    vk::DescriptorSet VKDevice::allocateDescriptorSet(
        vk::DescriptorSetLayout layout,
        const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
        vk::DescriptorPoolCreateFlags poolFlags,
        vk::DescriptorPool &owningPool)
    {
        ensureAlive("VKDevice::allocateDescriptorSet");
        ScopedLock lock(mDescriptorPoolMutex);
        const vk::DescriptorSet descriptorSet = mDescriptorPoolStore.allocateDescriptorSet(
            layout,
            poolSizesPerSet,
            poolFlags,
            owningPool,
            getDescriptorPoolReuseRetiredSubmissionId());
        GVMLogDebug(
            this, DeviceLogCategory,
            "event=device_allocate_descriptor_set_end descriptor_set_ptr={} owning_pool_ptr={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(owningPool)));
        mDescriptorChurnCounters.recordDescriptorSetAllocated();
        return descriptorSet;
    }

    void VKDevice::freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet)
    {
        if (mResourceRegistry.isDestroyed())
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
        ScopedLock lock(mDescriptorPoolMutex);
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
        mDescriptorChurnCounters.recordDescriptorSetFreed();
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



    Buffer VKDevice::createBuffer(const BufferDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createBuffer");

        auto *bufferImpl = new VKBuffer();
        try
        {
            bufferImpl->init(*this, descriptor);
            return mResourceRegistry.adoptBuffer(*bufferImpl);
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
        ensureUsable("VKDevice::createQuerySet");
        QuerySet querySet = new VKQuerySet();
        static_cast<VKQuerySet *>(querySet.get())->init(*this, descriptor);
        return querySet;
    }

    Texture VKDevice::createTexture(const TextureDescriptor &descriptor)
    {
        ensureUsable("VKDevice::createTexture");

        auto *textureImpl = new VKTexture();
        try
        {
            textureImpl->init(this, descriptor);
            return mResourceRegistry.adoptTexture(*textureImpl);
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
        ensureUsable("VKDevice::createSwapchainImageTexture");

        auto *textureImpl = new VKTexture();
        try
        {
            textureImpl->initExternalImage(this, descriptor, image);
            textureImpl->setSteadyStateLayout(vk::ImageLayout::ePresentSrcKHR);
            return mResourceRegistry.adoptTexture(*textureImpl);
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
        ensureUsable("VKDevice::createSampler");

        auto *samplerImpl = new VKSampler();
        try
        {
            samplerImpl->init(*this, descriptor);
            return mResourceRegistry.adoptSampler(*samplerImpl);
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
        mResourceRegistry.freeBuffer(buffer);
    }

    void VKDevice::freeTexture(Texture texture)
    {
        mResourceRegistry.freeTexture(texture);
    }

    void VKDevice::freeSampler(Sampler sampler)
    {
        mResourceRegistry.freeSampler(sampler);
    }


    VKDescriptorChurnCounters &VKDevice::getDescriptorChurnCounters()
    {
        return mDescriptorChurnCounters;
    }

} // namespace GVM::RHI::Vulkan
