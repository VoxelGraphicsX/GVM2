#include "TaskGraph.hpp"

namespace GVM::Core
{
    TaskGraph::TaskGraph()
        : mDefinition(eastl::make_shared<Private::TaskGraphDefinition>())
    {
    }

    TaskGraph::~TaskGraph() = default;
}
