#include "VKCommon.hpp"
#include "VKInstance.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace GVM::RHI
{
    Instance GVM_RHI_VULKAN_INTERNAL_createInstance(const InstanceDescriptor &descriptor)
    {
        auto *instance = new Vulkan::VKInstance();
        try
        {
            instance->init(descriptor);
            return instance;
        }
        catch (...)
        {
            delete instance;
            throw;
        }
    }
} // namespace GVM::RHI
