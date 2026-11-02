#pragma once
#include "GraphPass.hpp"

namespace GVM::Core::Private
{
    /// Groups a contiguous planned pass range and preserves only explicit execution waits between batches.
    struct PassBatch
    {
        uint32_t end = 0;
        eastl::vector<uint32_t> waits;
    };

    /// Solves stable pass dependencies without assigning public graph objects to hardware queues.
    class PassSchedule final
    {
    public:
        /// Resolves resource hazards and explicit edges, then orders graphics along a legal business topology.
        explicit PassSchedule(const eastl::vector<GraphPass> &passes);
        /// Returns predecessor pass indices in stable order; readers do not depend on other readers.
        const eastl::vector<eastl::vector<uint32_t>> &predecessors() const;
        /// Returns a deterministic legal order, prioritizing observable readback paths over unrelated work.
        const eastl::vector<uint32_t> &order() const;
        /// Groups the planned order without removing graphics, opaque, control or observable completion boundaries.
        eastl::vector<PassBatch> batches(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const;
        /// Returns exclusive batch ends for the single-queue path, bounding submits and graphics ordering.
        eastl::vector<uint32_t> batchEnds(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const;
    private:
        /// Sorts the dependency DAG and reports invalid edges or cycles before recording begins.
        void sort(const eastl::vector<GraphPass> &passes);
        eastl::vector<eastl::vector<uint32_t>> mPredecessors;
        eastl::vector<eastl::vector<uint32_t>> mControlPredecessors;
        eastl::vector<uint32_t> mOrder;
    };
}
