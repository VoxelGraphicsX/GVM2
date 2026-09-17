#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKFencePool final
    {
    public:
        void init(VKDevice *device);

        [[nodiscard]]
        vk::UniqueFence acquire();

        void recycle(vk::UniqueFence fence);
        void clear();

    private:
        VKDevice *mDevice = nullptr;
        eastl::vector<vk::UniqueFence> mFences;
    };
} // namespace GVM::RHI::Vulkan
