#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::RHI::Vulkan
{
    /** Owns the device's VkPipelineCache and its persisted form.
        The serialized data starts with a GVM header recording the cache format version and the physical device's
        vendor, device, driver version, and pipeline cache UUID; data written by another GVM version, device, or driver
        is ignored when loading, so callers can always pass whatever they stored. */
    class VKPipelineCache final
    {
    public:
        /** Creates the native cache, seeded from initialData when its header matches properties; mismatched or corrupt data starts an empty cache. */
        void init(vk::Device device, const vk::PhysicalDeviceProperties &properties, eastl::span<const uint8_t> initialData);

        /** Destroys the native cache; call before the logical device is destroyed. */
        void destroy();

        /** Returns the native cache to pass to every pipeline creation call, or a null handle before init. */
        [[nodiscard]] vk::PipelineCache getNativePipelineCache() const;

        /** Returns the current cache contents with the GVM header, ready to persist; empty before init or when the driver returns no data. */
        [[nodiscard]] eastl::vector<uint8_t> serialize() const;

        /** Reports whether init accepted the supplied initial data. */
        [[nodiscard]] bool loadedInitialData() const;

    private:
        vk::Device mDevice = nullptr;
        vk::PhysicalDeviceProperties mProperties = {};
        vk::UniquePipelineCache mPipelineCache;
        bool mLoadedInitialData = false;
    };
} // namespace GVM::RHI::Vulkan
