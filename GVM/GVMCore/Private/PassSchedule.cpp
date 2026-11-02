#include "PassSchedule.hpp"
#include <stdexcept>

namespace GVM::Core::Private
{
    PassSchedule::PassSchedule(const eastl::vector<GraphPass> &passes)
        : mPredecessors(passes.size())
    {
        uint32_t lastRender = UINT32_MAX;
        for (uint32_t current = 0; current < passes.size(); ++current)
        {
            const auto &pass = passes[current];
            for (uint32_t earlier = 0; earlier < current; ++earlier)
            {
                const auto &previous = passes[earlier];
                bool dependency = !pass.resources.complete || !previous.resources.complete;
                dependency = dependency || (pass.kind == GraphPassKind::Render && earlier == lastRender);
                for (const auto &access : pass.resources.accesses)
                {
                    if (access.resource == nullptr)
                    {
                        dependency = true;
                    }
                    for (const auto &prior : previous.resources.accesses)
                    {
                        dependency = dependency || prior.resource == nullptr || access.conflicts(prior);
                    }
                }
                if (dependency)
                {
                    mPredecessors[current].push_back(earlier);
                }
            }
            if (pass.kind == GraphPassKind::Render)
            {
                lastRender = current;
            }
        }
    }

    const eastl::vector<eastl::vector<uint32_t>> &PassSchedule::predecessors() const
    {
        return mPredecessors;
    }

    eastl::vector<uint32_t> PassSchedule::batchEnds(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const
    {
        if (maximumBatchSize == 0)
        {
            throw std::invalid_argument("GPU batch size must be nonzero.");
        }
        eastl::vector<uint32_t> ends;
        uint32_t begin = 0;
        bool hasRender = false;
        for (uint32_t index = 0; index < passes.size(); ++index)
        {
            const bool opaque = !passes[index].resources.complete;
            if (index != begin && (index - begin == maximumBatchSize || opaque ||
                (hasRender && passes[index].kind == GraphPassKind::Render)))
            {
                ends.push_back(index);
                begin = index;
                hasRender = false;
            }
            hasRender = hasRender || passes[index].kind == GraphPassKind::Render;
            if (opaque)
            {
                ends.push_back(index + 1);
                begin = index + 1;
                hasRender = false;
            }
        }
        if (begin != passes.size())
        {
            ends.push_back(static_cast<uint32_t>(passes.size()));
        }
        return ends;
    }
}
