#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
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

        [[nodiscard]] bool hasPending() const;
        [[nodiscard]] size_t pendingCount() const;

        void assignUnassignedSubmissionSerial(VKSubmissionSerial serial);

        [[nodiscard]]
        VKSubmissionSerial getLastAssignedSubmissionSerial() const;

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
