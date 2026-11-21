#pragma once
#include "PassResourceAccess.hpp"
#include <EASTL/functional.h>
#include <EASTL/map.h>
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>
namespace GVM::Core
{

    struct ComputePassTaskDescriptor
    {

        eastl::function<void(GVM::RHI::ComputePassEncoder)> dispatchFn;
        Private::PassResourceUsage resources;
        uint16_t weight = 8u;
    };

    enum class RenderPassPhaseRequirement
    {
        OrdinaryOrPixelLocal,
        PixelLocalOnly,
    };

    struct RenderPassTaskDescriptor
    {

        eastl::function<void(GVM::RHI::RenderPassEncoder)> drawFn;
        RenderPassPhaseRequirement phaseRequirement = RenderPassPhaseRequirement::OrdinaryOrPixelLocal;
        Private::PassResourceUsage resources;
    };

    /// Marks a boundary between two ordered pixel-local passes inside one render pass.
    struct PixelLocalNextPassDescriptor
    {
    };

    /// Creates a pixel-local pass boundary token for pixelLocalPass argument lists.
    inline PixelLocalNextPassDescriptor nextPixelLocalPass()
    {
        return {};
    }

    /// Stores the ordered pixel-local task groups that queue->renderPass expands into render-pass phases.
    struct PixelLocalPassTaskDescriptor
    {
        eastl::vector<eastl::vector<RenderPassTaskDescriptor>> pixelLocalPasses;

        /// Returns true when the descriptor contains no executable pixel-local tasks.
        bool empty() const
        {
            for (const auto &pass : pixelLocalPasses)
            {
                if (!pass.empty())
                {
                    return false;
                }
            }
            return true;
        }
    };

    namespace Private
    {
        template <class R>
        using TaskArrayDataValue = std::remove_cv_t<std::remove_reference_t<decltype(*std::data(std::declval<const R &>()))>>;

        template <typename R, typename T>
        concept TaskArrayRange = requires(const R &r) {
            { std::data(r) };
            { std::size(r) };
        } && std::is_convertible_v<decltype(std::size(std::declval<const R &>())), std::size_t> &&
            std::is_convertible_v<TaskArrayDataValue<R>, T>;

        template <typename T>
        concept RenderPassTask = std::is_same_v<std::remove_cvref_t<T>, RenderPassTaskDescriptor>;

        template <typename T>
        concept PixelLocalPassBoundary = std::is_same_v<std::remove_cvref_t<T>, PixelLocalNextPassDescriptor>;

        template <typename T>
        concept PixelLocalPassTaskGroup = std::is_same_v<std::remove_cvref_t<T>, PixelLocalPassTaskDescriptor>;

        template <typename T>
        concept PixelLocalPassArg = RenderPassTask<T> || TaskArrayRange<T, RenderPassTaskDescriptor> || PixelLocalPassBoundary<T>;

        template <typename T>
        concept RenderPassPhase = RenderPassTask<T> || PixelLocalPassTaskGroup<T>;

        /// Ensures that a descriptor has a current pixel-local pass before a task is appended.
        inline void ensurePixelLocalPass(PixelLocalPassTaskDescriptor &descriptor)
        {
            if (descriptor.pixelLocalPasses.empty())
            {
                descriptor.pixelLocalPasses.emplace_back();
            }
        }

        /// Removes one trailing empty pixel-local pass produced by a terminal boundary token.
        inline void trimTrailingEmptyPixelLocalPass(PixelLocalPassTaskDescriptor &descriptor)
        {
            if (!descriptor.pixelLocalPasses.empty() && descriptor.pixelLocalPasses.back().empty())
            {
                descriptor.pixelLocalPasses.pop_back();
            }
        }

        /// Appends a pixel-local pass boundary while avoiding duplicate empty passes.
        inline void appendPixelLocalPassArg(PixelLocalPassTaskDescriptor &descriptor, PixelLocalNextPassDescriptor)
        {
            if (!descriptor.pixelLocalPasses.empty() && !descriptor.pixelLocalPasses.back().empty())
            {
                descriptor.pixelLocalPasses.emplace_back();
            }
        }

        /// Appends one pixel-local task to the current pass.
        inline void appendPixelLocalPassArg(PixelLocalPassTaskDescriptor &descriptor, RenderPassTaskDescriptor task)
        {
            ensurePixelLocalPass(descriptor);
            descriptor.pixelLocalPasses.back().emplace_back(std::move(task));
        }

        /// Appends a contiguous range of pixel-local tasks to the current pass.
        template <typename TTasks>
            requires TaskArrayRange<TTasks, RenderPassTaskDescriptor>
        void appendPixelLocalPassArg(PixelLocalPassTaskDescriptor &descriptor, TTasks &&tasks)
        {
            ensurePixelLocalPass(descriptor);
            descriptor.pixelLocalPasses.back().insert(
                descriptor.pixelLocalPasses.back().end(),
                std::data(tasks),
                std::data(tasks) + std::size(tasks));
        }

        /// Tracks the top-level render pass phase while expanding ordinary render tasks and pixel-local task groups.
        struct RenderPassPhaseState
        {
            bool hasEncodedPhase = false;
            bool currentPhaseIsOrdinary = false;
            uint32_t phaseCount = 0u;
        };

        /// Counts top-level render pass phases while folding adjacent ordinary render tasks into one phase.
        struct RenderPassPhaseCounter
        {
            uint32_t phaseCount = 0u;
            bool currentPhaseIsOrdinary = false;
            bool hasPixelLocalPhase = false;
        };

        /// Returns the number of non-empty pixel-local subpasses inside a phase group.
        inline uint32_t getNonEmptyPixelLocalPassCount(const PixelLocalPassTaskDescriptor &tasks)
        {
            uint32_t nonEmptyPassCount = 0u;
            for (const auto &pass : tasks.pixelLocalPasses)
            {
                if (!pass.empty())
                {
                    ++nonEmptyPassCount;
                }
            }
            return nonEmptyPassCount;
        }

        /// Rejects pixel-local-only draw tasks used outside pixelLocalPass(...).
        inline void validateOrdinaryRenderPassTask(const RenderPassTaskDescriptor &task, const char *context)
        {
            if (task.phaseRequirement == RenderPassPhaseRequirement::PixelLocalOnly)
            {
                throw std::invalid_argument((eastl::string(context) + " cannot execute a pixel-local-only RenderPassTaskDescriptor outside pixelLocalPass(...).").c_str());
            }
        }

        /// Validates every task in an ordinary render-pass task range.
        template <typename TTasks>
        void validateOrdinaryRenderPassTaskRange(const TTasks &tasks, const char *context)
        {
            for (const auto &task : tasks)
            {
                validateOrdinaryRenderPassTask(task, context);
            }
        }

        /// Opens or reuses an ordinary subpass phase on the active render pass encoder.
        inline void beginOrdinaryRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state)
        {
            if (state.hasEncodedPhase && state.currentPhaseIsOrdinary)
            {
                return;
            }
            if (state.hasEncodedPhase)
            {
                renderpassEncoder->nextPixelLocalPass();
            }
            state.hasEncodedPhase = true;
            state.currentPhaseIsOrdinary = true;
            ++state.phaseCount;
        }

        /// Advances the render pass encoder to the next pixel-local subpass phase.
        inline void beginPixelLocalRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state)
        {
            if (state.hasEncodedPhase)
            {
                renderpassEncoder->nextPixelLocalPass();
            }
            state.hasEncodedPhase = true;
            state.currentPhaseIsOrdinary = false;
            ++state.phaseCount;
        }

        /// Encodes an ordinary task after phase counting has checked its phase requirement.
        inline void encodeRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state, const RenderPassTaskDescriptor &task)
        {
            beginOrdinaryRenderPassPhase(renderpassEncoder, state);
            task.drawFn(renderpassEncoder);
        }

        /// Encodes each non-empty pixel-local subpass inside a pixel-local task group.
        inline void encodeRenderPassPhase(GVM::RHI::RenderPassEncoder renderpassEncoder, RenderPassPhaseState &state, const PixelLocalPassTaskDescriptor &tasks)
        {
            for (const auto &pass : tasks.pixelLocalPasses)
            {
                if (pass.empty())
                {
                    continue;
                }
                beginPixelLocalRenderPassPhase(renderpassEncoder, state);
                for (const auto &task : pass)
                {
                    task.drawFn(renderpassEncoder);
                }
            }
        }

        /// Updates the phase counter for one ordinary render task.
        inline void countRenderPassPhase(RenderPassPhaseCounter &counter, const RenderPassTaskDescriptor &task, const char *context)
        {
            validateOrdinaryRenderPassTask(task, context);
            if (!counter.currentPhaseIsOrdinary)
            {
                ++counter.phaseCount;
                counter.currentPhaseIsOrdinary = true;
            }
        }

        /// Updates the phase counter for one pixel-local task group.
        inline void countRenderPassPhase(RenderPassPhaseCounter &counter, const PixelLocalPassTaskDescriptor &tasks, const char *)
        {
            const uint32_t nonEmptyPassCount = getNonEmptyPixelLocalPassCount(tasks);
            if (nonEmptyPassCount == 0u)
            {
                return;
            }
            counter.phaseCount += nonEmptyPassCount;
            counter.currentPhaseIsOrdinary = false;
            counter.hasPixelLocalPhase = true;
        }

        /// Resolves total subpass phase count and whether pixel-local storage is enabled.
        template <class... Args>
        RenderPassPhaseCounter resolveRenderPassPhaseCounter(const char *context, const Args &...args)
        {
            RenderPassPhaseCounter counter = {};
            (countRenderPassPhase(counter, args, context), ...);
            if (counter.phaseCount == 0u)
            {
                counter.phaseCount = 1u;
            }
            return counter;
        }
    } // namespace Private

    /// Packs render tasks and nextPixelLocalPass boundaries into a pixel-local render-pass phase group.
    template <class... TArgs>
        requires(sizeof...(TArgs) > 0 && (... && Private::PixelLocalPassArg<TArgs>))
    PixelLocalPassTaskDescriptor pixelLocalPass(TArgs &&...args)
    {
        PixelLocalPassTaskDescriptor descriptor;
        (Private::appendPixelLocalPassArg(descriptor, std::forward<TArgs>(args)), ...);
        Private::trimTrailingEmptyPixelLocalPass(descriptor);
        return descriptor;
    }

    struct BlitPassTaskDescriptor
    {

        eastl::function<void(GVM::RHI::BlitPassEncoder)> blitFn;
    };

} // namespace GVM::Core
