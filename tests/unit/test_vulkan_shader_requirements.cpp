#include <gtest/gtest.h>
#include "VKShaderReflection.hpp"
#include <stdexcept>

using namespace GVM::RHI::Vulkan::Detail;

/** User clipping requires its own device feature, independently of image capabilities. */
TEST(VulkanShaderRequirements, RequiresEnabledClipDistance)
{
    ReflectedShaderModule reflection;
    reflection.usesClipDistance = true;
    vk::PhysicalDeviceFeatures enabled;
    vk::PhysicalDeviceSubgroupProperties subgroups;
    EXPECT_THROW(validateImageAndSubgroupRequirements(
        reflection, enabled, subgroups, false, "page-clipping"), std::invalid_argument);
    enabled.shaderClipDistance = VK_TRUE;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(
        reflection, enabled, subgroups, false, "page-clipping"));
}

TEST(VulkanShaderRequirements, HalfArithmeticDoesNotAuthorizeHalfStorage)
{
    ReflectedShaderModule reflection;
    reflection.usesFloat16Capability = true;
    reflection.usesStorageBuffer16BitAccess = true;
    vk::PhysicalDevice16BitStorageFeatures enabled;
    EXPECT_THROW(validate16BitStorageRequirements(reflection, enabled, "half-storage"), std::invalid_argument);
    enabled.storageBuffer16BitAccess = VK_TRUE;
    EXPECT_NO_THROW(validate16BitStorageRequirements(reflection, enabled, "half-storage"));
    reflection.usesUniformAndStorageBuffer16BitAccess = true;
    EXPECT_THROW(validate16BitStorageRequirements(reflection, enabled, "half-uniform"), std::invalid_argument);
    enabled.uniformAndStorageBuffer16BitAccess = VK_TRUE;
    EXPECT_NO_THROW(validate16BitStorageRequirements(reflection, enabled, "half-uniform"));
    reflection.usesStorageInputOutput16 = true;
    EXPECT_THROW(validate16BitStorageRequirements(reflection, enabled, "half-interface"), std::invalid_argument);
    enabled.storageInputOutput16 = VK_TRUE;
    reflection.usesStoragePushConstant16 = true;
    EXPECT_THROW(validate16BitStorageRequirements(reflection, enabled, "half-push"), std::invalid_argument);
    enabled.storagePushConstant16 = VK_TRUE;
    EXPECT_NO_THROW(validate16BitStorageRequirements(reflection, enabled, "half-all"));
}

TEST(VulkanShaderRequirements, RequiresActualImageFeatures)
{
    ReflectedShaderModule reflection;
    vk::PhysicalDeviceFeatures enabled;
    vk::PhysicalDeviceSubgroupProperties subgroups;
    reflection.usesStorageImageExtendedFormats = true;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "images"), std::invalid_argument);
    enabled.shaderStorageImageExtendedFormats = VK_TRUE;
    reflection.usesImageGatherExtended = true;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "gather"), std::invalid_argument);
    enabled.shaderImageGatherExtended = VK_TRUE;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "images"));
}

TEST(VulkanShaderRequirements, ChecksSubgroupOperationsAndOnlyParticipatingStages)
{
    ReflectedShaderModule reflection;
    reflection.requiredSubgroupOperations = vk::SubgroupFeatureFlagBits::eBasic | vk::SubgroupFeatureFlagBits::eBallot;
    ReflectedEntryPoint compute;
    compute.name = "compute";
    compute.stage = vk::ShaderStageFlagBits::eCompute;
    compute.usesSubgroupOperations = true;
    ReflectedEntryPoint vertex;
    vertex.name = "vertex";
    vertex.stage = vk::ShaderStageFlagBits::eVertex;
    reflection.entryPoints = {compute, vertex};
    vk::PhysicalDeviceFeatures enabled;
    vk::PhysicalDeviceSubgroupProperties subgroups;
    subgroups.supportedOperations = vk::SubgroupFeatureFlagBits::eBasic;
    subgroups.supportedStages = vk::ShaderStageFlagBits::eCompute;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "subgroups"), std::invalid_argument);
    subgroups.supportedOperations |= vk::SubgroupFeatureFlagBits::eBallot;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "subgroups"));
    reflection.entryPoints[1].usesSubgroupOperations = true;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "subgroups"), std::invalid_argument);
}

TEST(VulkanShaderRequirements, HalfSubgroupDataRequiresExtendedTypes)
{
    ReflectedShaderModule reflection;
    reflection.usesFloat16Capability = true;
    reflection.usesSubgroupExtendedTypes = true;
    vk::PhysicalDeviceFeatures enabled;
    vk::PhysicalDeviceSubgroupProperties subgroups;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "half-subgroup"), std::invalid_argument);
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, true, "half-subgroup"));
}

/** Requires only the storage-write feature for each participating graphics stage, while preserving readonly access. */
TEST(VulkanShaderRequirements, SeparatesVertexAndFragmentStorageWriteCapabilities)
{
    ReflectedShaderModule reflection;
    ReflectedEntryPoint entry;
    entry.name = "stage-store";
    entry.stage = vk::ShaderStageFlagBits::eVertex;
    entry.descriptorBindings.push_back({.descriptorType = vk::DescriptorType::eStorageBuffer,
        .storageBufferAccess = GVM::RHI::StorageBufferAccess::ReadWrite});
    reflection.entryPoints.push_back(entry);
    vk::PhysicalDeviceFeatures enabled;
    vk::PhysicalDeviceSubgroupProperties subgroups;
    enabled.vertexPipelineStoresAndAtomics = VK_TRUE;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "vertex"));
    reflection.entryPoints[0].stage = vk::ShaderStageFlagBits::eFragment;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "fragment"), std::invalid_argument);
    enabled.fragmentStoresAndAtomics = VK_TRUE;
    enabled.vertexPipelineStoresAndAtomics = VK_FALSE;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "fragment"));
    reflection.entryPoints[0].stage = vk::ShaderStageFlagBits::eVertex;
    EXPECT_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "vertex"), std::invalid_argument);
    reflection.entryPoints[0].descriptorBindings[0].storageBufferAccess = GVM::RHI::StorageBufferAccess::ReadOnly;
    EXPECT_NO_THROW(validateImageAndSubgroupRequirements(reflection, enabled, subgroups, false, "readonly"));
}
