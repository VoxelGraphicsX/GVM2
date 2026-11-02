#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/shared_ptr.h>
#include <mutex>

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
        /// Returns the canonical graphics-capable fallback used by the single-queue scheduler.
        RHI::Queue queue() const;
        /// Locks native object creation, mapping and destruction without blocking graph executor joins.
        std::unique_lock<std::recursive_mutex> lock();
    private:
        /// Resolves the canonical queue once before worker callbacks are published.
        explicit GraphDevice(RHI::DeviceImpl &device);
        RHI::DeviceImpl &mDevice;
        RHI::Queue mQueue;
        std::recursive_mutex mMutex;
    };
}
