#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include "PassSchedule.hpp"

namespace GVM::Core::Private
{
    /// Serializes native object construction and retirement for graphs sharing one borrowed device.
    class GraphDevice final
    {
    public:
        /// Acquires the shared graph-side synchronization domain; the RHI device must outlive all graph resources.
        static eastl::shared_ptr<GraphDevice> acquire(RHI::DeviceImpl &device);
        /// Borrows the native device while its graph synchronization lock is held.
        RHI::DeviceImpl &device() const;
        /// Returns a deduplicated physical candidate; index zero denotes the canonical graphics queue.
        RHI::Queue queue(uint32_t index = 0) const;
        /// Returns deduplicated candidates used only inside the physical pass planner.
        PassQueues candidates() const;
        /// Locks native object creation, mapping and destruction without blocking graph executor joins.
        std::unique_lock<std::recursive_mutex> lock();
    private:
        /// Resolves device queue preferences once before worker callbacks are published.
        explicit GraphDevice(RHI::DeviceImpl &device);
        RHI::DeviceImpl &mDevice;
        eastl::vector<RHI::Queue> mQueues;
        PassQueues mCandidates;
        std::recursive_mutex mMutex;
    };
}
