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

        [[nodiscard]] bool hasPending() const;
        [[nodiscard]] size_t pendingCount() const;

        void assignUnassignedCompletion(const VKSubmissionCompletion &completion);

        [[nodiscard]]
        VKSubmissionCompletion getLastAssignedCompletion() const;

        void resolveCompleted(VKDevice *device);
        void destroy(VKDevice *device);

        [[nodiscard]] Logger getLogger() const;

    private:
        struct PendingLinearReadback
        {
            uint64_t debugId = 0u;
            BufferRange source = {};
            Buffer stagingBuffer = {};
            void *destination = nullptr;
            uint64_t size = 0u;
            VKSubmissionCompletion completion = {};
            bool usesDirectMapping = false;
        };

        eastl::vector<PendingLinearReadback> mPendingReadbacks;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
    };
} // namespace GVM::RHI::Vulkan
