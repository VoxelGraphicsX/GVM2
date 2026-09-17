#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan::Detail
{
    struct ReflectedDescriptorBinding
    {
        eastl::string name;
        uint32_t set = 0;
        uint32_t binding = 0;
        vk::DescriptorType descriptorType = vk::DescriptorType::eSampler;
        uint32_t descriptorCount = 1;
        bool isRuntimeDescriptorArray = false;
        TextureViewDimension textureViewDimension = TextureViewDimension::Undefined;
        vk::Format imageFormat = vk::Format::eUndefined;
        StorageBufferAccess storageBufferAccess = StorageBufferAccess::Undefined;
        StorageTextureAccess storageTextureAccess = StorageTextureAccess::Undefined;
        uint32_t inputAttachmentIndex = 0;
    };

    struct ReflectedInterfaceVariable
    {
        eastl::string name;
        uint32_t location = 0;
        vk::Format format = vk::Format::eUndefined;
    };

    struct ReflectedEntryPoint
    {
        eastl::string name;
        vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eVertex;
        eastl::vector<ReflectedDescriptorBinding> descriptorBindings;
        eastl::vector<ReflectedInterfaceVariable> inputVariables;
        eastl::vector<ReflectedInterfaceVariable> outputVariables;
        uint32_t localSizeX = 0;
        uint32_t localSizeY = 0;
        uint32_t localSizeZ = 0;
        bool usesPushConstants = false;
        uint32_t functionId = 0;
        bool usesSubgroupOperations = false;
    };

    struct ReflectedShaderModule
    {
        eastl::vector<ReflectedEntryPoint> entryPoints;
        bool usesGeometryCapability = false;
        bool usesTessellationCapability = false;
        bool usesFloat16Capability = false;
        bool usesStorageBuffer16BitAccess = false;
        bool usesUniformAndStorageBuffer16BitAccess = false;
        bool usesStoragePushConstant16 = false;
        bool usesStorageInputOutput16 = false;
        bool usesStorageImageExtendedFormats = false;
        bool usesImageGatherExtended = false;
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
    /** Rejects image and subgroup requirements unsupported by the enabled features or entry-point stage. */
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
    void validatePipelineLayoutAgainstEntryPoint(
        const VKPipelineLayout &pipelineLayout,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName);
    void validateVertexInputsAgainstEntryPoint(
        const VertexState &vertexState,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName);
    void validateStageInterface(
        const ReflectedEntryPoint &producerEntryPoint,
        const ReflectedEntryPoint &consumerEntryPoint,
        const char *ownerName);
    void validateFragmentTargetsAgainstEntryPoint(
        const FragmentState &fragmentState,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName);
} // namespace GVM::RHI::Vulkan::Detail
