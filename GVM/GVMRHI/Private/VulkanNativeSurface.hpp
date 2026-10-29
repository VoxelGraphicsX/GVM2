#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>

namespace GVM::RHI::Vulkan::Private
{
    enum class NativeSurfaceKind : uint32_t
    {
        Undefined = 0u,
        MetalLayer = 1u,
        Win32 = 2u,
        Android = 3u,
        Wayland = 4u,
        Xcb = 5u,
        Xlib = 6u,
    };

    struct NativeSurfaceInfo
    {
        static constexpr uint32_t Magic = 0x56534B31u; // "VSK1"

        uint32_t magic = Magic;
        uint32_t abiVersion = 1u;
        NativeSurfaceKind kind = NativeSurfaceKind::Undefined;
        void *displayOrInstance = nullptr;
        const Extent3D *extentHint = nullptr;
    };

    inline const NativeSurfaceInfo *tryGetNativeSurfaceInfo(const void *payload)
    {
        if (payload == nullptr)
        {
            return nullptr;
        }

        const auto *info = static_cast<const NativeSurfaceInfo *>(payload);
        if (info->magic != NativeSurfaceInfo::Magic)
        {
            return nullptr;
        }
        return info;
    }
} // namespace GVM::RHI::Vulkan::Private
