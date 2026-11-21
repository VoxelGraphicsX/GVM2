#pragma once

#include "../Public/TaskRuntime.hpp"
#include "GraphDevice.hpp"
#include "GraphPass.hpp"
#include "GraphResourceAllocation.hpp"
#include <EASTL/fixed_vector.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <array>
#include <mutex>

namespace GVM::Core::Private
{
    class PassWeightProfiler;

    /// Holds borrowed query-arena bindings for one in-flight GpuGraphExecution without blocking on pool exhaustion.
    class PassProfileSession final
    {
    public:
        struct SampledPassRef
        {
            uint64_t profileKey = 0;
            eastl::string label;
            uint32_t beginQuery = 0;
            uint32_t endQuery = 0;
        };

        PassProfileSession() = default;
        PassProfileSession(const PassProfileSession &) = delete;
        PassProfileSession &operator=(const PassProfileSession &) = delete;
        PassProfileSession(PassProfileSession &&other) noexcept;
        PassProfileSession &operator=(PassProfileSession &&other) noexcept;

        bool active() const noexcept { return mArenaIndex != UINT32_MAX; }
        const RHI::PassTimestampWrites *timestampWritesFor(uint32_t passIndex) const noexcept;
        bool hasBatchQueries(uint32_t beginPass, uint32_t endPass) const noexcept;
        void encodeBatchResolve(uint32_t queueIndex, uint32_t beginPass, uint32_t endPass, RHI::CommandEncoder encoder) const;

    private:
        friend class PassWeightProfiler;

        uint32_t mArenaIndex = UINT32_MAX;
        eastl::vector<RHI::PassTimestampWrites> mPassWrites;
        std::array<RHI::QuerySet, 3> mQueueQuerySets{};
        std::array<RHI::Buffer, 3> mQueueResolveBuffers{};
        std::array<RHI::Buffer, 3> mQueueReadbackBuffers{};
        std::array<uint32_t, 3> mQueueQueryCounts{};
        std::array<eastl::vector<SampledPassRef>, 3> mQueueSamples{};
    };

    /// Learns smoothed GPU pass durations across frames and converts them into bounded PassSchedule weights.
    class PassWeightProfiler final
    {
    public:
        static constexpr uint32_t kArenaPoolCapacity = 4u;
        static constexpr uint32_t kMaxProfiledPassesPerQueue = 128u;
        static constexpr uint32_t kMaxQueriesPerQueue = kMaxProfiledPassesPerQueue * 2u;

        explicit PassWeightProfiler(eastl::shared_ptr<GraphDevice> device, uint32_t queueHandoffThresholdNs = 50000u);
        ~PassWeightProfiler() = default;

        PassWeightProfiler(const PassWeightProfiler &) = delete;
        PassWeightProfiler &operator=(const PassWeightProfiler &) = delete;

        /// Computes a stable non-zero 64-bit key for a labeled compute or render pass; empty labels return zero.
        static uint64_t hashProfileKey(GraphPassKind kind, eastl::string_view label) noexcept;

        /// Maps a smoothed GPU duration in nanoseconds into PassSchedule's [1, 64] weight scale.
        static uint16_t durationToWeight(double smoothedDurationNs, uint32_t queueHandoffThresholdNs) noexcept;

        /// Overwrites static pass weights when prior hardware measurements exist for matching profile keys.
        void applyLearnedWeights(eastl::vector<GraphPass> &passes) const;

        /// Leases an idle query arena for post-schedule passes, or returns an inactive session if none is free.
        PassProfileSession beginSession(const eastl::vector<GraphPass> &orderedPasses, const eastl::vector<uint32_t> &passQueues);

        /// Harvests already completed readback buffers without waiting and returns the leased arena to the pool.
        void harvestSession(PassProfileSession &&session) noexcept;

        /// Releases a leased arena without updating learned weights when an execution fails or cancels.
        void cancelSession(PassProfileSession &&session) noexcept;

        /// Updates the EWMA table directly with one measured duration in nanoseconds (exposed for deterministic unit tests).
        void recordMeasurement(uint64_t profileKey, eastl::string_view label, double durationNs);

        /// Returns a deterministic snapshot of all learned pass durations and weights.
        eastl::vector<PassProfileFeedbackEntry> snapshot() const;

    private:
        struct LearnedEntry
        {
            eastl::string label;
            double smoothedDurationNs = 0.0;
            uint32_t sampleCount = 0;
            uint16_t learnedWeight = kDefaultPassWeight;
        };

        struct QueueArena
        {
            RHI::TimestampQuerySupport support{};
            RHI::QuerySet querySet;
            eastl::unique_ptr<GraphResourceAllocation> resolveBuffer;
            eastl::unique_ptr<GraphResourceAllocation> readbackBuffer;
            bool initialized = false;
            bool unusable = false;
        };

        struct Arena
        {
            std::array<QueueArena, 3> queues{};
        };

        bool ensureQueueArenaInitialized(uint32_t arenaIndex, uint32_t queueIndex) noexcept;
        void releaseArenaIndex(uint32_t arenaIndex) noexcept;

        eastl::shared_ptr<GraphDevice> mDevice;
        uint32_t mQueueHandoffThresholdNs = 50000u;

        mutable std::mutex mTableMutex;
        eastl::unordered_map<uint64_t, LearnedEntry> mEntries;

        std::mutex mPoolMutex;
        std::array<Arena, kArenaPoolCapacity> mArenas{};
        eastl::fixed_vector<uint32_t, kArenaPoolCapacity, false> mFreeArenas;
    };
}
