#include <EASTL/shared_ptr.h>
#include "TaskGraphDefinition.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <limits>
#include <stdexcept>

namespace GVM::Core::Private
{
    uint32_t TaskGraphDefinition::append(TaskDefinition definition)
    {
        if (!definition.evaluate)
        {
            throw std::invalid_argument("A graph task requires an owning operation.");
        }
        eastl::sort(definition.predecessors.begin(), definition.predecessors.end());
        definition.predecessors.erase(eastl::unique(definition.predecessors.begin(), definition.predecessors.end()), definition.predecessors.end());
        std::lock_guard lock(mMutex);
        if (mTasks.size() >= std::numeric_limits<uint32_t>::max())
        {
            throw std::length_error("TaskGraph exceeds its task index capacity.");
        }
        for (uint32_t predecessor : definition.predecessors)
        {
            if (predecessor >= mTasks.size())
            {
                throw std::invalid_argument("TaskGraph dependencies must reference an earlier task in the same graph.");
            }
        }
        const auto index = static_cast<uint32_t>(mTasks.size());
        mTasks.push_back(eastl::move(definition));
        return index;
    }

    eastl::vector<TaskDefinition> TaskGraphDefinition::snapshot(uint32_t maximumTasks) const
    {
        std::lock_guard lock(mMutex);
        if (mTasks.size() > maximumTasks)
        {
            throw std::length_error("TaskGraph exceeds the runtime task budget.");
        }
        return mTasks;
    }
}
