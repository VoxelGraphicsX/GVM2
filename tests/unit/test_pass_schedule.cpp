#include <gtest/gtest.h>
#include <EASTL/algorithm.h>
#include <GVMCore/Private/PassSchedule.hpp>
#include <GVMCore/Private/PassWeightProfiler.hpp>

namespace
{
    using namespace GVM::Core::Private;

    /// Builds a resource-complete pass for dependency analysis without requiring a GPU device.
    GraphPass makePass(GraphPassKind kind, eastl::vector<PassResourceAccess> accesses = {})
    {
        GraphPass result;
        result.kind = kind;
        result.resources = {eastl::move(accesses), true};
        return result;
    }

    /// Infers overlapping RAW, WAR and WAW edges while keeping disjoint ranges and readers independent.
    TEST(PassScheduleTests, ResolvesBufferHazardsByRange)
    {
        const int resource = 0;
        eastl::vector<GraphPass> passes;
        passes.push_back(makePass(GraphPassKind::Compute, {{&resource, 0, 16, true}}));
        passes.push_back(makePass(GraphPassKind::Compute, {{&resource, 0, 16, false}}));
        passes.push_back(makePass(GraphPassKind::Compute, {{&resource, 16, 16, true}}));
        passes.push_back(makePass(GraphPassKind::Compute, {{&resource, 0, 16, false}}));
        passes.push_back(makePass(GraphPassKind::Transfer, {{&resource, 0, 32, true}}));
        const PassSchedule schedule(passes);
        EXPECT_TRUE(schedule.predecessors()[0].empty());
        EXPECT_EQ(schedule.predecessors()[1], (eastl::vector<uint32_t>{0}));
        EXPECT_TRUE(schedule.predecessors()[2].empty());
        EXPECT_EQ(schedule.predecessors()[3], (eastl::vector<uint32_t>{0}));
        EXPECT_EQ(schedule.predecessors()[4], (eastl::vector<uint32_t>{0, 1, 2, 3}));
    }

    /// Keeps resource-independent render and compute passes unserialized while serializing shared render targets.
    TEST(PassScheduleTests, DisjointRenderTargetsStayIndependentWhileSharedTargetSerializes)
    {
        const int targetA = 0, targetB = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(reinterpret_cast<const GVM::RHI::TextureImpl *>(&targetA), true)}),
            makePass(GraphPassKind::Compute),
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(reinterpret_cast<const GVM::RHI::TextureImpl *>(&targetB), true)}),
            makePass(GraphPassKind::Compute),
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(reinterpret_cast<const GVM::RHI::TextureImpl *>(&targetA), true)})};
        const PassSchedule schedule(passes);
        EXPECT_TRUE(schedule.predecessors()[1].empty());
        EXPECT_TRUE(schedule.predecessors()[2].empty());
        EXPECT_TRUE(schedule.predecessors()[3].empty());
        EXPECT_EQ(schedule.predecessors()[4], (eastl::vector<uint32_t>{0}));
        EXPECT_EQ(schedule.batchEnds(passes, 64), (eastl::vector<uint32_t>{5}));
    }

    /// Conservatively orders opaque legacy passes and isolates their submissions.
    TEST(PassScheduleTests, OpaquePassesFenceBothSides)
    {
        eastl::vector<GraphPass> passes(4);
        for (auto &pass : passes)
        {
            pass.resources.complete = true;
        }
        passes[1].resources.complete = false;
        const PassSchedule schedule(passes);
        EXPECT_EQ(schedule.predecessors()[1], (eastl::vector<uint32_t>{0}));
        EXPECT_EQ(schedule.predecessors()[2], (eastl::vector<uint32_t>{1}));
        EXPECT_EQ(schedule.predecessors()[3], (eastl::vector<uint32_t>{1}));
        EXPECT_EQ(schedule.batchEnds(passes, 64), (eastl::vector<uint32_t>{1, 2, 4}));
    }

    /// Bounds submission size while retaining one stable serial pass order on a single queue.
    TEST(PassScheduleTests, SingleQueueBatchesRespectBudget)
    {
        eastl::vector<GraphPass> passes(7);
        for (auto &pass : passes)
        {
            pass.resources.complete = true;
        }
        const PassSchedule schedule(passes);
        EXPECT_EQ(schedule.batchEnds(passes, 3), (eastl::vector<uint32_t>{3, 6, 7}));
        EXPECT_THROW(schedule.batchEnds(passes, 0), std::invalid_argument);
        const eastl::vector<GraphPass> empty;
        EXPECT_TRUE(PassSchedule(empty).batchEnds(empty, 1).empty());
    }

    /// Tests overlap arithmetic near the largest byte offset without overflowing range ends.
    TEST(PassScheduleTests, AccessRangesDoNotOverflow)
    {
        const int resource = 0;
        const PassResourceAccess tail{&resource, UINT64_MAX - 3, 3, true};
        EXPECT_FALSE(tail.conflicts({&resource, 0, 16, true}));
        EXPECT_TRUE(tail.conflicts({&resource, UINT64_MAX - 1, 1, false}));
        EXPECT_FALSE(tail.conflicts({&resource, UINT64_MAX - 1, 0, true}));
    }
    /// Orders graphics after business dependencies even when their stable declaration IDs are reversed.
    TEST(PassScheduleTests, GraphicsChainFollowsBusinessTopology)
    {
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render), makePass(GraphPassKind::Compute), makePass(GraphPassKind::Render)};
        passes[0].controlPredecessors = {1};
        passes[1].controlPredecessors = {2};
        const PassSchedule schedule(passes);
        EXPECT_EQ(schedule.order(), (eastl::vector<uint32_t>{2, 1, 0}));
        EXPECT_EQ(schedule.predecessors()[0], (eastl::vector<uint32_t>{1}));
        EXPECT_EQ(schedule.batches(passes, 64).size(), 3u);
        passes[2].controlPredecessors = {0};
        EXPECT_THROW(PassSchedule cyclic(passes), std::invalid_argument);
    }

    /// Keeps ordinary resource hazards inside batches without adding redundant same-queue submission waits.
    TEST(PassScheduleTests, ResourceBatchesDoNotAddExecutionWaits)
    {
        const int resource = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Compute, {{&resource, 0, 16, true}}),
            makePass(GraphPassKind::Compute, {{&resource, 0, 16, false}})};
        const auto batches = PassSchedule(passes).batches(passes, 1);
        ASSERT_EQ(batches.size(), 2u);
        EXPECT_TRUE(batches[0].waits.empty());
        EXPECT_TRUE(batches[1].waits.empty());
        passes[1].controlPredecessors = {0};
        EXPECT_EQ(PassSchedule(passes).batches(passes, 64)[1].waits, (eastl::vector<uint32_t>{0}));
    }

    /// Schedules a ready observable branch before unrelated work and closes its batch immediately.
    TEST(PassScheduleTests, ReadbackDoesNotWaitForUnrelatedPasses)
    {
        const int resource = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Compute, {{&resource, 0, 16, true}}),
            makePass(GraphPassKind::Compute),
            makePass(GraphPassKind::Transfer, {{&resource, 0, 16, false}})};
        passes[2].completionNode = 5;
        const PassSchedule schedule(passes);
        EXPECT_EQ(schedule.order(), (eastl::vector<uint32_t>{0, 2, 1}));
        EXPECT_EQ(schedule.batchEnds(passes, 64), (eastl::vector<uint32_t>{2, 3}));
    }

    /// Places two independent compute producers on distinct queues and joins their writes before a graphics consumer.
    TEST(PassScheduleTests, IndependentComputeBranchesFanOutAndJoin)
    {
        const int resource = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Compute, {{&resource, 0, 4, true}}),
            makePass(GraphPassKind::Compute, {{&resource, 4, 4, true}}),
            makePass(GraphPassKind::Render, {{&resource, 0, 8, false}}),
            makePass(GraphPassKind::Render)};
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 0});
        ASSERT_EQ(schedule.order(), (eastl::vector<uint32_t>{0, 1, 2, 3}));
        ASSERT_EQ(batches.size(), 3u);
        EXPECT_EQ(batches[0].queue, 1u);
        EXPECT_EQ(batches[1].queue, 0u);
        EXPECT_EQ(batches[2].queue, 0u);
        EXPECT_EQ(batches[2].end, 4u);
        EXPECT_EQ(batches[2].waits, (eastl::vector<uint32_t>{0}));
        EXPECT_EQ(batches[2].previousOnQueue, 1u);

        PassSchedule single(passes);
        const auto serial = single.place(passes, 1, {});
        ASSERT_EQ(single.order(), schedule.order());
        ASSERT_EQ(serial.size(), 4u);
        EXPECT_EQ(serial[0].queue, serial[2].queue);
        EXPECT_EQ(serial[1].queue, serial[2].queue);
        EXPECT_TRUE(serial[2].waits.empty());
    }

    /// Keeps a short upload/fork/join/readback on one queue when the other queue gains no time after a handoff.
    TEST(PassScheduleTests, ShortBufferForkStaysInOneSubmission)
    {
        const int source = 0, left = 0, right = 0, sum = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Transfer, {{&source, 0, 16, true}}),
            makePass(GraphPassKind::Compute, {{&source, 0, 16, false}, {&left, 0, 16, true}}),
            makePass(GraphPassKind::Compute, {{&source, 0, 16, false}, {&right, 0, 16, true}}),
            makePass(GraphPassKind::Compute, {{&left, 0, 16, false}, {&right, 0, 16, false}, {&sum, 0, 16, true}}),
            makePass(GraphPassKind::Transfer, {{&sum, 0, 16, false}})};
        passes.back().completionNode = 5;
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 1});
        ASSERT_EQ(batches.size(), 1u);
        EXPECT_EQ(batches.front().queue, 0u);
        EXPECT_EQ(batches.front().end, passes.size());
        EXPECT_TRUE(batches.front().waits.empty());
    }

    /// Places independent transfer and compute/render work on distinct queues and keeps a transfer-compute-readback chain together.
    TEST(PassScheduleTests, TransferPassesRunInParallelWithGraphicsAndKeepChainLocality)
    {
        const int uploaded = 0, computed = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Transfer, {{&uploaded, 0, 16, true}}),
            makePass(GraphPassKind::Compute, {{&uploaded, 0, 16, false}, {&computed, 0, 16, true}}),
            makePass(GraphPassKind::Transfer, {{&computed, 0, 16, false}}),
            makePass(GraphPassKind::Render)};
        passes[2].completionNode = 3;
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 1});
        ASSERT_EQ(schedule.order(), (eastl::vector<uint32_t>{0, 1, 2, 3}));
        ASSERT_EQ(batches.size(), 2u);
        EXPECT_EQ(batches[0].queue, 1u);
        EXPECT_EQ(batches[0].end, 3u);
        EXPECT_TRUE(batches[0].waits.empty());
        EXPECT_EQ(batches[1].queue, 0u);
        EXPECT_EQ(batches[1].end, 4u);
        EXPECT_TRUE(batches[1].waits.empty());
    }

    /// Places independent render, compute, and transfer roots on three queues and joins their consumers.
    TEST(PassScheduleTests, ThreeQueueRootsJoinOnGraphicsAndAliasForSingleQueue)
    {
        const int computed = 0, transferred = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render),
            makePass(GraphPassKind::Compute, {{&computed, 0, 16, true}}),
            makePass(GraphPassKind::Transfer, {{&transferred, 0, 16, true}}),
            makePass(GraphPassKind::Render, {{&computed, 0, 16, false}, {&transferred, 0, 16, false}})};
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 2});
        ASSERT_EQ(schedule.order(), (eastl::vector<uint32_t>{1, 2, 0, 3}));
        ASSERT_EQ(batches.size(), 4u);
        EXPECT_EQ(batches[0].queue, 1u);
        EXPECT_EQ(batches[1].queue, 2u);
        EXPECT_EQ(batches[2].queue, 0u);
        EXPECT_EQ(batches[3].queue, 0u);
        EXPECT_EQ(batches[3].waits, (eastl::vector<uint32_t>{0, 1}));

        PassSchedule aliased(passes);
        for (const auto &batch : aliased.place(passes, 16, {1, 1}))
        {
            EXPECT_LT(batch.queue, 2u);
        }
        PassSchedule single(passes);
        for (const auto &batch : single.place(passes, 16, {}))
        {
            EXPECT_EQ(batch.queue, 0u);
        }
    }

    /// Allows a dependent compute branch to migrate when queued graphics work more than covers the handoff.
    TEST(PassScheduleTests, BusyGraphicsQueueCanOffloadDependentCompute)
    {
        const int upload = 0;
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Transfer, {{&upload, 0, 16, true}}),
            makePass(GraphPassKind::Render), makePass(GraphPassKind::Render), makePass(GraphPassKind::Render),
            makePass(GraphPassKind::Compute, {{&upload, 0, 16, false}})};
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 0});
        uint32_t begin = 0;
        for (const auto &batch : batches)
        {
            for (uint32_t offset = begin; offset < batch.end; ++offset)
            {
                if (schedule.order()[offset] == 4)
                {
                    EXPECT_EQ(batch.queue, 1u);
                    EXPECT_FALSE(batch.waits.empty());
                    return;
                }
            }
            begin = batch.end;
        }
        FAIL() << "Missing compute pass";
    }

    /// Preserves explicit batch boundaries without moving a serial graphics-compute-graphics chain off its producer queue.
    TEST(PassScheduleTests, SerialChainAvoidsQueueHandoffs)
    {
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render), makePass(GraphPassKind::Compute), makePass(GraphPassKind::Render)};
        passes[1].controlPredecessors = {0};
        passes[2].controlPredecessors = {1};
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 32, {1, 0});
        ASSERT_EQ(batches.size(), 3u);
        EXPECT_EQ(batches[0].queue, 0u);
        EXPECT_EQ(batches[1].queue, 0u);
        EXPECT_EQ(batches[2].queue, 0u);
        EXPECT_EQ(batches[1].waits, (eastl::vector<uint32_t>{0}));
        EXPECT_NE(eastl::find(batches[2].waits.begin(), batches[2].waits.end(), 1u), batches[2].waits.end());
        PassSchedule single(passes);
        const auto expected = single.batches(passes, 32);
        const auto aliased = single.place(passes, 32, {});
        ASSERT_EQ(aliased.size(), expected.size());
        for (size_t index = 0; index < expected.size(); ++index)
        {
            EXPECT_EQ(aliased[index].end, expected[index].end);
            EXPECT_EQ(aliased[index].waits, expected[index].waits);
            EXPECT_EQ(aliased[index].queue, 0u);
        }
    }

    /// Distinguishes disjoint texture mips, layers, and depth/stencil aspects without false serialization.
    TEST(PassScheduleTests, ResolvesTextureHazardsBySubresource)
    {
        const auto *tex = reinterpret_cast<const GVM::RHI::TextureImpl *>(0x1000);
        eastl::vector<GraphPass> passes{
            // 0: Write mip 0, layer 0, DepthOnly
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(tex, true, 0, 1, 0, 1, GVM::RHI::TextureAspect::DepthOnly)}),
            // 1: Write mip 1, layer 0, DepthOnly (disjoint mip)
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(tex, true, 1, 1, 0, 1, GVM::RHI::TextureAspect::DepthOnly)}),
            // 2: Write mip 0, layer 1, DepthOnly (disjoint layer)
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(tex, true, 0, 1, 1, 1, GVM::RHI::TextureAspect::DepthOnly)}),
            // 3: Write mip 0, layer 0, StencilOnly (disjoint aspect)
            makePass(GraphPassKind::Render, {PassResourceAccess::textureRange(tex, true, 0, 1, 0, 1, GVM::RHI::TextureAspect::StencilOnly)}),
            // 4: Read mip 0, layer 0, All aspects (conflicts with 0 and 3, not 1 or 2)
            makePass(GraphPassKind::Compute, {PassResourceAccess::textureRange(tex, false, 0, 1, 0, 1, GVM::RHI::TextureAspect::All)})};
        const PassSchedule schedule(passes);
        EXPECT_TRUE(schedule.predecessors()[1].empty());
        EXPECT_TRUE(schedule.predecessors()[2].empty());
        EXPECT_TRUE(schedule.predecessors()[3].empty());
        EXPECT_EQ(schedule.predecessors()[4], (eastl::vector<uint32_t>{0, 3}));
    }

    /// Coalesces interleaved work per queue lane instead of splitting batches on every alternating pass.
    TEST(PassScheduleTests, OpenBatchesPerLanePreventInterleavingFragmentation)
    {
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render),
            makePass(GraphPassKind::Compute),
            makePass(GraphPassKind::Render),
            makePass(GraphPassKind::Compute)};
        PassSchedule schedule(passes);
        const auto batches = schedule.place(passes, 16, {1, 0});
        ASSERT_EQ(batches.size(), 2u);
        EXPECT_EQ(batches[0].end, 2u);
        EXPECT_EQ(batches[1].end, 4u);
        EXPECT_NE(batches[0].queue, batches[1].queue);
    }

    /// Handles large pass graphs in O(N*A) without quadratic stalls.
    TEST(PassScheduleTests, LargeGraphBuildsAndPlacesQuickly)
    {
        constexpr uint32_t kCount = 4096;
        eastl::vector<int> resources(64, 0);
        eastl::vector<GraphPass> passes;
        passes.reserve(kCount);
        for (uint32_t i = 0; i < kCount; ++i)
        {
            const auto *res = &resources[i % resources.size()];
            passes.push_back(makePass(i % 3 == 0 ? GraphPassKind::Render : GraphPassKind::Compute, {{res, 0, 16, true}}));
        }
        PassSchedule schedule(passes);
        EXPECT_EQ(schedule.order().size(), kCount);
        const auto batches = schedule.place(passes, 64, {1, 0});
        EXPECT_FALSE(batches.empty());
        EXPECT_EQ(batches.back().end, kCount);
    }

    /// Prevents tiny dependent passes from migrating across queues under light load while offloading heavier passes.
    TEST(PassScheduleTests, PassWeightsPreventTinyOffloadWhileAllowingSufficientWorkToMigrate)
    {
        const int resource = 0;
        auto buildPasses = [&](uint16_t consumerWeight)
        {
            eastl::vector<GraphPass> passes{
                makePass(GraphPassKind::Render, {{&resource, 0, 16, true}}),
                makePass(GraphPassKind::Render),
                makePass(GraphPassKind::Compute, {{&resource, 0, 16, false}}),
            };
            passes[0].weight = 16u;
            passes[1].weight = 12u;
            passes[2].weight = consumerWeight;
            return passes;
        };

        const auto tinyPasses = buildPasses(2u);
        PassSchedule tinySchedule(tinyPasses);
        const auto tinyBatches = tinySchedule.place(tinyPasses, 16, {1, 0});
        ASSERT_EQ(tinyBatches.size(), 1u);
        EXPECT_EQ(tinyBatches[0].queue, 0u);
        EXPECT_EQ(tinyBatches[0].end, 3u);
        EXPECT_TRUE(tinyBatches[0].waits.empty());

        const auto fullPasses = buildPasses(kDefaultPassWeight);
        PassSchedule fullSchedule(fullPasses);
        const auto fullBatches = fullSchedule.place(fullPasses, 16, {1, 0});
        ASSERT_EQ(fullBatches.size(), 2u);
        ASSERT_EQ(fullSchedule.order(), (eastl::vector<uint32_t>{0u, 1u, 2u}));
        EXPECT_EQ(fullBatches[0].queue, 0u);
        EXPECT_EQ(fullBatches[0].end, 2u);
        EXPECT_EQ(fullBatches[1].queue, 1u);
        EXPECT_EQ(fullBatches[1].end, 3u);
        EXPECT_EQ(fullBatches[1].waits, (eastl::vector<uint32_t>{0u}));
    }

    /// Maps GPU nanosecond durations into bounded weights with EWMA spike clamping and overrides static tiny-dispatch weights.
    TEST(PassScheduleTests, PassWeightProfilerLearnsSmoothedWeightsAndOverridesPlacement)
    {
        EXPECT_EQ(PassWeightProfiler::hashProfileKey(GraphPassKind::Compute, ""), 0u);
        const uint64_t computeKey = PassWeightProfiler::hashProfileKey(GraphPassKind::Compute, "PCGScatter");
        const uint64_t renderKey = PassWeightProfiler::hashProfileKey(GraphPassKind::Render, "PCGScatter");
        EXPECT_NE(computeKey, 0u);
        EXPECT_NE(renderKey, 0u);
        EXPECT_NE(computeKey, renderKey);
        EXPECT_EQ(computeKey, PassWeightProfiler::hashProfileKey(GraphPassKind::Compute, "PCGScatter"));

        EXPECT_EQ(PassWeightProfiler::durationToWeight(0.0, 50'000u), 1u);
        EXPECT_EQ(PassWeightProfiler::durationToWeight(2'000.0, 50'000u), 1u);
        EXPECT_EQ(PassWeightProfiler::durationToWeight(50'000.0, 50'000u), kQueueHandoffCost);
        EXPECT_EQ(PassWeightProfiler::durationToWeight(100'000.0, 50'000u), 16u);
        EXPECT_EQ(PassWeightProfiler::durationToWeight(10'000'000.0, 50'000u), 64u);

        PassWeightProfiler profiler(nullptr, 50'000u);
        profiler.recordMeasurement(computeKey, "PCGScatter", 50'000.0);
        auto snap = profiler.snapshot();
        ASSERT_EQ(snap.size(), 1u);
        EXPECT_EQ(snap[0].label, "PCGScatter");
        EXPECT_EQ(snap[0].sampleCount, 1u);
        EXPECT_DOUBLE_EQ(snap[0].smoothedDurationNs, 50'000.0);
        EXPECT_EQ(snap[0].learnedWeight, kQueueHandoffCost);

        // One-frame extreme spike (10 ms) is clamped to 4x (200 us) before 0.75/0.25 EWMA -> 87.5 us.
        profiler.recordMeasurement(computeKey, "PCGScatter", 10'000'000.0);
        snap = profiler.snapshot();
        ASSERT_EQ(snap.size(), 1u);
        EXPECT_EQ(snap[0].sampleCount, 2u);
        EXPECT_DOUBLE_EQ(snap[0].smoothedDurationNs, 87'500.0);
        EXPECT_EQ(snap[0].learnedWeight, 14u);

        // Sustained heavy execution raises the weight of a small-thread-count compute pass so PassSchedule offloads it.
        const int resource = 0;
        auto buildTinyStaticPasses = [&]()
        {
            eastl::vector<GraphPass> passes{
                makePass(GraphPassKind::Render, {{&resource, 0, 16, true}}),
                makePass(GraphPassKind::Render),
                makePass(GraphPassKind::Compute, {{&resource, 0, 16, false}}),
            };
            passes[0].weight = 16u;
            passes[1].weight = 20u;
            passes[2].weight = 2u; // Static heuristic under-predicted a heavy 1-workgroup dispatch.
            passes[2].profileLabel = "PCGScatter";
            passes[2].profileKey = computeKey;
            return passes;
        };

        auto unlearned = buildTinyStaticPasses();
        PassSchedule unlearnedSchedule(unlearned);
        const auto unlearnedBatches = unlearnedSchedule.place(unlearned, 16, {1, 0});
        ASSERT_EQ(unlearnedBatches.size(), 1u);
        EXPECT_EQ(unlearnedBatches[0].queue, 0u);

        auto learned = buildTinyStaticPasses();
        profiler.applyLearnedWeights(learned);
        EXPECT_EQ(learned[2].weight, 14u);
        PassSchedule learnedSchedule(learned);
        const auto learnedBatches = learnedSchedule.place(learned, 16, {1, 0});
        ASSERT_EQ(learnedBatches.size(), 2u);
        EXPECT_EQ(learnedBatches[0].queue, 0u);
        EXPECT_EQ(learnedBatches[1].queue, 1u);
    }
}
