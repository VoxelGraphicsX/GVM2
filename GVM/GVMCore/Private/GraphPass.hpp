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

    /// Baseline logical cost unit for one ordinary pass and one cross-queue synchronization handoff.
    inline constexpr uint16_t kDefaultPassWeight = 8u;
    inline constexpr uint32_t kQueueHandoffCost = kDefaultPassWeight;

    /// Owns one independently recordable logical pass and its immutable resource captures.
    struct GraphPass
    {
        GraphPassKind kind = GraphPassKind::Compute;
        eastl::string profileLabel;
        uint64_t profileKey = 0;
        PassResourceUsage resources;
        eastl::function<void(RHI::CommandEncoder, const RHI::PassTimestampWrites *)> record;
        eastl::vector<uint32_t> controlPredecessors;
        eastl::vector<RHI::Submission> externalWaits;
        uint32_t completionNode = UINT32_MAX;
        uint16_t weight = kDefaultPassWeight;
        bool requiresMainQueue = false;
        bool hasExplicitTimestampWrites = false;
    };

    /// Owns one node invocation's passes and an optional CPU result resolver after GPU completion.
    struct RenderNodeFragment
    {
        eastl::vector<GraphPass> passes;
        eastl::vector<RHI::Submission> externalWaits;
        eastl::vector<eastl::shared_ptr<GraphResourceAllocation>> allocations;
        eastl::function<eastl::any()> resolve;
    };
}
