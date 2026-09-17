#pragma once
#include "UGL.Format.h"
#include "UGL.Resources.h"
#include <vector>

namespace UGL
{
    enum class SwapchainNextTextureQueryStatus
    {
        Success = 0x00000000,
        Timeout = 0x00000001,
        Outdated = 0x00000002,
        Lost = 0x00000003,
        OutOfMemory = 0x00000004,
        DeviceLost = 0x00000005,
        Force32 = 0x7FFFFFFF
    };

    struct SwapchainQueryResult
    {
        Texture<UGL::TextureFormat::PreferredSwapchain, UGL::TextureUsage<UGL::TextureBinding, UGL::RenderAttachment>, UGL::TextureDimension::e2D> texture;
        SwapchainNextTextureQueryStatus status;
    };

    struct RenderToSwapchainDescriptor
    {
        bool flipYAxis = false;
    };

    struct RuntimeTextureFormat
    {
    };

    class SwapchainImpl
    {
    public:
        std::vector<RuntimeTextureFormat> getSupportedFormats() const
        {
            return {};
        }
        RuntimeTextureFormat getPreferredFormat() const
        {
            return {};
        }
        SwapchainQueryResult queryNextTexture()
        {
            return {
                .texture = {},
                .status = SwapchainNextTextureQueryStatus::Success,
            };
        }
        void present()
        {
        }
    };

    class Swapchain
    {
        SwapchainImpl *impl;

    public:
        SwapchainImpl *operator->()
        {
            return impl;
        }
    };
} // namespace UGL
