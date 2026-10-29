#include "VKBindingUtils.hpp"

#include "VKBindGroup.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKBuffer.hpp"
#include "VKEnumUtils.hpp"
#include "VKPipelineLayout.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include <GVMRHI/Private/TextureViewResolution.hpp>

#include <EASTL/unordered_set.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** Compares already resolved view ranges after their parent texture and layout have matched. */
        bool haveSameTextureSubresources(const VKTextureView &left, const VKTextureView &right)
        {
            const auto &lhs = left.getDescriptor();
            const auto &rhs = right.getDescriptor();
            return lhs.aspect == rhs.aspect && lhs.baseMipLevel == rhs.baseMipLevel &&
                lhs.mipLevelCount == rhs.mipLevelCount && lhs.baseArrayLayer == rhs.baseArrayLayer &&
                lhs.arrayLayerCount == rhs.arrayLayerCount;
        }

        [[nodiscard]]
        vk::AccessFlags2 resolveBufferBindingAccess(const BufferBindingLayout &bufferLayout)
        {
            switch (bufferLayout.type)
            {
            case BufferBindingType::Uniform:
                return vk::AccessFlagBits2::eUniformRead;
            case BufferBindingType::ReadOnlyStorage:
                return vk::AccessFlagBits2::eShaderRead;
            case BufferBindingType::Storage:
                switch (bufferLayout.access)
                {
                case StorageBufferAccess::ReadOnly: return vk::AccessFlagBits2::eShaderRead;
                case StorageBufferAccess::WriteOnly: return vk::AccessFlagBits2::eShaderWrite;
                case StorageBufferAccess::ReadWrite: return vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
                default: return vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
                }
            default:
                throw std::invalid_argument("prepareBindGroupBindings encountered an unsupported buffer binding layout.");
            }
        }

        [[nodiscard]]
        vk::AccessFlags2 resolveTextureBindingAccess(const BindGroupLayoutEntry &layoutEntry)
        {
            if (layoutEntry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                switch (layoutEntry.storageTexture.access)
                {
                case StorageTextureAccess::ReadOnly: return vk::AccessFlagBits2::eShaderRead;
                case StorageTextureAccess::WriteOnly: return vk::AccessFlagBits2::eShaderWrite;
                case StorageTextureAccess::ReadWrite: return vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
                default: return vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
                }
            }
            return vk::AccessFlagBits2::eShaderRead;
        }

        [[nodiscard]]
        vk::ImageLayout resolveTextureBindingLayout(const BindGroupLayoutEntry &layoutEntry)
        {
            return layoutEntry.storageTexture.access != StorageTextureAccess::Undefined
                ? vk::ImageLayout::eGeneral
                : vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        /** Returns a diagnostic label for a bind group involved in descriptor preparation. */
        eastl::string describeBindGroupLabel(const VKBindGroup *bindGroup)
        {
            if (bindGroup == nullptr)
            {
                return "<null>";
            }
            return bindGroup->getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(bindGroup->getLabelName().c_str());
        }

        /** Rejects retired descriptor buffers; range bounds are resolved once by the synchronizer. */
        void validatePreparedBufferBinding(
            const VKBindGroup *bindGroup,
            const VKBindGroup::ResolvedBinding &binding,
            const BufferRange &bufferRange,
            const VKBuffer *buffer)
        {
            if (buffer == nullptr)
            {
                throw std::logic_error((
                    "prepareBindGroupBindings encountered a null Vulkan buffer in bind group '" + describeBindGroupLabel(bindGroup) +
                    "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".").c_str());
            }
            if (buffer->isDestroyed() || buffer->getNativeBuffer() == vk::Buffer{})
            {
                throw std::logic_error((
                    "prepareBindGroupBindings encountered destroyed Vulkan buffer '" + buffer->getLabelName() +
                    "' in bind group '" + describeBindGroupLabel(bindGroup) + "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".").c_str());
            }
        }

        bool areBindGroupLayoutsCompatible(const BindGroupLayout &lhs, const BindGroupLayout &rhs)
        {
            return lhs.get() == rhs.get();
        }

        [[nodiscard]]
        bool isTransientDescriptorSetCompatible(
            const BindGroupLayout &layoutHandle,
            const VKPipelineLayout *pipelineLayout,
            uint32_t groupIndex)
        {
            if (pipelineLayout == nullptr || layoutHandle == nullptr)
            {
                return false;
            }
            if (groupIndex >= pipelineLayout->getBindGroupLayoutCount())
            {
                return false;
            }
            return pipelineLayout->getBindGroupLayoutHandle(groupIndex).get() == layoutHandle.get();
        }

        [[nodiscard]]
        bool isTransientDescriptorSetCompatible(
            const BindGroupLayout &layoutHandle,
            const VKPipelineLayout &pipelineLayout,
            uint32_t groupIndex)
        {
            if (layoutHandle == nullptr)
            {
                return false;
            }
            if (groupIndex >= pipelineLayout.getBindGroupLayoutCount())
            {
                return false;
            }
            return pipelineLayout.getBindGroupLayoutHandle(groupIndex).get() == layoutHandle.get();
        }
    }

    namespace Detail
    {
        BindGroupEntryKind getBindGroupLayoutEntryKind(const BindGroupLayoutEntry &entry)
        {
            if (entry.sampler.type != SamplerBindingType::Undefined)
            {
                return BindGroupEntryKind::Sampler;
            }
            if (entry.texture.sampleType != TextureSampleType::Undefined || entry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                return BindGroupEntryKind::TextureView;
            }
            if (entry.buffer.type != BufferBindingType::Undefined)
            {
                return BindGroupEntryKind::Buffer;
            }
            return BindGroupEntryKind::Undefined;
        }

        BindGroupEntryKind getBindGroupEntryKind(const BindGroupEntry &entry)
        {
            const bool hasBuffers = !entry.buffer.empty();
            const bool hasSamplers = !entry.sampler.empty();
            const bool hasTextureViews = !entry.textureView.empty();
            const uint32_t populatedKinds =
                static_cast<uint32_t>(hasBuffers) +
                static_cast<uint32_t>(hasSamplers) +
                static_cast<uint32_t>(hasTextureViews);
            if (populatedKinds != 1u)
            {
                return BindGroupEntryKind::Undefined;
            }
            if (hasBuffers)
            {
                return BindGroupEntryKind::Buffer;
            }
            if (hasSamplers)
            {
                return BindGroupEntryKind::Sampler;
            }
            return BindGroupEntryKind::TextureView;
        }

        size_t getBindGroupEntryResourceCount(const BindGroupEntry &entry, BindGroupEntryKind kind)
        {
            switch (kind)
            {
            case BindGroupEntryKind::Buffer:
                return entry.buffer.size();
            case BindGroupEntryKind::Sampler:
                return entry.sampler.size();
            case BindGroupEntryKind::TextureView:
                return entry.textureView.size();
            default:
                return 0u;
            }
        }

    } // namespace Detail

    PreparedBindGroupBindings prepareBindGroupBindings(
        vk::CommandBuffer commandBuffer,
        VKTaskDependencyResolver &stateTracker,
        const eastl::vector<const VKBindGroup *> &bindGroups,
        vk::PipelineStageFlags2 allowedStages,
        BindGroupPrepareCache *prepareCache,
        eastl::span<const VKTaskDependencyResolver::TextureSubresourceSyncRequest> argumentTextures,
        eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests)
    {
        eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest> mergedBufferSyncRequests(
            bufferSyncRequests.begin(), bufferSyncRequests.end());
        PreparedBindGroupBindings preparedBindings;
        preparedBindings.reserve(bindGroups.size());
        eastl::unordered_multimap<VKTexture *, size_t> texturePrepareIndices;
        eastl::vector<VKTaskDependencyResolver::TextureSubresourceSyncRequest> textureSyncRequests;
        vk::PipelineStageFlags2 allowedBindingStages = allowedStages;
        // eAllGraphics is an aggregate synchronization stage, not a bitwise alias
        // for the individual programmable graphics stages used by descriptor visibility.
        if ((allowedStages & vk::PipelineStageFlagBits2::eAllGraphics) !=
            vk::PipelineStageFlags2{})
        {
            allowedBindingStages |=
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eFragmentShader;
        }

        for (const VKBindGroup *bindGroup : bindGroups)
        {
            if (bindGroup == nullptr)
            {
                continue;
            }

            if (preparedBindings.find(bindGroup) != preparedBindings.end())
            {
                continue;
            }

            for (const VKBindGroup::ResolvedBinding &binding : bindGroup->getResolvedBindings())
            {
                const vk::PipelineStageFlags2 stageMask =
                    translateShaderStagesToPipelineStages(
                        binding.layoutEntry.visibility) &
                    allowedBindingStages;
                if (stageMask == vk::PipelineStageFlags2{})
                {
                    continue;
                }

                if (!binding.descriptorEntry.buffer.empty())
                {
                    const vk::AccessFlags2 accessMask = resolveBufferBindingAccess(binding.layoutEntry.buffer);
                    for (const BufferRange &bufferRange : binding.descriptorEntry.buffer)
                    {
                        auto *buffer = static_cast<VKBuffer *>(bufferRange.buffer.get());
                        validatePreparedBufferBinding(bindGroup, binding, bufferRange, buffer);
                        mergedBufferSyncRequests.push_back({buffer, bufferRange.offset, bufferRange.size, stageMask, accessMask});
                    }
                    continue;
                }

                if (binding.descriptorEntry.textureView.empty())
                {
                    continue;
                }

                for (const TextureView &viewHandle : binding.descriptorEntry.textureView)
                {
                    auto *view = static_cast<VKTextureView *>(viewHandle.get());
                    auto *texture = view != nullptr ? view->getTexture() : nullptr;
                    if (view == nullptr || texture == nullptr)
                    {
                        continue;
                    }

                    const vk::AccessFlags2 accessMask = resolveTextureBindingAccess(binding.layoutEntry);
                    const vk::ImageLayout imageLayout = resolveTextureBindingLayout(binding.layoutEntry);
                    const auto textureRequests = texturePrepareIndices.equal_range(texture);

                    size_t textureIndex = textureSyncRequests.size();
                    for (auto it = textureRequests.first; it != textureRequests.second; ++it)
                    {
                        if (haveSameTextureSubresources(*textureSyncRequests[it->second].view, *view))
                        {
                            textureIndex = it->second;
                            break;
                        }
                    }
                    if (textureIndex == textureSyncRequests.size())
                    {
                        texturePrepareIndices.emplace(texture, textureIndex);
                        textureSyncRequests.push_back({view, imageLayout, stageMask, accessMask});
                    }
                    else
                    {
                        textureSyncRequests[textureIndex].stages |= stageMask;
                        textureSyncRequests[textureIndex].accesses |= accessMask;
                    }
                }
            }

            preparedBindings.emplace(
                bindGroup,
                PreparedBindGroupBinding{
                    .prepared = true,
                });
        }

        for (const auto &use : argumentTextures)
        {
            auto *texture = use.view->getTexture();
            const auto requests = texturePrepareIndices.equal_range(texture);
            const auto stages = use.stages & allowedBindingStages;
            if (!stages)
            {
                continue;
            }
            size_t index = textureSyncRequests.size();
            for (auto found = requests.first; found != requests.second; ++found)
            {
                if (haveSameTextureSubresources(*textureSyncRequests[found->second].view, *use.view))
                {
                    index = found->second;
                    break;
                }
            }
            if (index == textureSyncRequests.size())
            {
                texturePrepareIndices.emplace(texture, index);
                textureSyncRequests.push_back({use.view, use.layout, stages, use.accesses});
            }
            else
            {
                textureSyncRequests[index].stages |= stages;
                textureSyncRequests[index].accesses |= use.accesses;
            }
        }

        if (prepareCache != nullptr)
        {
            prepareCache->bufferCount = mergedBufferSyncRequests.size();
            prepareCache->textureCount = textureSyncRequests.size();
        }
        stateTracker.synchronizeBufferRanges(commandBuffer, eastl::move(mergedBufferSyncRequests));
        stateTracker.synchronizeTextureViews(commandBuffer, eastl::move(textureSyncRequests));

        return preparedBindings;
    }

    bool isBindGroupCompatibleWithPipelineLayout(
        const VKBindGroup *bindGroup,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex)
    {
        if (bindGroup == nullptr || pipelineLayout == nullptr)
        {
            return false;
        }
        return isBindGroupCompatibleWithPipelineLayout(*bindGroup, *pipelineLayout, groupIndex);
    }

    bool isBindGroupCompatibleWithPipelineLayout(
        const VKBindGroup &bindGroup,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex)
    {
        if (groupIndex >= pipelineLayout.getBindGroupLayoutCount())
        {
            return false;
        }

        return areBindGroupLayoutsCompatible(
            bindGroup.getLayoutHandle(),
            pipelineLayout.getBindGroupLayoutHandle(groupIndex));
    }

    bool areDescriptorBindingStatesEquivalent(
        const DescriptorBindingState &lhs,
        const DescriptorBindingState &rhs)
    {
        return lhs.group == rhs.group &&
               lhs.layout == rhs.layout &&
               lhs.descriptorSet == rhs.descriptorSet &&
               lhs.descriptorSetOverride == rhs.descriptorSetOverride;
    }

    bool isDescriptorBindingStateCompatibleWithPipelineLayout(
        const DescriptorBindingState &bindingState,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex)
    {
        if (pipelineLayout == nullptr)
        {
            return false;
        }
        return isDescriptorBindingStateCompatibleWithPipelineLayout(bindingState, *pipelineLayout, groupIndex);
    }

    bool isDescriptorBindingStateCompatibleWithPipelineLayout(
        const DescriptorBindingState &bindingState,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex)
    {
        if (bindingState.descriptorSetOverride)
        {
            return isTransientDescriptorSetCompatible(bindingState.layout, pipelineLayout, groupIndex);
        }
        if (bindingState.group == nullptr)
        {
            return false;
        }

        return isBindGroupCompatibleWithPipelineLayout(
            static_cast<const VKBindGroup &>(*bindingState.group),
            pipelineLayout,
            groupIndex);
    }

    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        DescriptorBindingTable &nativeBoundDescriptorBindings)
    {
        if (!isDescriptorBindingStateCompatibleWithPipelineLayout(desiredState, pipelineLayout, groupIndex))
        {
            clearDescriptorBinding(nativeBoundDescriptorBindings, groupIndex);
            return;
        }
        bindCompatibleDescriptorBindingStateIfNeeded(commandBuffer, bindPoint, nativePipelineLayout,
            groupIndex, desiredState, nativeBoundDescriptorBindings);
    }

    void bindCompatibleDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        DescriptorBindingTable &nativeBoundDescriptorBindings)
    {
        const DescriptorBindingState *bound = getDescriptorBinding(nativeBoundDescriptorBindings, groupIndex);
        if (bound != nullptr && areDescriptorBindingStatesEquivalent(*bound, desiredState))
        {
            return;
        }
        const vk::DescriptorSet descriptorSet = desiredState.descriptorSetOverride ? desiredState.descriptorSet : static_cast<const VKBindGroup &>(*desiredState.group).getNativeDescriptorSet();
        bindDescriptorSet(commandBuffer, bindPoint, nativePipelineLayout, descriptorSet, groupIndex);
        setDescriptorBinding(nativeBoundDescriptorBindings, groupIndex, desiredState);
    }

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        const VKBindGroup *bindGroup,
        uint32_t groupIndex)
    {
        const vk::DescriptorSet descriptorSet = bindGroup->getNativeDescriptorSet();
        commandBuffer.bindDescriptorSets(bindPoint, pipelineLayout, groupIndex, descriptorSet, {});
    }

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        const VKBindGroup &bindGroup,
        uint32_t groupIndex)
    {
        const vk::DescriptorSet descriptorSet = bindGroup.getNativeDescriptorSet();
        commandBuffer.bindDescriptorSets(bindPoint, pipelineLayout, groupIndex, descriptorSet, {});
    }

    void bindDescriptorSet(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout pipelineLayout,
        vk::DescriptorSet descriptorSet,
        uint32_t groupIndex)
    {
        commandBuffer.bindDescriptorSets(bindPoint, pipelineLayout, groupIndex, descriptorSet, {});
    }
} // namespace GVM::RHI::Vulkan
