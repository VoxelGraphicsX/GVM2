#pragma once
#include "PassResourceAccess.hpp"
#include <EASTL/shared_ptr.h>
#include <EASTL/any.h>
#include <EASTL/functional.h>

namespace GVM::Core::Private
{
    class GraphResourceAllocation;

    /// Identifies the capability inferred from a logical pass's actual commands.
    enum class GraphPassKind { Compute, Render, Transfer };

    /// Owns one independently recordable logical pass and its immutable resource captures.
    struct GraphPass
    {
        GraphPassKind kind = GraphPassKind::Compute;
        PassResourceUsage resources;
        eastl::function<void(RHI::CommandEncoder)> record;
        eastl::vector<uint32_t> controlPredecessors;
        uint32_t completionNode = UINT32_MAX;
    };

    /// Owns one node invocation's passes and an optional CPU result resolver after GPU completion.
    struct RenderNodeFragment
    {
        eastl::vector<GraphPass> passes;
        eastl::vector<eastl::shared_ptr<GraphResourceAllocation>> allocations;
        eastl::function<eastl::any()> resolve;
    };
}
