#pragma once

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

    /// Describes one owning operation and its earlier graph dependencies, independent of workers.
    struct TaskDefinition
    {
        eastl::vector<uint32_t> predecessors;
        eastl::function<eastl::any(const TaskResults &, RenderNode *)> evaluate;
        bool buildsSubgraph = false;
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

    /// Identifies native resource handles and compiler-described records without inspecting CPU payloads at runtime.
    template <class T>
    inline constexpr bool containsGraphResources = []
    {
        if constexpr (eastl::is_same_v<T, RHI::Buffer> || eastl::is_same_v<T, RHI::BufferRange> ||
            eastl::is_same_v<T, RHI::Texture> || eastl::is_same_v<T, RHI::TextureView>)
        {
            return true;
        }
        else if constexpr (requires { T::__gvm_containsResources; })
        {
            return T::__gvm_containsResources;
        }
        else if constexpr (eastl::is_array_v<T>)
        {
            return containsGraphResources<eastl::remove_extent_t<T>>;
        }
        else if constexpr (requires { typename T::value_type; })
        {
            return containsGraphResources<typename T::value_type>;
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
        if constexpr (eastl::is_same_v<T, RHI::Buffer> || eastl::is_same_v<T, RHI::Texture>)
        {
            visitor(value.get());
        }
        else if constexpr (eastl::is_same_v<T, RHI::BufferRange>)
        {
            visitor(value.buffer.get());
        }
        else if constexpr (eastl::is_same_v<T, RHI::TextureView>)
        {
            visitor(value.isNull() ? nullptr : value->getSourceTexture());
        }
        else if constexpr (requires { value.__gvm_visitResources(visitor); })
        {
            value.__gvm_visitResources(visitor);
        }
        else if constexpr (containsGraphResources<T> && (eastl::is_array_v<T> || requires { value.begin(); value.end(); }))
        {
            for (const auto &element : value)
            {
                visitGraphResources(element, visitor);
            }
        }
    }
}
