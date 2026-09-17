#include "VKBindingUtils.hpp"

#include "VKBindGroup.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKBuffer.hpp"
#include "VKEnumUtils.hpp"
#include "VKPipelineLayout.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

namespace GVM::RHI::Vulkan
{
    namespace
    {
        struct PreparedUsageState
        {
            vk::PipelineStageFlags stageMask = {};
            vk::AccessFlags accessMask = {};
        };

        struct BufferPrepareKey
        {
            VKBuffer *buffer = nullptr;
            uint64_t offset = 0u;
            uint64_t size = 0u;

            bool operator==(const BufferPrepareKey &other) const
            {
                return buffer == other.buffer &&
                       offset == other.offset &&
                       size == other.size;
            }
        };

        struct BufferPrepareKeyHash
        {
            size_t operator()(const BufferPrepareKey &key) const
            {
                size_t hash = eastl::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(key.buffer));
                hash ^= eastl::hash<uint64_t>{}(key.offset) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint64_t>{}(key.size) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                return hash;
            }
        };

        struct TexturePrepareKey
        {
            VKTexture *texture = nullptr;
            vk::ImageLayout imageLayout = vk::ImageLayout::eUndefined;
            TextureAspectFlags aspectMask = TextureAspect::All;
            uint32_t baseMipLevel = 0u;
            uint32_t mipLevelCount = 0u;
            uint32_t baseArrayLayer = 0u;
            uint32_t arrayLayerCount = 0u;

            bool operator==(const TexturePrepareKey &other) const
            {
                return texture == other.texture &&
                       imageLayout == other.imageLayout &&
                       aspectMask == other.aspectMask &&
                       baseMipLevel == other.baseMipLevel &&
                       mipLevelCount == other.mipLevelCount &&
                       baseArrayLayer == other.baseArrayLayer &&
                       arrayLayerCount == other.arrayLayerCount;
            }
        };

        struct TexturePrepareKeyHash
        {
            size_t operator()(const TexturePrepareKey &key) const
            {
                size_t hash = eastl::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(key.texture));
                hash ^= eastl::hash<uint32_t>{}(static_cast<uint32_t>(key.imageLayout)) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint32_t>{}(static_cast<uint32_t>(key.aspectMask)) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint32_t>{}(key.baseMipLevel) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint32_t>{}(key.mipLevelCount) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint32_t>{}(key.baseArrayLayer) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                hash ^= eastl::hash<uint32_t>{}(key.arrayLayerCount) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
                return hash;
            }
        };

        [[nodiscard]]
        vk::AccessFlags resolveBufferBindingAccess(const BufferBindingLayout &bufferLayout)
        {
            switch (bufferLayout.type)
            {
            case BufferBindingType::Uniform:
                return vk::AccessFlagBits::eUniformRead;
            case BufferBindingType::ReadOnlyStorage:
                return vk::AccessFlagBits::eShaderRead;
            case BufferBindingType::Storage:
                switch (bufferLayout.access)
                {
                case StorageBufferAccess::ReadOnly: return vk::AccessFlagBits::eShaderRead;
                case StorageBufferAccess::WriteOnly: return vk::AccessFlagBits::eShaderWrite;
                case StorageBufferAccess::ReadWrite: return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
                default: return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
                }
            default:
                throw makeInvalidArgument("prepareBindGroupBindings encountered an unsupported buffer binding layout.");
            }
        }

        [[nodiscard]]
        vk::AccessFlags resolveTextureBindingAccess(const BindGroupLayoutEntry &layoutEntry)
        {
            if (layoutEntry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                switch (layoutEntry.storageTexture.access)
                {
                case StorageTextureAccess::ReadOnly: return vk::AccessFlagBits::eShaderRead;
                case StorageTextureAccess::WriteOnly: return vk::AccessFlagBits::eShaderWrite;
                case StorageTextureAccess::ReadWrite: return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
                default: return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
                }
            }
            return vk::AccessFlagBits::eShaderRead;
        }

        [[nodiscard]]
        vk::ImageLayout resolveTextureBindingLayout(const BindGroupLayoutEntry &layoutEntry)
        {
            return layoutEntry.storageTexture.access != StorageTextureAccess::Undefined
                ? vk::ImageLayout::eGeneral
                : vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        [[nodiscard]]
        bool isStorageTextureBinding(const BindGroupLayoutEntry &layoutEntry)
        {
            return layoutEntry.storageTexture.access != StorageTextureAccess::Undefined;
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

        /** Returns a diagnostic label for a texture involved in descriptor preparation. */
        eastl::string describeTextureLabel(const VKTexture *texture)
        {
            if (texture == nullptr)
            {
                return "<null>";
            }
            return texture->getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(texture->getLabelName().c_str());
        }

        /** Formats where a texture role came from so conflict diagnostics identify both bindings. */
        eastl::string buildTextureRoleDetail(const VKBindGroup *bindGroup, uint32_t binding)
        {
            return "bind group '" + describeBindGroupLabel(bindGroup) + "' binding " + eastl::to_string(binding);
        }

        /** Tracks one texture role and rejects unsorted sampled/storage use in one prepared binding set. */
        void recordTextureRole(
            const VKTexture *texture,
            bool storageRole,
            const eastl::string &roleDetail,
            eastl::unordered_map<const VKTexture *, eastl::string> &sampledRoles,
            eastl::unordered_map<const VKTexture *, eastl::string> &storageRoles)
        {
            if (texture == nullptr)
            {
                return;
            }

            auto &sameRoleMap = storageRole ? storageRoles : sampledRoles;
            const auto sameRoleIt = sameRoleMap.find(texture);
            if (sameRoleIt == sameRoleMap.end())
            {
                sameRoleMap.emplace(texture, roleDetail);
            }

            const auto &oppositeRoleMap = storageRole ? sampledRoles : storageRoles;
            const auto oppositeRoleIt = oppositeRoleMap.find(texture);
            if (oppositeRoleIt == oppositeRoleMap.end())
            {
                return;
            }

            const eastl::string &sampledDetail = storageRole ? oppositeRoleIt->second : roleDetail;
            const eastl::string &storageDetail = storageRole ? roleDetail : oppositeRoleIt->second;
            throw makeLogicError(
                "Vulkan descriptor preparation found texture '" + describeTextureLabel(texture) +
                "' bound as both sampled and storage resources in one unsorted use. Sampled role: " +
                sampledDetail + "; storage role: " + storageDetail +
                ". Split these accesses into ordered compute dispatches or separate render passes.");
        }

        void validatePreparedBufferBinding(
            const VKBindGroup *bindGroup,
            const VKBindGroup::ResolvedBinding &binding,
            const BufferRange &bufferRange,
            const VKBuffer *buffer)
        {
            const eastl::string bindGroupLabel =
                bindGroup != nullptr && !bindGroup->getLabelName().empty() ? eastl::string(bindGroup->getLabelName().c_str()) : eastl::string("<null>");
            const eastl::string bufferLabel =
                buffer != nullptr && !buffer->getLabelName().empty() ? eastl::string(buffer->getLabelName().c_str()) : eastl::string("<unnamed>");

            if (bufferRange.buffer.isNull() || buffer == nullptr)
            {
                throw makeLogicError(
                    "prepareBindGroupBindings encountered a null Vulkan buffer in bind group '" + bindGroupLabel +
                    "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".");
            }
            if (buffer->isDestroyed() || buffer->getNativeBuffer() == vk::Buffer{})
            {
                throw makeLogicError(
                    "prepareBindGroupBindings encountered destroyed Vulkan buffer '" + bufferLabel +
                    "' in bind group '" + bindGroupLabel + "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".");
            }

            const uint64_t storageSize = buffer->getStorageSize();
            if (bufferRange.offset > storageSize)
            {
                throw makeLogicError(
                    "prepareBindGroupBindings encountered out-of-range offset " + eastl::to_string(bufferRange.offset) +
                    " for Vulkan buffer '" + bufferLabel + "' (size " + eastl::to_string(storageSize) +
                    ") in bind group '" + bindGroupLabel + "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".");
            }

            const uint64_t resolvedSize = bufferRange.size == WholeSize ? (storageSize - bufferRange.offset) : bufferRange.size;
            if (resolvedSize == 0u)
            {
                throw makeLogicError(
                    "prepareBindGroupBindings encountered an empty buffer range for Vulkan buffer '" + bufferLabel +
                    "' in bind group '" + bindGroupLabel + "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".");
            }
            if (resolvedSize > (storageSize - bufferRange.offset))
            {
                throw makeLogicError(
                    "prepareBindGroupBindings encountered range [" + eastl::to_string(bufferRange.offset) + ", " +
                    eastl::to_string(bufferRange.offset + resolvedSize) + ") outside Vulkan buffer '" + bufferLabel +
                    "' (size " + eastl::to_string(storageSize) + ") in bind group '" + bindGroupLabel +
                    "' at binding " + eastl::to_string(binding.layoutEntry.binding) + ".");
            }
        }

        bool areBufferBindingLayoutsEquivalent(const BufferBindingLayout &lhs, const BufferBindingLayout &rhs)
        {
            return lhs.type == rhs.type && lhs.access == rhs.access;
        }

        bool areSamplerBindingLayoutsEquivalent(const SamplerBindingLayout &lhs, const SamplerBindingLayout &rhs)
        {
            return lhs.type == rhs.type;
        }

        bool areTextureBindingLayoutsEquivalent(const TextureBindingLayout &lhs, const TextureBindingLayout &rhs)
        {
            return lhs.sampleType == rhs.sampleType && lhs.viewDimension == rhs.viewDimension;
        }

        bool areStorageTextureBindingLayoutsEquivalent(const StorageTextureBindingLayout &lhs, const StorageTextureBindingLayout &rhs)
        {
            return lhs.access == rhs.access &&
                   lhs.format == rhs.format &&
                   lhs.viewDimension == rhs.viewDimension;
        }

        bool areBindGroupLayoutEntriesEquivalent(const BindGroupLayoutEntry &lhs, const BindGroupLayoutEntry &rhs)
        {
            return lhs.binding == rhs.binding &&
                   lhs.maxCount == rhs.maxCount &&
                   lhs.visibility == rhs.visibility &&
                   areBufferBindingLayoutsEquivalent(lhs.buffer, rhs.buffer) &&
                   areSamplerBindingLayoutsEquivalent(lhs.sampler, rhs.sampler) &&
                   areTextureBindingLayoutsEquivalent(lhs.texture, rhs.texture) &&
                   areStorageTextureBindingLayoutsEquivalent(lhs.storageTexture, rhs.storageTexture);
        }

        bool areBindGroupLayoutDescriptorsEquivalent(const BindGroupLayoutDescriptor &lhs, const BindGroupLayoutDescriptor &rhs)
        {
            if (lhs.entries.size() != rhs.entries.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.entries.size(); ++index)
            {
                if (!areBindGroupLayoutEntriesEquivalent(lhs.entries[index], rhs.entries[index]))
                {
                    return false;
                }
            }

            return true;
        }

        bool areBindGroupLayoutsCompatible(const BindGroupLayout &lhs, const BindGroupLayout &rhs)
        {
            if (lhs.get() == rhs.get())
            {
                return true;
            }
            if (lhs == nullptr || rhs == nullptr)
            {
                return false;
            }

            auto *lhsLayout = static_cast<const VKBindGroupLayout *>(lhs.get());
            auto *rhsLayout = static_cast<const VKBindGroupLayout *>(rhs.get());
            return areBindGroupLayoutDescriptorsEquivalent(lhsLayout->getDescriptor(), rhsLayout->getDescriptor());
        }

        [[nodiscard]]
        bool isTransientDescriptorSetCompatible(
            BindGroupLayout layoutHandle,
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
            BindGroupLayout layoutHandle,
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

        eastl::vector<size_t> resolveBindGroupEntryIndices(
            const BindGroupLayoutDescriptor &layoutDescriptor,
            const MultipleElements<BindGroupEntry> &descriptorEntries)
        {
            eastl::unordered_map<uint32_t, size_t> layoutBindingToIndex;
            layoutBindingToIndex.reserve(layoutDescriptor.entries.size());
            for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
            {
                const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
                if (!layoutBindingToIndex.emplace(layoutEntry.binding, layoutIndex).second)
                {
                    throw makeInvalidArgument("VKBindGroup layout contains duplicate binding numbers.");
                }
                if (getBindGroupLayoutEntryKind(layoutEntry) == BindGroupEntryKind::Undefined)
                {
                    throw makeInvalidArgument("VKBindGroup layout contains an entry with an undefined binding type.");
                }
            }

            eastl::unordered_map<uint32_t, size_t> descriptorBindingToIndex;
            descriptorBindingToIndex.reserve(descriptorEntries.size());
            for (size_t descriptorIndex = 0; descriptorIndex < descriptorEntries.size(); ++descriptorIndex)
            {
                const auto &entry = descriptorEntries[descriptorIndex];
                if (!descriptorBindingToIndex.emplace(entry.binding, descriptorIndex).second)
                {
                    throw makeInvalidArgument("VKBindGroup descriptor contains duplicate binding numbers.");
                }

                const BindGroupEntryKind entryKind = getBindGroupEntryKind(entry);
                if (entryKind == BindGroupEntryKind::Undefined)
                {
                    throw makeInvalidArgument("VKBindGroup descriptor entries must populate exactly one resource array.");
                }

                const auto layoutIt = layoutBindingToIndex.find(entry.binding);
                if (layoutIt == layoutBindingToIndex.end())
                {
                    throw makeInvalidArgument("VKBindGroup descriptor contains a binding that is not declared by the layout.");
                }

                const auto &layoutEntry = layoutDescriptor.entries[layoutIt->second];
                const BindGroupEntryKind layoutKind = getBindGroupLayoutEntryKind(layoutEntry);
                if (entryKind != layoutKind)
                {
                    throw makeInvalidArgument("VKBindGroup descriptor binding type does not match the layout binding type.");
                }

                const size_t resourceCount = getBindGroupEntryResourceCount(entry, entryKind);
                if (resourceCount == 0u || resourceCount > layoutEntry.maxCount)
                {
                    throw makeInvalidArgument("VKBindGroup descriptor resource count is incompatible with layout.maxCount.");
                }
            }

            if (descriptorEntries.size() != layoutDescriptor.entries.size())
            {
                throw makeInvalidArgument("VKBindGroup descriptor entry count must match the layout entry count.");
            }

            eastl::vector<size_t> resolvedIndices(layoutDescriptor.entries.size(), size_t(-1));
            for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
            {
                const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
                const auto descriptorIt = descriptorBindingToIndex.find(layoutEntry.binding);
                if (descriptorIt == descriptorBindingToIndex.end())
                {
                    throw makeInvalidArgument("VKBindGroup descriptor is missing a binding declared by the layout.");
                }
                resolvedIndices[layoutIndex] = descriptorIt->second;
            }
            return resolvedIndices;
        }
    } // namespace Detail

    PreparedBindGroupBindings prepareBindGroupBindings(
        vk::CommandBuffer commandBuffer,
        VKTaskDependencyResolver &stateTracker,
        const eastl::vector<const VKBindGroup *> &bindGroups,
        vk::PipelineStageFlags allowedStages,
        const eastl::unordered_map<const VKTexture *, Texture> *explicitSampledTextures,
        const eastl::vector<Texture> &forbiddenTextures,
        BindGroupPrepareCache *prepareCache)
    {
        PreparedBindGroupBindings preparedBindings;
        preparedBindings.reserve(bindGroups.size());
        eastl::unordered_map<BufferPrepareKey, size_t, BufferPrepareKeyHash> bufferPrepareIndices;
        eastl::unordered_map<TexturePrepareKey, size_t, TexturePrepareKeyHash> texturePrepareIndices;
        eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests;
        eastl::vector<TexturePrepareKey> texturePrepareKeys;
        eastl::vector<PreparedUsageState> texturePrepareStates;
        eastl::unordered_map<const VKTexture *, eastl::string> sampledTextureRoles;
        eastl::unordered_map<const VKTexture *, eastl::string> storageTextureRoles;
        vk::PipelineStageFlags allowedBindingStages = allowedStages;
        // eAllGraphics is an aggregate synchronization stage, not a bitwise alias
        // for the individual programmable graphics stages used by descriptor visibility.
        if ((allowedStages & vk::PipelineStageFlagBits::eAllGraphics) !=
            vk::PipelineStageFlags{})
        {
            allowedBindingStages |=
                vk::PipelineStageFlagBits::eVertexShader |
                vk::PipelineStageFlagBits::eTessellationControlShader |
                vk::PipelineStageFlagBits::eTessellationEvaluationShader |
                vk::PipelineStageFlagBits::eGeometryShader |
                vk::PipelineStageFlagBits::eFragmentShader;
        }

        if (explicitSampledTextures != nullptr)
        {
            for (const auto &[texture, _] : *explicitSampledTextures)
            {
                recordTextureRole(
                    texture,
                    false,
                    "explicit sampled texture transition",
                    sampledTextureRoles,
                    storageTextureRoles);
            }
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
                const vk::PipelineStageFlags stageMask =
                    translateShaderStagesToPipelineStages(
                        binding.layoutEntry.visibility) &
                    allowedBindingStages;
                if (stageMask == vk::PipelineStageFlags{})
                {
                    continue;
                }

                if (!binding.descriptorEntry.buffer.empty())
                {
                    const vk::AccessFlags accessMask = resolveBufferBindingAccess(binding.layoutEntry.buffer);
                    for (const BufferRange &bufferRange : binding.descriptorEntry.buffer)
                    {
                        auto *buffer = static_cast<VKBuffer *>(bufferRange.buffer.get());
                        validatePreparedBufferBinding(bindGroup, binding, bufferRange, buffer);

                        const BufferPrepareKey key = {
                            .buffer = buffer,
                            .offset = bufferRange.offset,
                            .size = bufferRange.size,
                        };
                        const auto [it, inserted] = bufferPrepareIndices.emplace(key, bufferSyncRequests.size());
                        if (inserted)
                        {
                            bufferSyncRequests.push_back(VKTaskDependencyResolver::BufferRangeSyncRequest{
                                .buffer = buffer,
                                .offset = bufferRange.offset,
                                .size = bufferRange.size,
                                .requiredStageMask = stageMask,
                                .requiredAccessMask = accessMask,
                            });
                        }
                        else
                        {
                            auto &request = bufferSyncRequests[it->second];
                            request.requiredStageMask |= stageMask;
                            request.requiredAccessMask |= accessMask;
                        }
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

                    recordTextureRole(
                        texture,
                        isStorageTextureBinding(binding.layoutEntry),
                        buildTextureRoleDetail(bindGroup, binding.layoutEntry.binding),
                        sampledTextureRoles,
                        storageTextureRoles);
                    const vk::AccessFlags accessMask = resolveTextureBindingAccess(binding.layoutEntry);
                    const vk::ImageLayout imageLayout = resolveTextureBindingLayout(binding.layoutEntry);

                    if (!forbiddenTextures.empty())
                    {
                        for (const Texture &forbiddenTexture : forbiddenTextures)
                        {
                            if (forbiddenTexture.get() == texture &&
                                imageLayout != vk::ImageLayout::eColorAttachmentOptimal &&
                                imageLayout != vk::ImageLayout::eDepthStencilAttachmentOptimal)
                            {
                                throw makeLogicError("Vulkan phase 2 does not yet support sampling or storage access to an active render attachment within the same render pass.");
                            }
                        }
                    }

                    const TexturePrepareKey key = {
                        .texture = texture,
                        .imageLayout = imageLayout,
                        .aspectMask = view->getDescriptor().aspect,
                        .baseMipLevel = view->getDescriptor().baseMipLevel,
                        .mipLevelCount = view->getDescriptor().mipLevelCount,
                        .baseArrayLayer = view->getDescriptor().baseArrayLayer,
                        .arrayLayerCount = view->getDescriptor().arrayLayerCount,
                    };
                    const auto [it, inserted] = texturePrepareIndices.emplace(key, texturePrepareKeys.size());
                    if (inserted)
                    {
                        texturePrepareKeys.push_back(key);
                        texturePrepareStates.push_back(PreparedUsageState{
                            .stageMask = stageMask,
                            .accessMask = accessMask,
                        });
                    }
                    else
                    {
                        texturePrepareStates[it->second].stageMask |= stageMask;
                        texturePrepareStates[it->second].accessMask |= accessMask;
                    }
                }
            }

            preparedBindings.emplace(
                bindGroup,
                PreparedBindGroupBinding{
                    .prepared = true,
                });
        }

        stateTracker.synchronizeBufferRanges(commandBuffer, bufferSyncRequests);
        for (size_t textureIndex = 0u; textureIndex < texturePrepareKeys.size(); ++textureIndex)
        {
            const TexturePrepareKey &key = texturePrepareKeys[textureIndex];
            const PreparedUsageState &state = texturePrepareStates[textureIndex];
            stateTracker.synchronizeTextureSubresources(
                commandBuffer,
                *key.texture,
                key.imageLayout,
                state.stageMask,
                state.accessMask,
                key.aspectMask,
                key.baseMipLevel,
                key.mipLevelCount,
                key.baseArrayLayer,
                key.arrayLayerCount);
        }

        if (prepareCache != nullptr)
        {
            prepareCache->bufferCount = bufferSyncRequests.size();
            prepareCache->textureCount = texturePrepareKeys.size();
        }

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

    bool areRequiredDescriptorBindingsSatisfied(
        const VKPipelineLayout *pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings)
    {
        if (pipelineLayout == nullptr)
        {
            return false;
        }
        return areRequiredDescriptorBindingsSatisfied(*pipelineLayout, boundDescriptorBindings);
    }

    bool areRequiredDescriptorBindingsSatisfied(
        const VKPipelineLayout &pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings)
    {
        for (uint32_t groupIndex = 0; groupIndex < pipelineLayout.getBindGroupLayoutCount(); ++groupIndex)
        {
            const BindGroupLayout &expectedLayout = pipelineLayout.getBindGroupLayoutHandle(groupIndex);
            if (expectedLayout == nullptr)
            {
                continue;
            }

            const auto boundIt = boundDescriptorBindings.find(groupIndex);
            if (boundIt == boundDescriptorBindings.end())
            {
                return false;
            }

            if (!isDescriptorBindingStateCompatibleWithPipelineLayout(boundIt->second, pipelineLayout, groupIndex))
            {
                return false;
            }
        }

        return true;
    }

    eastl::string buildMissingDescriptorBindingMessage(
        const char *apiName,
        const eastl::string &pipelineLabel,
        const VKPipelineLayout *pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings)
    {
        if (pipelineLayout == nullptr)
        {
            return {};
        }
        return buildMissingDescriptorBindingMessage(apiName, pipelineLabel, *pipelineLayout, boundDescriptorBindings);
    }

    eastl::string buildMissingDescriptorBindingMessage(
        const char *apiName,
        const eastl::string &pipelineLabel,
        const VKPipelineLayout &pipelineLayout,
        const eastl::unordered_map<uint32_t, DescriptorBindingState> &boundDescriptorBindings)
    {

        eastl::string message = eastl::string(apiName) + " cannot execute because pipeline '";
        message += pipelineLabel.empty() ? "<unnamed>" : pipelineLabel.c_str();
        message += "' has unresolved bind groups: ";

        bool hasIssue = false;
        for (uint32_t groupIndex = 0; groupIndex < pipelineLayout.getBindGroupLayoutCount(); ++groupIndex)
        {
            const BindGroupLayout &expectedLayout = pipelineLayout.getBindGroupLayoutHandle(groupIndex);
            if (expectedLayout == nullptr)
            {
                continue;
            }

            const auto boundIt = boundDescriptorBindings.find(groupIndex);
            if (boundIt == boundDescriptorBindings.end())
            {
                if (hasIssue)
                {
                    message += "; ";
                }
                message += "set ";
                message += eastl::to_string(groupIndex);
                message += " not bound";
                hasIssue = true;
                continue;
            }

            const DescriptorBindingState &boundState = boundIt->second;
            if (isDescriptorBindingStateCompatibleWithPipelineLayout(boundState, pipelineLayout, groupIndex))
            {
                continue;
            }

            if (hasIssue)
            {
                message += "; ";
            }
            message += "set ";
            message += eastl::to_string(groupIndex);
            if (boundState.descriptorSetOverride)
            {
                message += " bound to incompatible descriptor-set override";
            }
            else
            {
                message += " bound to incompatible bind group '";
                if (boundState.group == nullptr)
                {
                    message += "<null>";
                }
                else
                {
                    const auto &vkBindGroup = static_cast<const VKBindGroup &>(*boundState.group);
                    message += vkBindGroup.getLabelName().c_str();
                }
                message += "'";
            }
            hasIssue = true;
        }

        if (!hasIssue)
        {
            return {};
        }

        message += ". Bound sets: ";
        bool firstBound = true;
        for (const auto &[groupIndex, boundState] : boundDescriptorBindings)
        {
            if (!firstBound)
            {
                message += ", ";
            }
            firstBound = false;
            message += eastl::to_string(groupIndex);
            if (boundState.descriptorSetOverride)
            {
                message += "='<descriptor set override>'";
            }
            else
            {
                message += "='";
                if (boundState.group == nullptr)
                {
                    message += "<null>";
                }
                else
                {
                    const auto &vkBindGroup = static_cast<const VKBindGroup &>(*boundState.group);
                    message += vkBindGroup.getLabelName().c_str();
                }
                message += "'";
            }
        }
        return message;
    }

    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout *pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        eastl::unordered_map<uint32_t, DescriptorBindingState> &nativeBoundDescriptorBindings)
    {
        if (pipelineLayout == nullptr)
        {
            return;
        }
        bindDescriptorBindingStateIfNeeded(
            commandBuffer,
            bindPoint,
            nativePipelineLayout,
            *pipelineLayout,
            groupIndex,
            desiredState,
            nativeBoundDescriptorBindings);
    }

    void bindDescriptorBindingStateIfNeeded(
        vk::CommandBuffer commandBuffer,
        vk::PipelineBindPoint bindPoint,
        vk::PipelineLayout nativePipelineLayout,
        const VKPipelineLayout &pipelineLayout,
        uint32_t groupIndex,
        const DescriptorBindingState &desiredState,
        eastl::unordered_map<uint32_t, DescriptorBindingState> &nativeBoundDescriptorBindings)
    {

        if (!isDescriptorBindingStateCompatibleWithPipelineLayout(desiredState, pipelineLayout, groupIndex))
        {
            nativeBoundDescriptorBindings.erase(groupIndex);
            return;
        }

        const auto boundIt = nativeBoundDescriptorBindings.find(groupIndex);
        if (boundIt != nativeBoundDescriptorBindings.end() &&
            areDescriptorBindingStatesEquivalent(boundIt->second, desiredState))
        {
            return;
        }

        if (desiredState.descriptorSetOverride)
        {
            bindDescriptorSet(
                commandBuffer,
                bindPoint,
                nativePipelineLayout,
                desiredState.descriptorSet,
                groupIndex);
        }
        else
        {
            bindDescriptorSet(
                commandBuffer,
                bindPoint,
                nativePipelineLayout,
                static_cast<const VKBindGroup &>(*desiredState.group),
                groupIndex);
        }

        nativeBoundDescriptorBindings[groupIndex] = desiredState;
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
