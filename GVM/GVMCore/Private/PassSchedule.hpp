#pragma once
#include "GraphPass.hpp"

namespace GVM::Core::Private
{
    /// Solves stable pass dependencies without assigning public graph objects to hardware queues.
    class PassSchedule final
    {
    public:
        /// Builds RAW/WAR/WAW and graphics-order edges from an already topologically ordered node expansion.
        explicit PassSchedule(const eastl::vector<GraphPass> &passes);
        /// Returns predecessor pass indices in stable order; readers do not depend on other readers.
        const eastl::vector<eastl::vector<uint32_t>> &predecessors() const;
        /// Returns exclusive batch ends for the single-queue path, bounding submits and graphics ordering.
        eastl::vector<uint32_t> batchEnds(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const;
    private:
        eastl::vector<eastl::vector<uint32_t>> mPredecessors;
    };
}
