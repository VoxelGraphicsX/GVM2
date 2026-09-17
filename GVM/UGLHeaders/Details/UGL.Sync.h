#pragma once
#include "UGL.Resources.h"
#include <EASTL/vector.h>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace UGL
{
    struct TimestampQuerySupport
    {
        uint32_t validBits = 0;
        double tickPeriodNs = 1.0;
    };

    struct PassTimestampWrites
    {
        uint32_t beginningOfPassWriteIndex = 0;
        uint32_t endOfPassWriteIndex = 0;
    };

    struct PassCounterWrites
    {
        uint32_t stageUtilizationBeginIndex = 0;
        uint32_t stageUtilizationEndIndex = 0;
        uint32_t statisticBeginIndex = 0;
        uint32_t statisticEndIndex = 0;
    };

    struct PassCounterQuerySupport
    {
        bool supported = false;
        string backendName = "";
        bool requiresExclusiveProfilingLock = false;
        bool supportsSingleSubmitCounterPass = false;
        string unsupportedReason = "";
    };

    struct PassCounterStageUtilizationRawResult
    {
        uint64_t totalCycles = 0;
        uint64_t vertexCycles = 0;
        uint64_t tessellationCycles = 0;
        uint64_t postTessellationVertexCycles = 0;
        uint64_t fragmentCycles = 0;
        uint64_t renderTargetCycles = 0;
    };

    struct PassCounterStatisticRawResult
    {
        uint64_t tessellationInputPatches = 0;
        uint64_t vertexInvocations = 0;
        uint64_t postTessellationVertexInvocations = 0;
        uint64_t clipperInvocations = 0;
        uint64_t clipperPrimitivesOut = 0;
        uint64_t fragmentInvocations = 0;
        uint64_t fragmentsPassed = 0;
        uint64_t computeKernelInvocations = 0;
    };

    struct PassCounterRangeResult
    {
        string label = "";
        PassCounterStageUtilizationRawResult stage = {};
        PassCounterStatisticRawResult statistic = {};
    };

    class GpuTimestampFrameProfiler
    {
    public:
        struct Scope
        {
            string label = "";
            PassTimestampWrites timestampWrites = {};
            PassCounterWrites counterWrites = {};
        };

        void reset()
        {
        }

        Scope writePass(string)
        {
            return {};
        }

        uint32_t getUsedQueryCount() const
        {
            return 0;
        }

        Buffer<uint64_t, BufferUsage<CopySrc>> getResolveBuffer() const
        {
            return {};
        }

        TimestampQuerySupport getSupport() const
        {
            return {};
        }

        eastl::vector<Scope> getScopes() const
        {
            return {};
        }
    };

    class GpuPassCounterFrameProfiler
    {
    public:
        struct Scope
        {
            string label = "";
            PassCounterWrites counterWrites = {};
        };

        void reset()
        {
        }

        Scope writePass(string)
        {
            return {};
        }

        uint32_t getUsedQueryCount() const
        {
            return 0;
        }

        Buffer<PassCounterStageUtilizationRawResult, BufferUsage<CopySrc>> getStageUtilizationResolveBuffer() const
        {
            return {};
        }

        Buffer<PassCounterStatisticRawResult, BufferUsage<CopySrc>> getStatisticResolveBuffer() const
        {
            return {};
        }

        eastl::vector<PassCounterRangeResult> buildRangeResults(const eastl::vector<PassCounterStageUtilizationRawResult> &, const eastl::vector<PassCounterStatisticRawResult> &) const
        {
            return {};
        }
    };

    struct ComputePassTaskDescriptor
    {
    };

    enum class RenderPassPhaseRequirement
    {
        OrdinaryOrPixelLocal,
        PixelLocalOnly,
    };

    struct RenderPassTaskDescriptor
    {
        RenderPassPhaseRequirement phaseRequirement = RenderPassPhaseRequirement::OrdinaryOrPixelLocal;
    };

    /// Represents a boundary between adjacent pixel-local passes inside one render pass.
    struct PixelLocalNextPassDescriptor
    {
    };

    /// Creates a boundary token that separates two pixel-local passes in pixelLocalPass(...).
    inline PixelLocalNextPassDescriptor nextPixelLocalPass()
    {
        return {};
    }

    /// Represents a grouped pixel-local phase that queue->renderPass can execute between ordinary render tasks.
    struct PixelLocalPassTaskDescriptor
    {
    };

    namespace Private
    {
        template <typename R, typename T>
        concept TaskArrayRange = std::ranges::contiguous_range<R> && std::convertible_to<std::ranges::range_value_t<R>, T>;

        template <typename T>
        concept RenderPassTask = std::same_as<std::remove_cvref_t<T>, RenderPassTaskDescriptor>;

        template <typename T>
        concept PixelLocalPassBoundary = std::same_as<std::remove_cvref_t<T>, PixelLocalNextPassDescriptor>;

        template <typename T>
        concept PixelLocalPassTaskGroup = std::same_as<std::remove_cvref_t<T>, PixelLocalPassTaskDescriptor>;

        template <typename T>
        concept PixelLocalPassArg = RenderPassTask<T> || TaskArrayRange<T, RenderPassTaskDescriptor> || PixelLocalPassBoundary<T>;

        template <typename T>
        concept RenderPassPhase = RenderPassTask<T> || PixelLocalPassTaskGroup<T>;
    } // namespace Private

    /// Packs render tasks and nextPixelLocalPass boundaries into a pixel-local render-pass phase group.
    template <class... TArgs>
        requires(sizeof...(TArgs) > 0 && (... && Private::PixelLocalPassArg<TArgs>))
    PixelLocalPassTaskDescriptor pixelLocalPass(TArgs &&...)
    {
        return {};
    }

    struct BlitPassTaskDescriptor
    {
    };
} // namespace UGL
