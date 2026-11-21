#include "PassWeightProfiler.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>
#include <utility>

namespace GVM::Core::Private
{
    PassProfileSession::PassProfileSession(PassProfileSession &&other) noexcept
        : mArenaIndex(std::exchange(other.mArenaIndex, UINT32_MAX))
        , mPassWrites(eastl::move(other.mPassWrites))
        , mQueueQuerySets(eastl::move(other.mQueueQuerySets))
        , mQueueResolveBuffers(eastl::move(other.mQueueResolveBuffers))
        , mQueueReadbackBuffers(eastl::move(other.mQueueReadbackBuffers))
        , mQueueQueryCounts(other.mQueueQueryCounts)
        , mQueueSamples(eastl::move(other.mQueueSamples))
    {
        other.mQueueQueryCounts.fill(0u);
    }

    PassProfileSession &PassProfileSession::operator=(PassProfileSession &&other) noexcept
    {
        if (this != &other)
        {
            mArenaIndex = std::exchange(other.mArenaIndex, UINT32_MAX);
            mPassWrites = eastl::move(other.mPassWrites);
            mQueueQuerySets = eastl::move(other.mQueueQuerySets);
            mQueueResolveBuffers = eastl::move(other.mQueueResolveBuffers);
            mQueueReadbackBuffers = eastl::move(other.mQueueReadbackBuffers);
            mQueueQueryCounts = other.mQueueQueryCounts;
            mQueueSamples = eastl::move(other.mQueueSamples);
            other.mQueueQueryCounts.fill(0u);
        }
        return *this;
    }

    const RHI::PassTimestampWrites *PassProfileSession::timestampWritesFor(uint32_t passIndex) const noexcept
    {
        if (!active() || passIndex >= mPassWrites.size())
        {
            return nullptr;
        }
        const auto &writes = mPassWrites[passIndex];
        return writes.querySet != nullptr ? &writes : nullptr;
    }

    bool PassProfileSession::hasBatchQueries(uint32_t beginPass, uint32_t endPass) const noexcept
    {
        if (!active())
        {
            return false;
        }
        const uint32_t clampedEnd = eastl::min<uint32_t>(endPass, static_cast<uint32_t>(mPassWrites.size()));
        for (uint32_t p = beginPass; p < clampedEnd; ++p)
        {
            if (mPassWrites[p].querySet != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void PassProfileSession::encodeBatchResolve(
        uint32_t queueIndex,
        uint32_t beginPass,
        uint32_t endPass,
        RHI::CommandEncoder encoder) const
    {
        if (!active() || queueIndex >= mQueueQueryCounts.size())
        {
            return;
        }
        if (mQueueQuerySets[queueIndex] == nullptr ||
            mQueueResolveBuffers[queueIndex].isNull() ||
            mQueueReadbackBuffers[queueIndex].isNull())
        {
            return;
        }
        const uint32_t clampedEnd = eastl::min<uint32_t>(endPass, static_cast<uint32_t>(mPassWrites.size()));
        uint32_t firstQuery = UINT32_MAX;
        uint32_t lastQueryExclusive = 0u;
        for (uint32_t p = beginPass; p < clampedEnd; ++p)
        {
            const auto &writes = mPassWrites[p];
            if (writes.querySet == nullptr)
            {
                continue;
            }
            firstQuery = eastl::min(firstQuery, writes.beginningOfPassWriteIndex);
            lastQueryExclusive = eastl::max(lastQueryExclusive, writes.endOfPassWriteIndex + 1u);
        }
        if (firstQuery >= lastQueryExclusive)
        {
            return;
        }
        const uint32_t queryCount = lastQueryExclusive - firstQuery;
        const uint64_t byteOffset = static_cast<uint64_t>(firstQuery) * sizeof(uint64_t);
        const uint64_t byteCount = static_cast<uint64_t>(queryCount) * sizeof(uint64_t);
        const RHI::BufferRange resolveRange(mQueueResolveBuffers[queueIndex], byteOffset, byteCount);
        const RHI::BufferRange readbackRange(mQueueReadbackBuffers[queueIndex], byteOffset, byteCount);
        encoder->resolveQuerySet(mQueueQuerySets[queueIndex], firstQuery, queryCount, resolveRange);
        auto blit = encoder->beginBlitPass({.label = "PassWeightProfilerReadback"});
        try
        {
            blit->copyBufferToBuffer(resolveRange, readbackRange);
        }
        catch (...)
        {
            blit->end();
            throw;
        }
        blit->end();
    }

    PassWeightProfiler::PassWeightProfiler(eastl::shared_ptr<GraphDevice> device, uint32_t queueHandoffThresholdNs)
        : mDevice(eastl::move(device))
        , mQueueHandoffThresholdNs(eastl::max<uint32_t>(1000u, queueHandoffThresholdNs))
    {
        for (uint32_t i = 0u; i < kArenaPoolCapacity; ++i)
        {
            mFreeArenas.push_back(kArenaPoolCapacity - 1u - i);
        }
    }

    uint64_t PassWeightProfiler::hashProfileKey(GraphPassKind kind, eastl::string_view label) noexcept
    {
        if (label.empty())
        {
            return 0u;
        }
        constexpr uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;
        uint64_t hash = kFnvOffset;
        hash ^= static_cast<uint64_t>(kind);
        hash *= kFnvPrime;
        hash ^= static_cast<uint64_t>(':');
        hash *= kFnvPrime;
        for (char ch : label)
        {
            hash ^= static_cast<uint8_t>(ch);
            hash *= kFnvPrime;
        }
        return hash == 0u ? 1u : hash;
    }

    uint16_t PassWeightProfiler::durationToWeight(double smoothedDurationNs, uint32_t queueHandoffThresholdNs) noexcept
    {
        if (!(smoothedDurationNs > 0.0))
        {
            return 1u;
        }
        const uint32_t thresholdNs = eastl::max<uint32_t>(1000u, queueHandoffThresholdNs);
        const double unitNs = static_cast<double>(thresholdNs) / static_cast<double>(kQueueHandoffCost);
        const double scaled = smoothedDurationNs / unitNs;
        if (scaled >= 64.0)
        {
            return 64u;
        }
        const uint32_t rounded = static_cast<uint32_t>(scaled + 0.5);
        return static_cast<uint16_t>(eastl::clamp<uint32_t>(rounded, 1u, 64u));
    }

    void PassWeightProfiler::applyLearnedWeights(eastl::vector<GraphPass> &passes) const
    {
        std::lock_guard lock(mTableMutex);
        if (mEntries.empty())
        {
            return;
        }
        for (auto &pass : passes)
        {
            if (pass.profileKey == 0u)
            {
                continue;
            }
            const auto found = mEntries.find(pass.profileKey);
            if (found != mEntries.end())
            {
                pass.weight = found->second.learnedWeight;
            }
        }
    }

    bool PassWeightProfiler::ensureQueueArenaInitialized(uint32_t arenaIndex, uint32_t queueIndex) noexcept
    {
        if (mDevice == nullptr || arenaIndex >= kArenaPoolCapacity || queueIndex >= 3u)
        {
            return false;
        }
        auto &qa = mArenas[arenaIndex].queues[queueIndex];
        if (qa.initialized)
        {
            return true;
        }
        if (qa.unusable)
        {
            return false;
        }
        try
        {
            auto *queue = mDevice->queue(queueIndex);
            if (queue == nullptr)
            {
                qa.unusable = true;
                return false;
            }
            const auto support = queue->getTimestampQuerySupport();
            if (support.supported == RHI::False || support.passTimestampWritesSupported == RHI::False ||
                !(support.tickPeriodNs > 0.0))
            {
                qa.unusable = true;
                return false;
            }
            RHI::QuerySet querySet;
            {
                auto lock = mDevice->lockResources();
                RHI::QuerySetDescriptor queryDesc = {};
                queryDesc.label = "PassWeightProfiler.queries";
                queryDesc.type = RHI::QueryType::Timestamp;
                queryDesc.count = kMaxQueriesPerQueue;
                querySet = mDevice->device().createQuerySet(queryDesc);
            }
            const uint64_t bufferBytes = static_cast<uint64_t>(kMaxQueriesPerQueue) * sizeof(uint64_t);
            auto resolveAlloc = eastl::make_unique<GraphResourceAllocation>(mDevice, RHI::BufferDescriptor{
                .label = "PassWeightProfiler.resolve",
                .usage = RHI::BufferUsage::QueryResolve | RHI::BufferUsage::CopySrc,
                .size = bufferBytes,
            });
            auto readbackAlloc = eastl::make_unique<GraphResourceAllocation>(mDevice, RHI::BufferDescriptor{
                .label = "PassWeightProfiler.readback",
                .usage = RHI::BufferUsage::MapRead | RHI::BufferUsage::CopyDst,
                .size = bufferBytes,
            });
            qa.support = support;
            qa.querySet = eastl::move(querySet);
            qa.resolveBuffer = eastl::move(resolveAlloc);
            qa.readbackBuffer = eastl::move(readbackAlloc);
            qa.initialized = true;
            return true;
        }
        catch (...)
        {
            qa.unusable = true;
            return false;
        }
    }

    void PassWeightProfiler::releaseArenaIndex(uint32_t arenaIndex) noexcept
    {
        if (arenaIndex >= kArenaPoolCapacity)
        {
            return;
        }
        std::lock_guard lock(mPoolMutex);
        mFreeArenas.push_back(arenaIndex);
    }

    PassProfileSession PassWeightProfiler::beginSession(
        const eastl::vector<GraphPass> &orderedPasses, const eastl::vector<uint32_t> &passQueues)
    {
        if (mDevice == nullptr || orderedPasses.empty() || orderedPasses.size() != passQueues.size())
        {
            return {};
        }

        bool hasCandidatePass = false;
        for (size_t i = 0; i < orderedPasses.size(); ++i)
        {
            const auto &pass = orderedPasses[i];
            if (pass.profileKey != 0u &&
                !pass.hasExplicitTimestampWrites &&
                (pass.kind == GraphPassKind::Compute || pass.kind == GraphPassKind::Render) &&
                passQueues[i] < 3u)
            {
                hasCandidatePass = true;
                break;
            }
        }
        if (!hasCandidatePass)
        {
            return {};
        }

        uint32_t arenaIndex = UINT32_MAX;
        {
            std::lock_guard lock(mPoolMutex);
            if (mFreeArenas.empty())
            {
                return {};
            }
            arenaIndex = mFreeArenas.back();
            mFreeArenas.pop_back();
        }

        PassProfileSession session;
        session.mArenaIndex = arenaIndex;
        try
        {
            session.mPassWrites.resize(orderedPasses.size());

            bool anySampled = false;
            for (size_t i = 0; i < orderedPasses.size(); ++i)
            {
                const auto &pass = orderedPasses[i];
                const uint32_t q = passQueues[i];
                if (pass.profileKey == 0u ||
                    pass.hasExplicitTimestampWrites ||
                    (pass.kind != GraphPassKind::Compute && pass.kind != GraphPassKind::Render) ||
                    q >= 3u)
                {
                    continue;
                }
                if (session.mQueueQueryCounts[q] + 2u > kMaxQueriesPerQueue)
                {
                    continue;
                }
                if (!ensureQueueArenaInitialized(arenaIndex, q))
                {
                    continue;
                }
                const auto &qa = mArenas[arenaIndex].queues[q];
                const uint32_t beginQuery = session.mQueueQueryCounts[q]++;
                const uint32_t endQuery = session.mQueueQueryCounts[q]++;
                session.mQueueQuerySets[q] = qa.querySet;
                session.mQueueResolveBuffers[q] = qa.resolveBuffer->buffer();
                session.mQueueReadbackBuffers[q] = qa.readbackBuffer->buffer();
                session.mPassWrites[i] = RHI::PassTimestampWrites{
                    .querySet = qa.querySet,
                    .beginningOfPassWriteIndex = beginQuery,
                    .endOfPassWriteIndex = endQuery,
                };
                session.mQueueSamples[q].push_back(PassProfileSession::SampledPassRef{
                    .profileKey = pass.profileKey,
                    .label = pass.profileLabel,
                    .beginQuery = beginQuery,
                    .endQuery = endQuery,
                });
                anySampled = true;
            }

            if (!anySampled)
            {
                cancelSession(eastl::move(session));
                return {};
            }
            return session;
        }
        catch (...)
        {
            cancelSession(eastl::move(session));
            throw;
        }
    }

    void PassWeightProfiler::recordMeasurement(uint64_t profileKey, eastl::string_view label, double durationNs)
    {
        if (profileKey == 0u || !(durationNs >= 0.0) || durationNs > 10.0e9)
        {
            return;
        }
        std::lock_guard lock(mTableMutex);
        auto &entry = mEntries[profileKey];
        if (entry.sampleCount == 0u)
        {
            entry.label.assign(label.data(), label.size());
            entry.smoothedDurationNs = durationNs;
            entry.sampleCount = 1u;
        }
        else
        {
            if (entry.label.empty() && !label.empty())
            {
                entry.label.assign(label.data(), label.size());
            }
            const double spikeCeilingNs = eastl::max(
                entry.smoothedDurationNs * 4.0,
                static_cast<double>(mQueueHandoffThresholdNs) * 2.0);
            const double clampedNs = eastl::min(durationNs, spikeCeilingNs);
            entry.smoothedDurationNs = 0.75 * entry.smoothedDurationNs + 0.25 * clampedNs;
            ++entry.sampleCount;
        }
        entry.learnedWeight = durationToWeight(entry.smoothedDurationNs, mQueueHandoffThresholdNs);
    }

    void PassWeightProfiler::harvestSession(PassProfileSession &&session) noexcept
    {
        const uint32_t arenaIndex = std::exchange(session.mArenaIndex, UINT32_MAX);
        if (arenaIndex >= kArenaPoolCapacity)
        {
            return;
        }
        try
        {
            std::array<uint64_t, kMaxQueriesPerQueue> rawTicks{};
            for (uint32_t q = 0u; q < 3u; ++q)
            {
                const uint32_t queryCount = session.mQueueQueryCounts[q];
                if (queryCount == 0u)
                {
                    continue;
                }
                const auto &qa = mArenas[arenaIndex].queues[q];
                if (!qa.initialized || qa.readbackBuffer == nullptr)
                {
                    continue;
                }
                const uint64_t byteCount = static_cast<uint64_t>(queryCount) * sizeof(uint64_t);
                qa.readbackBuffer->read(0u, rawTicks.data(), byteCount);
                for (const auto &sample : session.mQueueSamples[q])
                {
                    if (sample.beginQuery >= queryCount || sample.endQuery >= queryCount)
                    {
                        continue;
                    }
                    const uint64_t beginRaw = rawTicks[sample.beginQuery];
                    const uint64_t endRaw = rawTicks[sample.endQuery];
                    if (endRaw == beginRaw)
                    {
                        // Drivers that emulate vkCmdWriteTimestamp2 at encoder stage boundaries (e.g. MoltenVK compute)
                        // can collapse begin/end ticks to the same value; preserve static pass weights instead of learning 0 ns.
                        continue;
                    }
                    if ((qa.support.validBits == 0u || qa.support.validBits >= 64u) && endRaw < beginRaw)
                    {
                        continue;
                    }
                    const double durationNs = RHI::calculateTimestampDurationNs(qa.support, beginRaw, endRaw);
                    if (!(durationNs > 0.0))
                    {
                        continue;
                    }
                    recordMeasurement(sample.profileKey, sample.label, durationNs);
                }
            }
        }
        catch (...)
        {
            // Profiling is strictly advisory and never fails a completed graph execution.
        }
        releaseArenaIndex(arenaIndex);
    }

    void PassWeightProfiler::cancelSession(PassProfileSession &&session) noexcept
    {
        const uint32_t arenaIndex = std::exchange(session.mArenaIndex, UINT32_MAX);
        if (arenaIndex < kArenaPoolCapacity)
        {
            releaseArenaIndex(arenaIndex);
        }
    }

    eastl::vector<PassProfileFeedbackEntry> PassWeightProfiler::snapshot() const
    {
        eastl::vector<PassProfileFeedbackEntry> out;
        {
            std::lock_guard lock(mTableMutex);
            out.reserve(mEntries.size());
            for (const auto &kv : mEntries)
            {
                out.push_back(PassProfileFeedbackEntry{
                    .label = kv.second.label,
                    .profileKey = kv.first,
                    .smoothedDurationNs = kv.second.smoothedDurationNs,
                    .sampleCount = kv.second.sampleCount,
                    .learnedWeight = kv.second.learnedWeight,
                });
            }
        }
        eastl::sort(out.begin(), out.end(), [](const PassProfileFeedbackEntry &a, const PassProfileFeedbackEntry &b)
        {
            if (a.label != b.label)
            {
                return a.label < b.label;
            }
            return a.profileKey < b.profileKey;
        });
        return out;
    }
}
