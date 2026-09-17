#pragma once

#include "VKCommon.hpp"

#include <cstdint>

namespace GVM::RHI::Vulkan::CacheDetail
{
    uint64_t hashTextureViewDescriptor(const TextureViewDescriptor &descriptor);
    bool equalTextureViewDescriptor(const TextureViewDescriptor &lhs, const TextureViewDescriptor &rhs);
} // namespace GVM::RHI::Vulkan::CacheDetail
