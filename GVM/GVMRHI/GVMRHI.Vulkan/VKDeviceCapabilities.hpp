#pragma once

#include "VKCommon.hpp"

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    constexpr const char *FragmentShaderBarycentricExtensionName = VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME;
    constexpr const char *PerformanceQueryExtensionName = VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME;
    constexpr const char *PortabilitySubsetExtensionName = "VK_KHR_portability_subset";
    constexpr const char *Robustness2ExtensionName = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
    constexpr const char *MemoryBudgetExtensionName = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    constexpr const char *SwapchainExtensionName = "VK_KHR_swapchain";

    /** Feature and property snapshot collected from one candidate physical device before the logical device exists. */
    struct VulkanCapabilitySurvey
    {
        bool supportsRequiredSampledImageDescriptorIndexing = false;
        bool supportsRequiredVulkan13Features = false;
        bool supportsRobustBufferAccess = false;
        bool supportsRobustBufferAccess2 = false;
        bool supportsSamplerAnisotropy = false;
        bool supportsIndependentBlend = false;
        bool supportsMultiDrawIndirect = false;
        bool supportsDrawIndirectCount = false;
        bool supportsDrawIndirectFirstInstance = false;
        bool supportsShaderClipDistance = false;
        bool supportsVertexPipelineStoresAndAtomics = false;
        bool supportsFragmentStoresAndAtomics = false;
        bool supportsFragmentShaderBarycentric = false;
        bool supportsTessellationShaderFeature = false;
        bool supportsShaderFloat16 = false;
        bool supportsShaderSubgroupExtendedTypes = false;
        vk::PhysicalDevice16BitStorageFeatures storage16BitFeatures = {};
        bool supportsStorageImageExtendedFormats = false;
        bool supportsStorageImageArrayDynamicIndexing = false;
        bool supportsStorageImageArrayNonUniformIndexing = false;
        bool supportsStorageImageUpdateAfterBind = false;
        vk::PhysicalDeviceVulkan12Properties indexingProperties = {};
        bool supportsImageGatherExtended = false;
        float maxSamplerAnisotropy = 1.0f;
    };

    /** Returns a human-readable name for one Vulkan physical-device type, used in capability logs. */
    const char *physicalDeviceTypeName(vk::PhysicalDeviceType type);
    /** Returns the descriptor-indexing path name used in capability logs for one survey. */
    const char *descriptorIndexingPathName(const VulkanCapabilitySurvey &survey);
    /** Returns the shader-float16 path name used in capability logs for one survey. */
    const char *shaderFloat16PathName(const VulkanCapabilitySurvey &survey);
    /** Formats a list of enabled extension names as a comma-separated string for logging. */
    eastl::string joinDeviceExtensionNames(const eastl::vector<const char *> &extensions);

    /** Selected physical device, graphics queue family, and the full capability record produced by the
     *  init-time capability survey. VKDevice owns exactly one instance; the survey writes it once and
     *  noteLogicalDeviceCreated() records the capabilities that only exist after vkCreateDevice. */
    class VKDeviceCapabilities final
    {
    public:
        /** Enumerates the instance's physical devices, surveys each candidate, selects the best
         *  Vulkan 1.3 graphics-capable device, and stores its properties, queue family, format
         *  properties, memory properties, and timestamp support. Throws when no device qualifies. */
        void init(vk::Instance instance, VKDevice *logSubject);
        /** Reports whether the selected device exposes the named extension. */
        [[nodiscard]] bool hasSelectedDeviceExtension(const char *extensionName) const;
        /** Probes VK_KHR_performance_query support on the selected device, records the pass-counter
         *  support snapshot, appends the extension to enabledExtensions when usable, and returns the
         *  feature struct to append to the device feature chain when true. */
        [[nodiscard]] bool enablePassCounterQuery(
            eastl::vector<const char *> &enabledExtensions,
            vk::PhysicalDevicePerformanceQueryFeaturesKHR &outFeatures);
        /** Records the capabilities that become concrete only once the logical device exists: the
         *  enabled feature snapshots, subgroup properties, and the survey-derived support flags. */
        void noteLogicalDeviceCreated(const vk::PhysicalDeviceFeatures &enabledCoreFeatures);

        [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const { return mPhysicalDevice; }
        [[nodiscard]] const vk::PhysicalDeviceProperties &getPhysicalDeviceProperties() const { return mPhysicalDeviceProperties; }
        [[nodiscard]] const vk::PhysicalDeviceVulkan12Properties &getDescriptorIndexingProperties() const { return mDescriptorIndexingProperties; }
        [[nodiscard]] const vk::PhysicalDeviceSubgroupProperties &getSubgroupProperties() const { return mSubgroupProperties; }
        [[nodiscard]] const vk::PhysicalDevice16BitStorageFeatures &getEnabled16BitStorageFeatures() const { return mEnabled16BitStorageFeatures; }
        [[nodiscard]] const vk::PhysicalDeviceFeatures &getEnabledShaderFeatures() const { return mEnabledShaderFeatures; }
        [[nodiscard]] const DeviceMemoryProperties &getMemoryProperties() const { return mMemoryProperties; }
        [[nodiscard]] const TimestampQuerySupport &getTimestampQuerySupport() const { return mTimestampQuerySupport; }
        [[nodiscard]] const PassCounterQuerySupport &getPassCounterQuerySupport() const { return mPassCounterQuerySupport; }
        [[nodiscard]] const VulkanCapabilitySurvey &getSurvey() const { return mSurvey; }
        [[nodiscard]] const eastl::array<vk::FormatProperties, static_cast<size_t>(TextureFormat::ASTC12x12UnormSrgb) + 1u> &getTextureFormatProperties() const { return mTextureFormatProperties; }
        /** Returns the format capabilities for one texture format; throws std::invalid_argument for an out-of-range format. */
        [[nodiscard]] const vk::FormatProperties &getTextureFormatProperties(TextureFormat format) const;
        [[nodiscard]] uint32_t getGraphicsQueueFamilyIndex() const { return mGraphicsQueueFamilyIndex; }
        /** Returns the queue-family properties of the selected physical device. */
        [[nodiscard]] const eastl::vector<vk::QueueFamilyProperties> &getQueueFamilyProperties() const { return mQueueFamilyProperties; }
        [[nodiscard]] bool supportsSwapchain() const { return mSupportsSwapchain; }
        [[nodiscard]] bool supportsSamplerAnisotropy() const { return mSupportsSamplerAnisotropy; }
        [[nodiscard]] bool supportsFragmentShaderBarycentric() const { return mSupportsFragmentShaderBarycentric; }
        [[nodiscard]] bool supportsTessellationShaderFeature() const { return mSupportsTessellationShaderFeature; }
        [[nodiscard]] bool supportsShaderFloat16() const { return mSupportsShaderFloat16; }
        [[nodiscard]] bool supportsShaderSubgroupExtendedTypes() const { return mSupportsShaderSubgroupExtendedTypes; }
        [[nodiscard]] bool supportsDrawIndirectCount() const { return mSupportsDrawIndirectCount; }
        /** Reports whether VK_EXT_memory_budget is enabled on the logical device. */
        [[nodiscard]] bool supportsMemoryBudget() const { return mSupportsMemoryBudget; }
        [[nodiscard]] bool supportsStorageImageArrayNonUniformIndexing() const { return mStorageImageArrayNonUniformIndexing; }
        [[nodiscard]] bool supportsStorageImageUpdateAfterBind() const { return mStorageImageUpdateAfterBind; }
        [[nodiscard]] float getMaxSamplerAnisotropy() const { return mMaxSamplerAnisotropy; }
        /** Records whether the swapchain extension was selected during logical-device creation. */
        void setSupportsSwapchain(bool supported) { mSupportsSwapchain = supported; }

    private:
        vk::PhysicalDevice mPhysicalDevice;
        vk::PhysicalDeviceProperties mPhysicalDeviceProperties;
        vk::PhysicalDeviceVulkan12Properties mDescriptorIndexingProperties{};
        bool mStorageImageArrayNonUniformIndexing = false;
        bool mStorageImageUpdateAfterBind = false;
        eastl::vector<vk::QueueFamilyProperties> mQueueFamilyProperties;
        eastl::array<vk::FormatProperties, static_cast<size_t>(TextureFormat::ASTC12x12UnormSrgb) + 1u> mTextureFormatProperties{};
        eastl::vector<vk::ExtensionProperties> mSelectedDeviceExtensions;
        VulkanCapabilitySurvey mSurvey = {};
        uint32_t mGraphicsQueueFamilyIndex = 0;
        bool mSupportsSwapchain = false;
        bool mSupportsSamplerAnisotropy = false;
        bool mSupportsFragmentShaderBarycentric = false;
        bool mSupportsTessellationShaderFeature = false;
        bool mSupportsShaderFloat16 = false;
        bool mSupportsShaderSubgroupExtendedTypes = false;
        vk::PhysicalDevice16BitStorageFeatures mEnabled16BitStorageFeatures = {};
        vk::PhysicalDeviceFeatures mEnabledShaderFeatures = {};
        bool mSupportsDrawIndirectCount = false;
        bool mSupportsMemoryBudget = false;
        vk::PhysicalDeviceSubgroupProperties mSubgroupProperties = {};
        TimestampQuerySupport mTimestampQuerySupport = {};
        PassCounterQuerySupport mPassCounterQuerySupport = {};
        DeviceMemoryProperties mMemoryProperties = {};
        float mMaxSamplerAnisotropy = 1.0f;
    };
} // namespace GVM::RHI::Vulkan
