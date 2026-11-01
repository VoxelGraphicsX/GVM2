#pragma once

#include <EASTL/shared_ptr.h>
#include <cstdint>

namespace GVM::Core
{
    namespace Private
    {
        class TaskGraphDefinition;
    }
    class TaskGraph;
    class TaskRuntime;

    /// Names a deferred value and owns its graph definition; it never waits or exposes result storage.
    template <class T>
    class Task final
    {
    public:
        using Value = T;
        /// Creates an unbound output slot; using it as an input or execution output is an error.
        Task() = default;

    private:
        friend class TaskGraph;
        friend class TaskRuntime;
        /// Binds a typed output to its producer without retaining an execution or a caller stack.
        Task(eastl::shared_ptr<Private::TaskGraphDefinition> definition, uint32_t index)
            : mDefinition(eastl::move(definition)), mIndex(index)
        {
        }

        eastl::shared_ptr<Private::TaskGraphDefinition> mDefinition;
        uint32_t mIndex = 0;
    };
}
