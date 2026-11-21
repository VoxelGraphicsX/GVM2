#pragma once

#include "../Public/GraphResourceTraits.hpp"
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/any.h>
#include <EASTL/type_traits.h>
#include <GVMRHI/GVMRHI.hpp>
#include <cstdint>
#include <mutex>

namespace GVM::Core { class RenderNode; }

namespace GVM::Core::Private
{
    using TaskResults = eastl::vector<eastl::any>;

    struct TaskGraphOutput;

    /// Describes one owning operation and its earlier graph dependencies, independent of workers.
    struct TaskDefinition
    {
        eastl::vector<uint32_t> predecessors;
        eastl::function<eastl::any(const TaskResults &, RenderNode *)> evaluate;
        eastl::function<TaskGraphOutput(const TaskResults &)> evaluateSubgraph;
        bool buildsSubgraph = false;
        bool buildsTaskSubgraph = false;
    };

    /// Owns a graph definition; execution snapshots isolate callable state between submissions.
    class TaskGraphDefinition final
    {
    public:
        /// Appends an operation whose predecessors must already belong to this definition.
        uint32_t append(TaskDefinition definition);
        /// Copies the current operations into an isolated execution plan within the supplied limit.
        eastl::vector<TaskDefinition> snapshot(uint32_t maximumTasks) const;

    private:
        mutable std::mutex mMutex;
        eastl::vector<TaskDefinition> mTasks;
    };

    /// Identifies the definition and output of a graph entry without borrowing a builder's stack.
    struct TaskGraphOutput
    {
        eastl::shared_ptr<TaskGraphDefinition> definition;
        uint32_t index = 0;
        eastl::function<eastl::any(const eastl::any &)> resolve;
        eastl::function<eastl::vector<const void *>(const eastl::any &)> resources;
    };

    /// Identifies native resource handles, trait-described C++ structs, and compiler-described records.
    template <class T>
    inline constexpr bool containsGraphResources = []
    {
        using U = eastl::remove_cvref_t<T>;
        if constexpr (eastl::is_void_v<U>)
        {
            return false;
        }
        else if constexpr (eastl::is_same_v<U, RHI::Buffer> || eastl::is_same_v<U, RHI::BufferRange> ||
            eastl::is_same_v<U, RHI::Texture> || eastl::is_same_v<U, RHI::TextureView>)
        {
            return true;
        }
        else if constexpr (GraphResourceTraits<U>::containsResources)
        {
            return true;
        }
        else if constexpr (requires { U::__gvm_containsResources; })
        {
            return U::__gvm_containsResources;
        }
        else if constexpr (requires { U::__gvm_hasResources(); })
        {
            return U::__gvm_hasResources();
        }
        else if constexpr (eastl::is_array_v<U>)
        {
            return containsGraphResources<eastl::remove_extent_t<U>>;
        }
        else if constexpr (requires { typename U::value_type; })
        {
            return containsGraphResources<typename U::value_type>;
        }
        else
        {
            return false;
        }
    }();

    /// Visits only GPU allocation identities in a typed output; CPU leaves never expose mapped storage.
    template <class T, class Visitor>
    void visitGraphResources(const T &value, const Visitor &visitor)
    {
        using U = eastl::remove_cvref_t<T>;
        if constexpr (eastl::is_same_v<U, RHI::Buffer> || eastl::is_same_v<U, RHI::Texture>)
        {
            visitor(value.get());
        }
        else if constexpr (eastl::is_same_v<U, RHI::BufferRange>)
        {
            visitor(value.buffer.get());
        }
        else if constexpr (eastl::is_same_v<U, RHI::TextureView>)
        {
            visitor(value.isNull() ? nullptr : value->getSourceTexture());
        }
        else if constexpr (GraphResourceTraits<U>::containsResources)
        {
            GraphResourceTraits<U>::visit(value, visitor);
        }
        else if constexpr (containsGraphResources<U> && requires { value.__gvm_visitResources(visitor); })
        {
            value.__gvm_visitResources(visitor);
        }
        else if constexpr (containsGraphResources<U> && (eastl::is_array_v<U> || requires { value.begin(); value.end(); }))
        {
            for (const auto &element : value)
            {
                visitGraphResources(element, visitor);
            }
        }
    }
}
