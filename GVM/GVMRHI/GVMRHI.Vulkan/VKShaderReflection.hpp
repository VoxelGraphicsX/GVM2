#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan::Detail
{
    struct ReflectedDescriptorBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        vk::DescriptorType descriptorType = vk::DescriptorType::eSampler;
        StorageBufferAccess storageBufferAccess = StorageBufferAccess::Undefined;
        StorageTextureAccess storageTextureAccess = StorageTextureAccess::Undefined;
        uint32_t inputAttachmentIndex = 0;
    };

    struct ReflectedEntryPoint
    {
        eastl::string name;
        vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eVertex;
        eastl::vector<ReflectedDescriptorBinding> descriptorBindings;
        uint32_t localSizeX = 0;
        uint32_t localSizeY = 0;
        uint32_t localSizeZ = 0;
        bool usesPushConstants = false;
        bool usesArgumentAddressing = false;
        bool hasValidArgumentRoot = false;
        uint32_t functionId = 0;
        bool usesSubgroupOperations = false;
    };

    struct ReflectedShaderModule
    {
        eastl::vector<ReflectedEntryPoint> entryPoints;
        bool usesGeometryCapability = false;
        bool usesPhysicalStorageBuffers = false;
        bool usesTessellationCapability = false;
        bool usesFloat16Capability = false;
        bool usesStorageBuffer16BitAccess = false;
        bool usesUniformAndStorageBuffer16BitAccess = false;
        bool usesStoragePushConstant16 = false;
        bool usesStorageInputOutput16 = false;
        bool usesStorageImageExtendedFormats = false;
        bool usesStorageImageArrayDynamicIndexing = false;
        bool usesStorageImageArrayNonUniformIndexing = false;
        bool usesImageGatherExtended = false;
        /** Requires user clipping before rasterization rather than fragment discard. */
        bool usesClipDistance = false;
        bool usesSubgroupExtendedTypes = false;
        vk::SubgroupFeatureFlags requiredSubgroupOperations = {};
        bool usesFragmentShaderBarycentricCapability = false;
        bool usesFragmentShaderBarycentricExtension = false;
    };

    ReflectedShaderModule reflectShaderModule(const eastl::vector<uint32_t> &spirv);
    /** Rejects modules whose independent 16-bit storage requirements were not enabled on the device. */
    void validate16BitStorageRequirements(const ReflectedShaderModule &reflection,
                                          const vk::PhysicalDevice16BitStorageFeatures &enabledFeatures,
                                          const eastl::string &shaderLabel);
    /** Rejects image, graphics storage, and subgroup requirements unsupported by enabled features or entry stages. */
    void validateImageAndSubgroupRequirements(const ReflectedShaderModule &reflection,
                                             const vk::PhysicalDeviceFeatures &enabledFeatures,
                                             const vk::PhysicalDeviceSubgroupProperties &subgroupProperties,
                                             bool shaderSubgroupExtendedTypes,
                                             const eastl::string &shaderLabel);
    const ReflectedEntryPoint &requireShaderEntryPoint(
        const ReflectedShaderModule &reflection,
        const eastl::string &entryPoint,
        vk::ShaderStageFlagBits expectedStage,
        const char *ownerName);
    /** Checks only the argument-buffer ABI: Args layouts require the BDA root and set-zero table, and non-Args layouts reject push-constant or argument addressing. */
    void validateArgumentAbiAgainstEntryPoint(
        const VKPipelineLayout &pipelineLayout,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName);
} // namespace GVM::RHI::Vulkan::Detail
