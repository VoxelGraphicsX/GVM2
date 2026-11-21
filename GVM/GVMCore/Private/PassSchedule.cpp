#include "PassSchedule.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace GVM::Core::Private
{
    namespace
    {
        bool accessCovers(const PassResourceAccess &outer, const PassResourceAccess &inner)
        {
            return outer.writes && outer.coversRange(inner);
        }

        struct ResourceAccessEntry
        {
            uint32_t pass = 0;
            PassResourceAccess access;
        };

        struct ResourceAccessHistory
        {
            eastl::vector<ResourceAccessEntry> writes;
            eastl::vector<ResourceAccessEntry> reads;
        };
    }

    PassSchedule::PassSchedule(const eastl::vector<GraphPass> &passes)
        : mPredecessors(passes.size()), mControlPredecessors(passes.size())
    {
        eastl::unordered_map<const void *, ResourceAccessHistory> history;
        uint32_t lastOpaque = UINT32_MAX;
        eastl::vector<uint32_t> sinceOpaque;
        sinceOpaque.reserve(passes.size());

        for (uint32_t current = 0; current < passes.size(); ++current)
        {
            const auto &pass = passes[current];
            mControlPredecessors[current] = pass.controlPredecessors;
            mPredecessors[current] = mControlPredecessors[current];

            bool opaque = !pass.resources.complete;
            for (const auto &access : pass.resources.accesses)
            {
                if (access.resource == nullptr)
                {
                    opaque = true;
                    break;
                }
            }

            if (lastOpaque != UINT32_MAX)
            {
                mPredecessors[current].push_back(lastOpaque);
                mControlPredecessors[current].push_back(lastOpaque);
            }

            if (opaque)
            {
                for (uint32_t earlier : sinceOpaque)
                {
                    mPredecessors[current].push_back(earlier);
                    mControlPredecessors[current].push_back(earlier);
                }
                sinceOpaque.clear();
                lastOpaque = current;
                continue;
            }

            sinceOpaque.push_back(current);
            for (const auto &access : pass.resources.accesses)
            {
                const auto it = history.find(access.resource);
                if (it == history.end())
                {
                    continue;
                }
                const auto &resourceHistory = it->second;
                for (const auto &prior : resourceHistory.writes)
                {
                    if (access.conflicts(prior.access))
                    {
                        mPredecessors[current].push_back(prior.pass);
                    }
                }
                if (access.writes)
                {
                    for (const auto &prior : resourceHistory.reads)
                    {
                        if (access.conflicts(prior.access))
                        {
                            mPredecessors[current].push_back(prior.pass);
                        }
                    }
                }
            }
            for (const auto &access : pass.resources.accesses)
            {
                auto &resourceHistory = history[access.resource];
                if (access.writes)
                {
                    // Prune writes and reads fully covered by this write to keep hot resources O(1).
                    auto writeEnd = eastl::remove_if(resourceHistory.writes.begin(), resourceHistory.writes.end(),
                        [&access](const ResourceAccessEntry &entry) { return accessCovers(access, entry.access); });
                    resourceHistory.writes.erase(writeEnd, resourceHistory.writes.end());
                    auto readEnd = eastl::remove_if(resourceHistory.reads.begin(), resourceHistory.reads.end(),
                        [&access](const ResourceAccessEntry &entry) { return accessCovers(access, entry.access); });
                    resourceHistory.reads.erase(readEnd, resourceHistory.reads.end());
                    resourceHistory.writes.push_back({current, access});
                }
                else
                {
                    resourceHistory.reads.push_back({current, access});
                }
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
            auto &control = mControlPredecessors[index];
            eastl::sort(control.begin(), control.end());
            control.erase(eastl::unique(control.begin(), control.end()), control.end());
        }
        eastl::vector<uint32_t> priority(passes.size(), UINT32_MAX);
        // Establish topology before propagating observable completion priorities backwards.
        for (uint32_t phase = 0; phase < 2; ++phase)
        {
            eastl::vector<uint32_t> pending(passes.size());
            using ReadyItem = std::pair<uint32_t, uint32_t>;
            std::priority_queue<ReadyItem, std::vector<ReadyItem>, std::greater<ReadyItem>> ready;
            for (uint32_t index = 0; index < passes.size(); ++index)
            {
                pending[index] = static_cast<uint32_t>(mPredecessors[index].size());
                if (pending[index] == 0)
                {
                    ready.push({priority[index], index});
                }
            }
            mOrder.clear();
            mOrder.reserve(passes.size());
            while (!ready.empty())
            {
                const uint32_t next = ready.top().second;
                ready.pop();
                mOrder.push_back(next);
                for (uint32_t successor : successors[next])
                {
                    if (--pending[successor] == 0)
                    {
                        ready.push({priority[successor], successor});
                    }
                }
            }
            if (mOrder.size() != passes.size())
            {
                throw std::invalid_argument("Pass resource and control dependencies contain a cycle.");
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
                const uint32_t prev = result.empty() ? UINT32_MAX : static_cast<uint32_t>(result.size() - 1);
                result.push_back({offset, {}, 0, prev});
                begin = offset;
            }
            passBatch[index] = static_cast<uint32_t>(result.size());
            if (!passes[index].resources.complete || passes[index].completionNode != UINT32_MAX)
            {
                const uint32_t prev = result.empty() ? UINT32_MAX : static_cast<uint32_t>(result.size() - 1);
                result.push_back({offset + 1, {}, 0, prev});
                begin = offset + 1;
            }
        }
        if (begin != mOrder.size())
        {
            const uint32_t prev = result.empty() ? UINT32_MAX : static_cast<uint32_t>(result.size() - 1);
            result.push_back({static_cast<uint32_t>(mOrder.size()), {}, 0, prev});
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

    eastl::vector<PassBatch> PassSchedule::place(const eastl::vector<GraphPass> &passes, uint32_t limit, PassQueues queues,
        const eastl::vector<uint32_t> &initialAvailable)
    {
        if (queues.compute == 0 && queues.transfer == 0)
        {
            return batches(passes, limit);
        }
        if (limit == 0)
        {
            throw std::invalid_argument("GPU batch size must be nonzero.");
        }
        const size_t count = passes.size();
        auto passWeight = [&](uint32_t idx) -> uint32_t
        {
            return eastl::max<uint32_t>(1u, passes[idx].weight);
        };
        eastl::vector<uint32_t> depth(count, 0), observable(count, UINT32_MAX), pending(count), finish(count);
        eastl::vector<eastl::vector<uint32_t>> successors(count);
        for (auto it = mOrder.rbegin(); it != mOrder.rend(); ++it)
        {
            const uint32_t index = *it;
            const uint32_t selfWeight = passWeight(index);
            depth[index] = eastl::max(depth[index], selfWeight);
            if (passes[index].completionNode != UINT32_MAX)
            {
                observable[index] = eastl::min(observable[index], index);
            }
            pending[index] = static_cast<uint32_t>(mPredecessors[index].size());
            for (uint32_t pred : mPredecessors[index])
            {
                successors[pred].push_back(index);
                depth[pred] = eastl::max(depth[pred], depth[index] + passWeight(pred));
                observable[pred] = eastl::min(observable[pred], observable[index]);
            }
        }
        const uint32_t lanes = eastl::max(queues.compute, queues.transfer) + 1;
        eastl::vector<uint32_t> available(lanes, 0), placement(count, UINT32_MAX);
        for (uint32_t lane = 0; lane < lanes && lane < initialAvailable.size(); ++lane)
        {
            available[lane] = initialAvailable[lane];
        }

        // Priority: smallest observable node first, then deepest critical path, then lowest pass index.
        struct ReadyCompare
        {
            const eastl::vector<uint32_t> &observable;
            const eastl::vector<uint32_t> &depth;
            bool operator()(uint32_t lhs, uint32_t rhs) const
            {
                if (observable[lhs] != observable[rhs])
                {
                    return observable[lhs] > observable[rhs];
                }
                if (depth[lhs] != depth[rhs])
                {
                    return depth[lhs] < depth[rhs];
                }
                return lhs > rhs;
            }
        };
        std::priority_queue<uint32_t, std::vector<uint32_t>, ReadyCompare> ready(ReadyCompare{observable, depth});
        for (uint32_t index = 0; index < count; ++index)
        {
            if (pending[index] == 0)
            {
                ready.push(index);
            }
        }

        eastl::vector<PassBatch> result;
        eastl::vector<uint32_t> passBatch(count, UINT32_MAX), lastBatchOnLane(lanes, UINT32_MAX);
        eastl::vector<eastl::vector<uint32_t>> openPasses(lanes), openWaits(lanes);
        eastl::vector<uint32_t> openStartSeq(lanes, UINT32_MAX);
        uint32_t seqCounter = 0;

        mOrder.clear();
        mOrder.reserve(count);

        auto flushLane = [&](uint32_t lane)
        {
            auto &passesOnLane = openPasses[lane];
            if (passesOnLane.empty())
            {
                return;
            }
            const uint32_t batchIndex = static_cast<uint32_t>(result.size());
            for (uint32_t passIndex : passesOnLane)
            {
                mOrder.push_back(passIndex);
                passBatch[passIndex] = batchIndex;
            }
            result.push_back({static_cast<uint32_t>(mOrder.size()), eastl::move(openWaits[lane]), lane, lastBatchOnLane[lane]});
            lastBatchOnLane[lane] = batchIndex;
            passesOnLane.clear();
            openWaits[lane].clear();
            openStartSeq[lane] = UINT32_MAX;
        };

        while (!ready.empty())
        {
            const uint32_t readyCount = static_cast<uint32_t>(ready.size());
            const uint32_t next = ready.top();
            ready.pop();

            const auto &pass = passes[next];
            const uint32_t selfWeight = passWeight(next);
            uint32_t preferred = 0;
            if (!pass.requiresMainQueue && pass.resources.complete && pass.kind != GraphPassKind::Render)
            {
                preferred = pass.kind == GraphPassKind::Compute ? queues.compute : queues.transfer;
            }
            uint32_t mainStart = available[0], preferredStart = available[preferred];
            bool mainNeedsWait = false, preferredNeedsWait = false;
            for (uint32_t pred : mPredecessors[next])
            {
                const bool toMain = placement[pred] != 0;
                const bool toPreferred = placement[pred] != preferred;
                mainNeedsWait = mainNeedsWait || toMain;
                preferredNeedsWait = preferredNeedsWait || toPreferred;
                // Each cross-queue handoff pays kQueueHandoffCost; arrivals overlap and only the latest gates the consumer.
                mainStart = eastl::max(mainStart, finish[pred] + (toMain ? kQueueHandoffCost : 0u));
                preferredStart = eastl::max(preferredStart, finish[pred] + (toPreferred ? kQueueHandoffCost : 0u));
            }
            // Prevent tiny passes from migrating off the main queue unless queued work covers both the handoff and potential return sync.
            const uint32_t offloadPenalty = (preferredNeedsWait && !mainNeedsWait && selfWeight < kQueueHandoffCost)
                ? (kQueueHandoffCost - selfWeight)
                : 0u;
            const uint32_t effectivePreferredStart = preferredStart + offloadPenalty;
            // On a tie favor locality. An independent root may use the auxiliary queue to leave graphics available for other ready work.
            const bool preferOnTie = (mainNeedsWait && !preferredNeedsWait) ||
                (mPredecessors[next].empty() && readyCount > 1);
            const bool usePreferred = preferred != 0 &&
                (effectivePreferredStart < mainStart || (effectivePreferredStart == mainStart && preferOnTie));
            const uint32_t lane = usePreferred ? preferred : 0;
            placement[next] = lane;
            finish[next] = (usePreferred ? preferredStart : mainStart) + selfWeight;
            available[lane] = finish[next];

            // Keep open batches per lane to avoid splitting submissions when unrelated queues interleave.
            bool flushSelf = !openPasses[lane].empty() && (!pass.resources.complete || openPasses[lane].size() >= limit);
            if (!flushSelf && !openPasses[lane].empty())
            {
                for (uint32_t pred : mPredecessors[next])
                {
                    if (placement[pred] != lane)
                    {
                        flushSelf = true;
                        break;
                    }
                }
                if (!flushSelf)
                {
                    for (uint32_t pred : mControlPredecessors[next])
                    {
                        if (placement[pred] == lane && passBatch[pred] == UINT32_MAX)
                        {
                            flushSelf = true;
                            break;
                        }
                    }
                }
            }
            eastl::vector<uint32_t> lanesToFlush;
            if (flushSelf)
            {
                lanesToFlush.push_back(lane);
            }
            for (uint32_t pred : mPredecessors[next])
            {
                const uint32_t pLane = placement[pred];
                if (pLane != lane && passBatch[pred] == UINT32_MAX &&
                    eastl::find(lanesToFlush.begin(), lanesToFlush.end(), pLane) == lanesToFlush.end())
                {
                    lanesToFlush.push_back(pLane);
                }
            }
            eastl::sort(lanesToFlush.begin(), lanesToFlush.end(), [&](uint32_t a, uint32_t b)
            {
                return openStartSeq[a] < openStartSeq[b];
            });
            for (uint32_t flushTarget : lanesToFlush)
            {
                flushLane(flushTarget);
            }
            if (openPasses[lane].empty())
            {
                openStartSeq[lane] = seqCounter++;
            }
            openPasses[lane].push_back(next);
            for (uint32_t pred : mPredecessors[next])
            {
                const uint32_t dependency = passBatch[pred];
                const bool control = eastl::find(mControlPredecessors[next].begin(), mControlPredecessors[next].end(), pred) != mControlPredecessors[next].end();
                if ((placement[pred] != lane || control) && dependency != UINT32_MAX &&
                    eastl::find(openWaits[lane].begin(), openWaits[lane].end(), dependency) == openWaits[lane].end())
                {
                    openWaits[lane].push_back(dependency);
                }
            }
            if (!pass.resources.complete || pass.completionNode != UINT32_MAX)
            {
                flushLane(lane);
            }

            for (uint32_t successor : successors[next])
            {
                if (--pending[successor] == 0)
                {
                    ready.push(successor);
                }
            }
        }

        for (;;)
        {
            uint32_t earliestLane = UINT32_MAX;
            uint32_t earliestSeq = UINT32_MAX;
            for (uint32_t l = 0; l < lanes; ++l)
            {
                if (!openPasses[l].empty() && openStartSeq[l] < earliestSeq)
                {
                    earliestSeq = openStartSeq[l];
                    earliestLane = l;
                }
            }
            if (earliestLane == UINT32_MAX)
            {
                break;
            }
            flushLane(earliestLane);
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
