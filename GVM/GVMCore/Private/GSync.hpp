#pragma once
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
