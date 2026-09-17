#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <stdexcept>
#include <string>

namespace GVM::RHI::Metal::Detail
{
    enum class BindGroupEntryKind : uint8_t
    {
        Undefined,
        Buffer,
        Sampler,
        TextureView,
    };

    inline BindGroupEntryKind getBindGroupLayoutEntryKind(const BindGroupLayoutEntry &entry)
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

    inline BindGroupEntryKind getBindGroupEntryKind(const BindGroupEntry &entry)
    {
        const bool hasBuffers = !entry.buffer.empty();
        const bool hasSamplers = !entry.sampler.empty();
        const bool hasTextureViews = !entry.textureView.empty();
        const uint32_t populatedKinds = static_cast<uint32_t>(hasBuffers) + static_cast<uint32_t>(hasSamplers) + static_cast<uint32_t>(hasTextureViews);
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

    inline size_t getBindGroupEntryResourceCount(const BindGroupEntry &entry, BindGroupEntryKind kind)
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

    inline eastl::vector<size_t> resolveBindGroupEntryIndices(
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
                throw std::invalid_argument("MBindGroup layout contains duplicate binding numbers.");
            }
            if (getBindGroupLayoutEntryKind(layoutEntry) == BindGroupEntryKind::Undefined)
            {
                throw std::invalid_argument("MBindGroup layout contains an entry with an undefined binding type.");
            }
        }

        eastl::unordered_map<uint32_t, size_t> descriptorBindingToIndex;
        descriptorBindingToIndex.reserve(descriptorEntries.size());
        for (size_t descriptorIndex = 0; descriptorIndex < descriptorEntries.size(); ++descriptorIndex)
        {
            const auto &entry = descriptorEntries[descriptorIndex];
            if (!descriptorBindingToIndex.emplace(entry.binding, descriptorIndex).second)
            {
                throw std::invalid_argument("MBindGroup descriptor contains duplicate binding numbers.");
            }

            const auto entryKind = getBindGroupEntryKind(entry);
            if (entryKind == BindGroupEntryKind::Undefined)
            {
                throw std::invalid_argument("MBindGroup descriptor entries must populate exactly one resource array.");
            }

            const auto layoutIt = layoutBindingToIndex.find(entry.binding);
            if (layoutIt == layoutBindingToIndex.end())
            {
                throw std::invalid_argument("MBindGroup descriptor contains a binding that is not declared by the layout.");
            }

            const auto &layoutEntry = layoutDescriptor.entries[layoutIt->second];
            const auto layoutKind = getBindGroupLayoutEntryKind(layoutEntry);
            if (entryKind != layoutKind)
            {
                throw std::invalid_argument("MBindGroup descriptor binding type does not match the layout binding type.");
            }

            const size_t resourceCount = getBindGroupEntryResourceCount(entry, entryKind);
            if (resourceCount == 0u || resourceCount > layoutEntry.maxCount)
            {
                throw std::invalid_argument("MBindGroup descriptor resource count is incompatible with layout.maxCount.");
            }
        }

        if (descriptorEntries.size() != layoutDescriptor.entries.size())
        {
            throw std::invalid_argument("MBindGroup descriptor entry count must match the layout entry count.");
        }

        eastl::vector<size_t> resolvedIndices(layoutDescriptor.entries.size(), size_t(-1));
        for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
        {
            const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
            const auto descriptorIt = descriptorBindingToIndex.find(layoutEntry.binding);
            if (descriptorIt == descriptorBindingToIndex.end())
            {
                throw std::invalid_argument("MBindGroup descriptor is missing a binding declared by the layout.");
            }
            resolvedIndices[layoutIndex] = descriptorIt->second;
        }
        return resolvedIndices;
    }
} // namespace GVM::RHI::Metal::Detail
