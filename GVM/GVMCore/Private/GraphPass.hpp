#pragma once
#include "PassResourceAccess.hpp"
#include "../Public/GraphBuffer.hpp"
#include <EASTL/any.h>
#include <EASTL/functional.h>

namespace GVM::Core::Private
{
    /// Identifies the capability inferred from a logical pass's actual commands.
    enum class GraphPassKind { Compute, Render, Transfer };

    /// Owns one independently recordable logical pass and its immutable resource captures.
    struct GraphPass
    {
        GraphPassKind kind = GraphPassKind::Compute;
        PassResourceUsage resources;
        eastl::function<void(RHI::CommandEncoder)> record;
    };

    /// Owns one node invocation's passes and an optional CPU result resolver after GPU completion.
    struct RenderNodeFragment
    {
        eastl::vector<GraphPass> passes;
        eastl::vector<GraphBuffer> buffers;
        eastl::function<eastl::any()> resolve;
    };
}
