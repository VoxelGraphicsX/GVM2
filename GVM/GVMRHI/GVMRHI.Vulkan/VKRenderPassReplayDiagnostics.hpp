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

    /** Diagnostics-only counters and label lists collected while one recorded render pass replays
     *  into a native command buffer. All state and all method bodies compile away when
     *  GVM_LOGGING_ENABLED=0, so replay call sites may invoke the record methods unconditionally. */
    class VKRenderPassReplayDiagnostics final
    {
    public:
        /** Prepares collection for one replay; labels are gathered only when trace logging is enabled. */
        void begin(const Logger &logger)
        {
#if GVM_LOGGING_ENABLED
            mCollectDiagnostics = shouldLog(logger, LogLevel::Trace);
#else
            (void)logger;
#endif
        }
        /** Records a pipeline switch and, while collecting, the pipeline's label. */
        void recordPipelineSwitch(const eastl::string &label)
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                ++mPipelineSwitchCount;
                appendUniqueLogLabel(mPipelineLabels, label);
            }
#else
            (void)label;
#endif
        }
        /** Records a bind-group set and, while collecting, the group's label. */
        void recordBindGroupSet(const eastl::string &label)
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                ++mBindGroupSetCount;
                appendUniqueLogLabel(mBindGroupLabels, label);
            }
#else
            (void)label;
#endif
        }
        /** Records one direct draw call, including the fixed-function pixel-local draw. */
        void recordDraw()
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                ++mDrawCount;
            }
#endif
        }
        /** Records one indexed draw call. */
        void recordDrawIndexed()
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                ++mDrawIndexedCount;
            }
#endif
        }
        /** Records one indirect draw; indexed selects the indexed-indirect counter. */
        void recordDrawIndirect(bool indexed)
        {
#if GVM_LOGGING_ENABLED
            if (mCollectDiagnostics)
            {
                if (indexed)
                {
                    ++mDrawIndexedIndirectCount;
                }
                else
                {
                    ++mDrawIndirectCount;
                }
            }
#else
            (void)indexed;
#endif
        }
        /** Emits the end-of-replay trace summary for the pass on the given device. */
        void emitEndOfReplayLog(
            VKDevice *device,
            const void *commandEncoder,
            vk::CommandBuffer commandBuffer,
            const eastl::string &passLabel,
            vk::RenderPass renderPass,
            vk::Framebuffer framebuffer) const;

    private:
#if GVM_LOGGING_ENABLED
        eastl::vector<eastl::string> mPipelineLabels;
        eastl::vector<eastl::string> mBindGroupLabels;
        uint32_t mPipelineSwitchCount = 0u;
        uint32_t mBindGroupSetCount = 0u;
        uint32_t mDrawCount = 0u;
        uint32_t mDrawIndexedCount = 0u;
        uint32_t mDrawIndirectCount = 0u;
        uint32_t mDrawIndexedIndirectCount = 0u;
        bool mCollectDiagnostics = false;
#endif
    };
} // namespace GVM::RHI::Vulkan
