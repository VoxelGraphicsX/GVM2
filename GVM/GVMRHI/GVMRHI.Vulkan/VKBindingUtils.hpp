#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKTaskDependencyResolver.hpp"

#include <EASTL/array.h>
#include <EASTL/span.h>
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

    /** Capacity of the fixed per-pass descriptor binding table; no real device exposes more bound descriptor sets. */
    inline constexpr uint32_t VKMaxBindGroupCount = 32;

    /** Descriptor binding state per bind group slot; bit g of boundMask is set when slot g holds a binding. */
    struct DescriptorBindingTable
    {
        eastl::array<DescriptorBindingState, VKMaxBindGroupCount> slots;
        uint32_t boundMask = 0u;
    };

    /** Returns the binding recorded at slot `groupIndex`, or nullptr when the slot is empty. */
    inline const DescriptorBindingState *getDescriptorBinding(const DescriptorBindingTable &table, uint32_t groupIndex)
    {
        if (groupIndex >= VKMaxBindGroupCount || (table.boundMask & (1u << groupIndex)) == 0u)
        {
            return nullptr;
        }
        return &table.slots[groupIndex];
    }

    /** Records a binding at slot `groupIndex`; the caller guarantees the index is below VKMaxBindGroupCount. */
    inline void setDescriptorBinding(DescriptorBindingTable &table, uint32_t groupIndex, const DescriptorBindingState &state)
    {
        table.slots[groupIndex] = state;
        table.boundMask |= 1u << groupIndex;
    }

    /** Removes the binding at slot `groupIndex` when one is recorded there. */
    inline void clearDescriptorBinding(DescriptorBindingTable &table, uint32_t groupIndex)
    {
        if (groupIndex < VKMaxBindGroupCount)
        {
            table.boundMask &= ~(1u << groupIndex);
        }
    }

    /** Returns how many slots of the table currently hold a binding. */
    inline uint32_t countDescriptorBindings(const DescriptorBindingTable &table)
    {
        uint32_t count = 0u;
        for (uint32_t mask = table.boundMask; mask != 0u; mask &= mask - 1u)
        {
            ++count;
        }
        return count;
    }

    using PreparedBindGroupBindings = eastl::unordered_map<const VKBindGroup *, PreparedBindGroupBinding>;

    /** Prepares one execution batch, combining descriptor buffers with caller-supplied Args and input uses. */
    PreparedBindGroupBindings prepareBindGroupBindings(
        vk::CommandBuffer commandBuffer,
        VKTaskDependencyResolver &stateTracker,
        const eastl::vector<const VKBindGroup *> &bindGroups,
        vk::PipelineStageFlags2 allowedStages,
        BindGroupPrepareCache *prepareCache = nullptr,
        eastl::span<const VKTaskDependencyResolver::TextureSubresourceSyncRequest> argumentTextures = {},
        eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests = {});

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

    /** Checks compatibility, then binds a descriptor state only when its native binding changes. */
    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        DescriptorBindingTable &nativeBoundDescriptorBindings);

    /** Binds a state whose compatibility with the current pipeline layout has already been established. */
    void bindCompatibleDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        DescriptorBindingTable &nativeBoundDescriptorBindings);

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
