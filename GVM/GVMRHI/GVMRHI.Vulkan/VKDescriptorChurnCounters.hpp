#pragma once

#include "VKCommon.hpp"

#include <GVMRHI/GVMCpuProbe.hpp>

#include <EASTL/atomic.h>
#include <cstdint>

namespace GVM::RHI::Vulkan
{
    /** One drained set of bind-group and descriptor-set churn counters; all fields are zero when GVM_CPU_PROBE_ENABLED=0. */
    struct VKDescriptorChurnSnapshot
    {
        uint64_t bindGroupCreated = 0u;
        uint64_t bindGroupDestroyed = 0u;
        uint64_t descriptorSetAllocated = 0u;
        uint64_t descriptorSetFreed = 0u;
    };

    /** Atomic counters tracking bind-group and descriptor-set churn, drained by the swapchain
     *  descriptor-churn CPU probe. All counters and record methods compile away when
     *  GVM_CPU_PROBE_ENABLED=0, so call sites may invoke the record methods unconditionally. */
    class VKDescriptorChurnCounters final
    {
    public:
        /** Records one bind group creation. */
        void recordBindGroupCreated()
        {
#if GVM_CPU_PROBE_ENABLED
            mBindGroupCreateCount.fetch_add(1u, eastl::memory_order_relaxed);
#endif
        }
        /** Records one bind group destruction. */
        void recordBindGroupDestroyed()
        {
#if GVM_CPU_PROBE_ENABLED
            mBindGroupDestroyCount.fetch_add(1u, eastl::memory_order_relaxed);
#endif
        }
        /** Records one native descriptor set allocation. */
        void recordDescriptorSetAllocated()
        {
#if GVM_CPU_PROBE_ENABLED
            mDescriptorSetAllocateCount.fetch_add(1u, eastl::memory_order_relaxed);
#endif
        }
        /** Records one native descriptor set free. */
        void recordDescriptorSetFreed()
        {
#if GVM_CPU_PROBE_ENABLED
            mDescriptorSetFreeCount.fetch_add(1u, eastl::memory_order_relaxed);
#endif
        }
        /** Drains all counters into one snapshot and resets them to zero. */
        [[nodiscard]] VKDescriptorChurnSnapshot takeAndReset();

    private:
#if GVM_CPU_PROBE_ENABLED
        eastl::atomic<uint64_t> mBindGroupCreateCount{0u};
        eastl::atomic<uint64_t> mBindGroupDestroyCount{0u};
        eastl::atomic<uint64_t> mDescriptorSetAllocateCount{0u};
        eastl::atomic<uint64_t> mDescriptorSetFreeCount{0u};
#endif
    };
} // namespace GVM::RHI::Vulkan
