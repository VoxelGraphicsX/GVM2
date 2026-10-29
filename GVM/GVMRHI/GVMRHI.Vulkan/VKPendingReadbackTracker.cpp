#include "VKPendingReadbackTracker.hpp"
#include "VKReadback.hpp"

#include "VKDevice.hpp"
#include "VKBuffer.hpp"
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
            throw std::invalid_argument("VKPendingReadbackTracker::enqueueDirect requires a valid source buffer, destination pointer, and size.");
        }

        mPendingReadbacks.push_back({
            .debugId = debugId,
            .source = source,
            .stagingBuffer = {},
            .destination = destination,
            .size = size,
            .submissionSerial = 0u,
            .hasSubmissionSerial = false,
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
            throw std::invalid_argument("VKPendingReadbackTracker::enqueueStaging requires a valid staging buffer, destination pointer, and size.");
        }

        mPendingReadbacks.push_back({
            .debugId = debugId,
            .source = {},
            .stagingBuffer = stagingBuffer,
            .destination = destination,
            .size = size,
            .submissionSerial = 0u,
            .hasSubmissionSerial = false,
            .usesDirectMapping = false,
        });
        GVMLogTrace(
            this, PendingReadbackTrackerLogCategory,
            "event=readback_enqueue readback_id={} type=staging size={} pending_after={}",
            debugId,
            size,
            mPendingReadbacks.size());
    }

    void VKPendingReadbackTracker::enqueueAsyncDirect(uint64_t debugId, const Readback &readback, BufferRange source)
    {
        if (readback == nullptr || source.buffer.isNull())
        {
            throw std::invalid_argument("VKPendingReadbackTracker::enqueueAsyncDirect requires a readback and a valid source buffer.");
        }
        mPendingReadbacks.push_back({
            .debugId = debugId,
            .source = source,
            .readback = readback,
            .usesDirectMapping = true,
        });
    }

    void VKPendingReadbackTracker::enqueueAsyncStaging(uint64_t debugId, const Readback &readback, Buffer stagingBuffer)
    {
        if (readback == nullptr || stagingBuffer.isNull())
        {
            throw std::invalid_argument("VKPendingReadbackTracker::enqueueAsyncStaging requires a readback and a staging buffer.");
        }
        mPendingReadbacks.push_back({
            .debugId = debugId,
            .stagingBuffer = stagingBuffer,
            .readback = readback,
            .usesDirectMapping = false,
        });
    }

    void VKPendingReadbackTracker::assignUnassignedSubmissionSerial(VKSubmissionSerial serial)
    {
        size_t assignedCount = 0u;
        for (PendingLinearReadback &pending : mPendingReadbacks)
        {
            if (!pending.hasSubmissionSerial)
            {
                pending.submissionSerial = serial;
                pending.hasSubmissionSerial = true;
                if (pending.readback != nullptr)
                {
                    static_cast<VKReadbackImpl *>(pending.readback.get())->assignSubmissionSerial(serial);
                }
                ++assignedCount;
                GVMLogTrace(
                    this, PendingReadbackTrackerLogCategory,
                    "event=readback_assign_completion readback_id={} submission_id={} size={} direct={}",
                    pending.debugId,
                    serial,
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
                serial,
                mPendingReadbacks.size());
        }
    }

    VKSubmissionSerial VKPendingReadbackTracker::getLastAssignedSubmissionSerial() const
    {
        VKSubmissionSerial serial = 0u;
        for (const PendingLinearReadback &pending : mPendingReadbacks)
        {
            if (!pending.hasSubmissionSerial)
            {
                continue;
            }
            serial = pending.submissionSerial;
        }
        return serial;
    }

    Logger VKPendingReadbackTracker::getLogger() const
    {
        return mLogger;
    }

    void VKPendingReadbackTracker::resolveCompleted(VKDevice *device, uint64_t lastRetiredSubmissionId)
    {
        for (size_t index = 0; index < mPendingReadbacks.size();)
        {
            PendingLinearReadback &pending = mPendingReadbacks[index];
            if (!pending.hasSubmissionSerial || pending.submissionSerial > lastRetiredSubmissionId)
            {
                ++index;
                continue;
            }

            GVMLogDebug(
                this, PendingReadbackTrackerLogCategory,
                "event=readback_resolve_begin readback_id={} submission_id={} size={} direct={} pending_before={}",
                pending.debugId,
                pending.submissionSerial,
                pending.size,
                pending.usesDirectMapping,
                mPendingReadbacks.size());

            Buffer sourceBuffer = pending.usesDirectMapping ? pending.source.buffer : pending.stagingBuffer;
            auto &buffer = *static_cast<VKBuffer *>(sourceBuffer.get());
            const auto *mapped = static_cast<const uint8_t *>(buffer.getPersistentMappedData());
            if (mapped == nullptr)
                throw std::runtime_error("Vulkan readback requires host-visible persistent storage.");
            // A readback must not use the writable unmap path or publish a fictitious host write.
            buffer.invalidateAllocation(pending.source.offset, pending.size);
            if (pending.readback != nullptr)
            {
                static_cast<VKReadbackImpl *>(pending.readback.get())->markReady();
            }
            else
            {
                memcpy(pending.destination, mapped + pending.source.offset, pending.size);
            }

            if (!pending.usesDirectMapping && device != nullptr && !pending.stagingBuffer.isNull() && pending.readback == nullptr)
            {
                device->freeBuffer(pending.stagingBuffer);
            }
            if (pending.usesDirectMapping && device != nullptr && !pending.source.buffer.isNull() && pending.readback == nullptr)
            {
                static_cast<VKBuffer *>(pending.source.buffer.get())->releaseReference();
            }
            GVMLogDebug(
                this, PendingReadbackTrackerLogCategory,
                "event=readback_resolve_end readback_id={} submission_id={} size={} direct={} pending_after={}",
                pending.debugId,
                pending.submissionSerial,
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
                    pending.submissionSerial,
                    pending.size,
                    pending.usesDirectMapping);
                if (pending.usesDirectMapping && !pending.source.buffer.isNull())
                {
                    static_cast<VKBuffer *>(pending.source.buffer.get())->releaseReference();
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
