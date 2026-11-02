#include "PassSchedule.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    PassSchedule::PassSchedule(const eastl::vector<GraphPass> &passes)
        : mPredecessors(passes.size()), mControlPredecessors(passes.size())
    {
        for (uint32_t current = 0; current < passes.size(); ++current)
        {
            mControlPredecessors[current] = passes[current].controlPredecessors;
            mPredecessors[current] = mControlPredecessors[current];
            for (uint32_t earlier = 0; earlier < current; ++earlier)
            {
                const auto &pass = passes[current];
                const auto &previous = passes[earlier];
                bool control = !pass.resources.complete || !previous.resources.complete;
                bool dependency = control;
                for (const auto &access : pass.resources.accesses)
                {
                    control = control || access.resource == nullptr;
                    for (const auto &prior : previous.resources.accesses)
                    {
                        control = control || prior.resource == nullptr;
                        dependency = dependency || access.conflicts(prior);
                    }
                }
                if (dependency || control)
                {
                    mPredecessors[current].push_back(earlier);
                }
                if (control)
                {
                    mControlPredecessors[current].push_back(earlier);
                }
            }
        }
        sort(passes);
        uint32_t lastRender = UINT32_MAX;
        for (uint32_t index : mOrder)
        {
            if (passes[index].kind == GraphPassKind::Render)
            {
                if (lastRender != UINT32_MAX)
                {
                    mPredecessors[index].push_back(lastRender);
                    mControlPredecessors[index].push_back(lastRender);
                }
                lastRender = index;
            }
        }
        sort(passes);
    }

    void PassSchedule::sort(const eastl::vector<GraphPass> &passes)
    {
        eastl::vector<eastl::vector<uint32_t>> successors(passes.size());
        for (uint32_t index = 0; index < passes.size(); ++index)
        {
            auto &dependencies = mPredecessors[index];
            eastl::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(eastl::unique(dependencies.begin(), dependencies.end()), dependencies.end());
            for (uint32_t predecessor : dependencies)
            {
                if (predecessor >= passes.size() || predecessor == index)
                {
                    throw std::invalid_argument("Pass dependency references an invalid producer.");
                }
                successors[predecessor].push_back(index);
            }
        }
        eastl::vector<uint32_t> priority(passes.size(), UINT32_MAX);
        // Establish topology before propagating observable completion priorities backwards.
        for (uint32_t phase = 0; phase < 2; ++phase)
        {
            eastl::vector<uint32_t> pending(passes.size());
            eastl::vector<bool> emitted(passes.size(), false);
            for (uint32_t index = 0; index < passes.size(); ++index)
            {
                pending[index] = static_cast<uint32_t>(mPredecessors[index].size());
            }
            mOrder.clear();
            for (uint32_t count = 0; count < passes.size(); ++count)
            {
                uint32_t next = UINT32_MAX;
                for (uint32_t index = 0; index < passes.size(); ++index)
                {
                    if (!emitted[index] && pending[index] == 0 &&
                        (next == UINT32_MAX || priority[index] < priority[next]))
                    {
                        next = index;
                    }
                }
                if (next == UINT32_MAX)
                {
                    throw std::invalid_argument("Pass resource and control dependencies contain a cycle.");
                }
                emitted[next] = true;
                mOrder.push_back(next);
                for (uint32_t successor : successors[next])
                {
                    --pending[successor];
                }
            }
            for (auto index = mOrder.rbegin(); index != mOrder.rend(); ++index)
            {
                if (passes[*index].completionNode != UINT32_MAX)
                {
                    priority[*index] = eastl::min(priority[*index], *index);
                }
                for (uint32_t predecessor : mPredecessors[*index])
                {
                    priority[predecessor] = eastl::min(priority[predecessor], priority[*index]);
                }
            }
        }
    }

    const eastl::vector<eastl::vector<uint32_t>> &PassSchedule::predecessors() const
    {
        return mPredecessors;
    }

    const eastl::vector<uint32_t> &PassSchedule::order() const
    {
        return mOrder;
    }

    eastl::vector<PassBatch> PassSchedule::batches(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const
    {
        if (maximumBatchSize == 0)
        {
            throw std::invalid_argument("GPU batch size must be nonzero.");
        }
        eastl::vector<PassBatch> result;
        eastl::vector<uint32_t> passBatch(passes.size(), UINT32_MAX);
        uint32_t begin = 0;
        for (uint32_t offset = 0; offset < mOrder.size(); ++offset)
        {
            const uint32_t index = mOrder[offset];
            bool boundary = offset - begin == maximumBatchSize || !passes[index].resources.complete;
            for (uint32_t predecessor : mControlPredecessors[index])
            {
                boundary = boundary || passBatch[predecessor] == result.size();
            }
            if (offset != begin && boundary)
            {
                result.push_back({offset, {}});
                begin = offset;
            }
            passBatch[index] = static_cast<uint32_t>(result.size());
            if (!passes[index].resources.complete || passes[index].completionNode != UINT32_MAX)
            {
                result.push_back({offset + 1, {}});
                begin = offset + 1;
            }
        }
        if (begin != mOrder.size())
        {
            result.push_back({static_cast<uint32_t>(mOrder.size()), {}});
        }
        for (uint32_t index : mOrder)
        {
            auto &waits = result[passBatch[index]].waits;
            for (uint32_t predecessor : mControlPredecessors[index])
            {
                const uint32_t previousBatch = passBatch[predecessor];
                if (previousBatch != passBatch[index] && eastl::find(waits.begin(), waits.end(), previousBatch) == waits.end())
                {
                    waits.push_back(previousBatch);
                }
            }
        }
        return result;
    }

    eastl::vector<uint32_t> PassSchedule::batchEnds(const eastl::vector<GraphPass> &passes, uint32_t maximumBatchSize) const
    {
        eastl::vector<uint32_t> result;
        for (const auto &batch : batches(passes, maximumBatchSize))
        {
            result.push_back(batch.end);
        }
        return result;
    }
}
