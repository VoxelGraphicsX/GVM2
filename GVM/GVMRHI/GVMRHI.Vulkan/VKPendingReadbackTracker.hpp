#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKResourceStateDB;

    class VKPendingReadbackTracker final
    {
    public:
        void init(VKQueue *queue);
        void enqueueDirect(uint64_t debugId, BufferRange source, void *destination, uint64_t size);
        void enqueueStaging(uint64_t debugId, Buffer stagingBuffer, void *destination, uint64_t size);
        /// Enqueues an async readback that aliases a host-visible source buffer (no copy); the tracker holds a reference until the read completes.
        void enqueueAsyncDirect(uint64_t debugId, const Readback &readback, BufferRange source);
        /// Enqueues an async readback whose bytes are produced in stagingBuffer; the tracker holds a reference until the copy completes.
        void enqueueAsyncStaging(uint64_t debugId, const Readback &readback, Buffer stagingBuffer);

        /// Reports whether the next submit must assign a completion value to any queued reads.
        [[nodiscard]] bool hasUnsubmitted() const;
        /// Finds the latest writer on the foreign queue needed by a newly queued mapped host read.
        [[nodiscard]] uint64_t lastDirectWriter(const VKResourceStateDB &history, uint32_t lane) const;
        [[nodiscard]] size_t pendingCount() const;

        void assignUnassignedSubmissionSerial(VKSubmissionSerial serial);

        /// Returns the last serial whose legacy destination pointer must be filled before submit returns.
        [[nodiscard]] VKSubmissionSerial lastHostCopySerial() const;

        void resolveCompleted(VKDevice *device, uint64_t lastRetiredSubmissionId);
        void destroy(VKDevice *device);

        [[nodiscard]] Logger getLogger() const;

    private:
        struct PendingLinearReadback
        {
            uint64_t debugId = 0u;
            BufferRange source = {};
            Buffer stagingBuffer = {};
            void *destination = nullptr;
            Readback readback = nullptr;
            uint64_t size = 0u;
            VKSubmissionSerial submissionSerial = 0u;
            bool hasSubmissionSerial = false;
            bool usesDirectMapping = false;
        };

        eastl::vector<PendingLinearReadback> mPendingReadbacks;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
    };
} // namespace GVM::RHI::Vulkan
