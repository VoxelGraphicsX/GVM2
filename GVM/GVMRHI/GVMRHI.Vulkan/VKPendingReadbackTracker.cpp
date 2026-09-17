#include "VKPendingReadbackTracker.hpp"

#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"

#include <cstring>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view PendingReadbackTrackerLogCategory = "gvmrhi.vulkan.pending_readback_tracker";
    } // namespace

    bool VKPendingReadbackTracker::hasPending() const
    {
        return !mPendingReadbacks.empty();
    }

    size_t VKPendingReadbackTracker::pendingCount() const
    {
        return mPendingReadbacks.size();
    }

    void VKPendingReadbackTracker::init(VKQueue *queue)
    {
        mLogger = queue != nullptr ? queue->getLogger() : Logger{};
        mLogContext = queue != nullptr ? queue->getLogContext() : eastl::shared_ptr<Internal::LogContext>{};
    }

    void VKPendingReadbackTracker::enqueueDirect(uint64_t debugId, BufferRange source, void *destination, uint64_t size)
    {
        if (source.buffer.isNull() || destination == nullptr || size == 0u)
        {
            throw makeInvalidArgument("VKPendingReadbackTracker::enqueueDirect requires a valid source buffer, destination pointer, and size.");
        }

        mPendingReadbacks.push_back({
            .debugId = debugId,
            .source = source,
            .stagingBuffer = {},
            .destination = destination,
            .size = size,
            .completion = {},
            .usesDirectMapping = true,
        });
        GVMLogTrace(
            this, PendingReadbackTrackerLogCategory,
            "event=readback_enqueue readback_id={} type=direct size={} pending_after={}",
            debugId,
            size,
            mPendingReadbacks.size());
    }

    void VKPendingReadbackTracker::enqueueStaging(uint64_t debugId, Buffer stagingBuffer, void *destination, uint64_t size)
    {
        if (stagingBuffer.isNull() || destination == nullptr || size == 0u)
        {
            throw makeInvalidArgument("VKPendingReadbackTracker::enqueueStaging requires a valid staging buffer, destination pointer, and size.");
        }

        mPendingReadbacks.push_back({
            .debugId = debugId,
            .source = {},
            .stagingBuffer = stagingBuffer,
            .destination = destination,
            .size = size,
            .completion = {},
            .usesDirectMapping = false,
        });
        GVMLogTrace(
            this, PendingReadbackTrackerLogCategory,
            "event=readback_enqueue readback_id={} type=staging size={} pending_after={}",
            debugId,
            size,
            mPendingReadbacks.size());
    }

    void VKPendingReadbackTracker::assignUnassignedCompletion(const VKSubmissionCompletion &completion)
    {
        size_t assignedCount = 0u;
        for (PendingLinearReadback &pending : mPendingReadbacks)
        {
            if (!pending.completion)
            {
                pending.completion = completion;
                ++assignedCount;
                GVMLogTrace(
                    this, PendingReadbackTrackerLogCategory,
                    "event=readback_assign_completion readback_id={} submission_id={} size={} direct={}",
                    pending.debugId,
                    submissionDiagnosticId(completion),
                    pending.size,
                    pending.usesDirectMapping);
            }
        }
        if (assignedCount != 0u)
        {
            GVMLogDebug(
                this, PendingReadbackTrackerLogCategory,
                "event=readback_assign_completion_batch assigned={} submission_id={} pending_total={}",
                assignedCount,
                submissionDiagnosticId(completion),
                mPendingReadbacks.size());
        }
    }

    VKSubmissionCompletion VKPendingReadbackTracker::getLastAssignedCompletion() const
    {
        VKSubmissionCompletion completion;
        for (const PendingLinearReadback &pending : mPendingReadbacks)
        {
            if (!pending.completion)
            {
                continue;
            }
            completion = pending.completion;
        }
        return completion;
    }

    Logger VKPendingReadbackTracker::getLogger() const
    {
        return mLogger;
    }

    void VKPendingReadbackTracker::resolveCompleted(VKDevice *device)
    {
        for (size_t index = 0; index < mPendingReadbacks.size();)
        {
            PendingLinearReadback &pending = mPendingReadbacks[index];
            if (!pending.completion || !pending.completion->completed.load(std::memory_order_acquire))
            {
                ++index;
                continue;
            }

            GVMLogDebug(
                this, PendingReadbackTrackerLogCategory,
                "event=readback_resolve_begin readback_id={} submission_id={} size={} direct={} pending_before={}",
                pending.debugId,
                submissionDiagnosticId(pending.completion),
                pending.size,
                pending.usesDirectMapping,
                mPendingReadbacks.size());

            Buffer sourceBuffer = pending.usesDirectMapping ? pending.source.buffer : pending.stagingBuffer;
            bool isMapped = false;
            try
            {
                sourceBuffer->map();
                isMapped = true;
                const void *mapped = sourceBuffer->getConstMappedRange(pending.source.offset, pending.size);
                if (mapped == nullptr)
                {
                    throw makeRuntimeError("VKPendingReadbackTracker::resolveCompleted received a null mapped pointer while resolving readback.");
                }
                memcpy(pending.destination, mapped, pending.size);
                sourceBuffer->unmap();
                isMapped = false;
            }
            catch (...)
            {
                if (isMapped)
                {
                    sourceBuffer->unmap();
                }
                throw;
            }

            if (!pending.usesDirectMapping && device != nullptr && !pending.stagingBuffer.isNull())
            {
                device->freeBuffer(pending.stagingBuffer);
            }
            if (pending.usesDirectMapping && device != nullptr && !pending.source.buffer.isNull())
            {
                device->releasePendingCommandBufferReference(pending.source.buffer);
            }
            GVMLogDebug(
                this, PendingReadbackTrackerLogCategory,
                "event=readback_resolve_end readback_id={} submission_id={} size={} direct={} pending_after={}",
                pending.debugId,
                submissionDiagnosticId(pending.completion),
                pending.size,
                pending.usesDirectMapping,
                mPendingReadbacks.size() - 1u);
            mPendingReadbacks.erase(mPendingReadbacks.begin() + index);
        }
    }

    void VKPendingReadbackTracker::destroy(VKDevice *device)
    {
        GVMLogInfo(this, PendingReadbackTrackerLogCategory, "event=readback_destroy pending_count={}", mPendingReadbacks.size());
        if (device != nullptr)
        {
            for (PendingLinearReadback &pending : mPendingReadbacks)
            {
                GVMLogDebug(
                    this, PendingReadbackTrackerLogCategory,
                    "event=readback_destroy_entry readback_id={} submission_id={} size={} direct={}",
                    pending.debugId,
                    submissionDiagnosticId(pending.completion),
                    pending.size,
                    pending.usesDirectMapping);
                if (pending.usesDirectMapping && !pending.source.buffer.isNull())
                {
                    device->releasePendingCommandBufferReference(pending.source.buffer);
                }
                else if (!pending.stagingBuffer.isNull())
                {
                    device->freeBuffer(pending.stagingBuffer);
                }
            }
        }
        mPendingReadbacks.clear();
        mLogger = nullptr;
        mLogContext.reset();
    }
} // namespace GVM::RHI::Vulkan
