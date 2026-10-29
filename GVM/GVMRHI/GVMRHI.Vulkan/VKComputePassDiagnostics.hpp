#pragma once

#include "VKCommon.hpp"
#include "VKLogging.hpp"

#include <GVMRHI/GVMLogging.hpp>
#include <GVMRHI/Private/LoggingCore.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    /** Diagnostics-only counters and label lists recorded while one compute pass is encoded.
     *  All state and all method bodies compile away when GVM_LOGGING_ENABLED=0, so call sites may
     *  invoke the record methods unconditionally. */
    class VKComputePassDiagnostics final
    {
    public:
        /** Prepares collection for a new pass; pipeline and bind-group labels are gathered only when trace logging is enabled. */
        void begin(const Logger &logger)
        {
#if GVM_LOGGING_ENABLED
            mCollectDiagnostics = shouldLog(logger, LogLevel::Trace);
#else
            (void)logger;
#endif
        }
        /** Records a pipeline switch and, while collecting, the pipeline's label. */
        void recordPipeline(const eastl::string &label)
        {
#if GVM_LOGGING_ENABLED
            ++mPipelineSwitchCount;
            if (mCollectDiagnostics)
            {
                appendUniqueLogLabel(mEncounteredPipelineLabels, label);
            }
#else
            (void)label;
#endif
        }
        /** Records a bind-group use and, while collecting, the group's label. */
        void recordBindGroup(const eastl::string &label)
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                appendUniqueLogLabel(mEncounteredBindGroupLabels, label);
            }
#else
            (void)label;
#endif
        }
        /** Records one direct dispatch's workgroup counts. */
        void recordDispatch(uint32_t x, uint32_t y, uint32_t z)
        {
#if GVM_LOGGING_ENABLED
            ++mDispatchCount;
            mHasDispatch = true;
            mLastDispatchWasIndirect = false;
            mLastDispatchX = x;
            mLastDispatchY = y;
            mLastDispatchZ = z;
#else
            (void)x;
            (void)y;
            (void)z;
#endif
        }
        /** Records one indirect dispatch. */
        void recordIndirectDispatch()
        {
#if GVM_LOGGING_ENABLED
            ++mIndirectDispatchCount;
            mHasDispatch = true;
            mLastDispatchWasIndirect = true;
#endif
        }
        /** Emits the end-of-pass trace summary for the pass on the given device. */
        void emitEndOfPassLog(
            VKDevice *device,
            const void *commandEncoder,
            vk::CommandBuffer commandBuffer,
            const eastl::string &passLabel,
            size_t bindGroupCount) const;

    private:
#if GVM_LOGGING_ENABLED
        /** Appends label to labels when it is not already present. */
        static void appendUniqueLabel(eastl::vector<eastl::string> &labels, const eastl::string &label);
        /** Formats up to limit labels as a comma-separated list followed by an ellipsis when truncated. */
        static eastl::string buildLabelList(const eastl::vector<eastl::string> &labels, size_t limit = 8u);

        eastl::vector<eastl::string> mEncounteredPipelineLabels;
        eastl::vector<eastl::string> mEncounteredBindGroupLabels;
        uint32_t mPipelineSwitchCount = 0u;
        uint32_t mDispatchCount = 0u;
        uint32_t mIndirectDispatchCount = 0u;
        uint32_t mLastDispatchX = 0u;
        uint32_t mLastDispatchY = 0u;
        uint32_t mLastDispatchZ = 0u;
        bool mLastDispatchWasIndirect = false;
        bool mHasDispatch = false;
        bool mCollectDiagnostics = false;
#endif
    };
} // namespace GVM::RHI::Vulkan
