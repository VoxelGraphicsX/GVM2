#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include "GBackendAvailability.hpp"

namespace GVM::RHI
{
#if GVM_RHI_FACTORY_HAS_METAL
    Instance GVM_RHI_METAL_INTERNAL_createInstance(const InstanceDescriptor &descriptor);
#endif

#if GVM_RHI_FACTORY_HAS_VULKAN
    Instance GVM_RHI_VULKAN_INTERNAL_createInstance(const InstanceDescriptor &descriptor);
#endif
} // namespace GVM::RHI
