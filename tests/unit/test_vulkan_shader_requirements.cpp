#include <gtest/gtest.h>
#include "VKShaderReflection.hpp"
#include <stdexcept>

using namespace GVM::RHI::Vulkan::Detail;

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
