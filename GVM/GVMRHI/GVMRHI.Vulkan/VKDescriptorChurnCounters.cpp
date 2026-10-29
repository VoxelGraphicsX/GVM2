#include "VKDescriptorChurnCounters.hpp"

namespace GVM::RHI::Vulkan
{
    VKDescriptorChurnSnapshot VKDescriptorChurnCounters::takeAndReset()
    {
        VKDescriptorChurnSnapshot snapshot = {};
#if GVM_CPU_PROBE_ENABLED
        snapshot.bindGroupCreated = mBindGroupCreateCount.exchange(0u, eastl::memory_order_acq_rel);
        snapshot.bindGroupDestroyed = mBindGroupDestroyCount.exchange(0u, eastl::memory_order_acq_rel);
        snapshot.descriptorSetAllocated = mDescriptorSetAllocateCount.exchange(0u, eastl::memory_order_acq_rel);
        snapshot.descriptorSetFreed = mDescriptorSetFreeCount.exchange(0u, eastl::memory_order_acq_rel);
#endif
        return snapshot;
    }
} // namespace GVM::RHI::Vulkan
