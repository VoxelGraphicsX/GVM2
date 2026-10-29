#pragma once

#include "VKCommon.hpp"

namespace GVM::RHI::Vulkan
{
    [[nodiscard]]
    vk::ImageLayout resolvePixelLocalColorInputLayout();
} // namespace GVM::RHI::Vulkan
