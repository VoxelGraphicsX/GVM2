#pragma once

#include <GVMRHI/GVMRHI.hpp>

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

} // namespace GVM::RHI::Metal::Detail
