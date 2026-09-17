#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    namespace Detail
    {
        enum class BindGroupEntryKind : uint8_t
        {
            Undefined,
            Buffer,
            Sampler,
            TextureView,
        };

        [[nodiscard]]
        BindGroupEntryKind getBindGroupLayoutEntryKind(const BindGroupLayoutEntry &entry);

        [[nodiscard]]
        BindGroupEntryKind getBindGroupEntryKind(const BindGroupEntry &entry);

        [[nodiscard]]
        size_t getBindGroupEntryResourceCount(const BindGroupEntry &entry, BindGroupEntryKind kind);

        [[nodiscard]]
        eastl::vector<size_t> resolveBindGroupEntryIndices(
            const BindGroupLayoutDescriptor &layoutDescriptor,
            const MultipleElements<BindGroupEntry> &descriptorEntries);
    } // namespace Detail

    struct BindGroupPrepareCache
    {
        size_t bufferCount = 0u;
        size_t textureCount = 0u;
    };

    struct DescriptorBindingState
    {
        BindGroup group = nullptr;
        BindGroupLayout layout = nullptr;
        vk::DescriptorSet descriptorSet = nullptr;
        bool descriptorSetOverride = false;
    };

    struct PreparedBindGroupBinding
    {
        bool prepared = false;
    };

    using PreparedBindGroupBindings = eastl::unordered_map<const VKBindGroup *, PreparedBindGroupBinding>;

    PreparedBindGroupBindings prepareBindGroupBindings(
        vk::CommandBuffer commandBuffer,
        VKTaskDependencyResolver &stateTracker,
        const eastl::vector<const VKBindGroup *> &bindGroups,
        vk::PipelineStageFlags allowedStages,
        const eastl::unordered_map<const VKTexture *, Texture> *explicitSampledTextures = nullptr,
        const eastl::vector<Texture> &forbiddenTextures = eastl::vector<Texture>{},
        BindGroupPrepareCache *prepareCache = nullptr);

    bool isBindGroupCompatibleWithPipelineLayout(
        const VKBindGroup *bindGroup,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex);

    bool isBindGroupCompatibleWithPipelineLayout(
        const VKBindGroup &bindGroup,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex);

    bool areDescriptorBindingStatesEquivalent(
        const DescriptorBindingState &lhs,
        const DescriptorBindingState &rhs);

    bool isDescriptorBindingStateCompatibleWithPipelineLayout(
        const DescriptorBindingState &bindingState,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex);

    bool isDescriptorBindingStateCompatibleWithPipelineLayout(
        const DescriptorBindingState &bindingState,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex);

    bool areRequiredDescriptorBindingsSatisfied(
        const VKPipelineLayout *pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings);

    bool areRequiredDescriptorBindingsSatisfied(
        const VKPipelineLayout &pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings);

    eastl::string buildMissingDescriptorBindingMessage(
        const char *apiName,
        const eastl::string &pipelineLabel,
        const VKPipelineLayout *pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings);

    eastl::string buildMissingDescriptorBindingMessage(
        const char *apiName,
        const eastl::string &pipelineLabel,
        const VKPipelineLayout &pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings);

    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        eastl::unordered_map<uint32_t, DescriptorBindingState> &nativeBoundDescriptorBindings);

    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        eastl::unordered_map<uint32_t, DescriptorBindingState> &nativeBoundDescriptorBindings);

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        const VKBindGroup *bindGroup,
        uint32_t groupIndex);

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        const VKBindGroup &bindGroup,
        uint32_t groupIndex);

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        vk::DescriptorSet descriptorSet,
        uint32_t groupIndex);
} // namespace GVM::RHI::Vulkan
