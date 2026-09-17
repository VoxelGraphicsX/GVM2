#pragma once

#include "VKCommon.hpp"

#include <EASTL/string_view.h>

namespace GVM::RHI::Vulkan
{
    class VKQueue;

    struct TaskDependencyResolverDiagnostics
    {
        uint64_t bufferPrepareCalls = 0u;
        uint64_t bufferBarrierCount = 0u;
        uint64_t bufferPipelineBarrierCallCount = 0u;
        uint64_t bufferGlobalMemoryBarrierCount = 0u;
        uint64_t bufferPromotedBarrierCount = 0u;
        uint64_t bufferWriteBarrierCount = 0u;
        uint64_t bufferOutstandingWriteBarrierCount = 0u;
        uint64_t bufferSuspiciousRepeatedReadBarrierCount = 0u;
        uint64_t bufferOutstandingWriteConsumedCount = 0u;
        uint64_t texturePrepareCalls = 0u;
        uint64_t texturePrepareSubresourceCount = 0u;
        uint64_t textureBarrierCount = 0u;
        uint64_t textureBarrierSubresourceCount = 0u;
        uint64_t textureLayoutBarrierCount = 0u;
        uint64_t textureStageBarrierCount = 0u;
        uint64_t textureAccessBarrierCount = 0u;
        uint64_t textureWriteBarrierCount = 0u;
        uint64_t textureSteadyStateRestoreCalls = 0u;

        void reset();
    };

    namespace TaskDependencyResolverDiagnosticUtils
    {
        inline constexpr eastl::string_view LogCategory = "gvmrhi.vulkan.command_resource_state_tracker";

        [[nodiscard]]
        uint64_t stageMaskBits(vk::PipelineStageFlags flags);

        [[nodiscard]]
        uint64_t accessMaskBits(vk::AccessFlags flags);

        [[nodiscard]]
        bool shouldPromoteBufferBarrierGroupToMemoryBarrier(
            vk::PipelineStageFlags srcStageMask,
            vk::PipelineStageFlags dstStageMask,
            vk::AccessFlags srcAccessMask,
            vk::AccessFlags dstAccessMask,
            size_t barrierCount);

        void emitFinalize(
            const VKQueue &queue,
            const TaskDependencyResolverDiagnostics &diagnostics,
            size_t trackedBufferCount,
            size_t trackedTextureCount,
            size_t finalBufferCount,
            size_t finalTextureCount,
            size_t trackedBufferRangeCount);
    } // namespace TaskDependencyResolverDiagnosticUtils
} // namespace GVM::RHI::Vulkan
