#include <gtest/gtest.h>
#include <GVMCore/Private/PassSchedule.hpp>

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

    /// Keeps only graphics-order edges between otherwise resource-independent render passes.
    TEST(PassScheduleTests, GraphicsOrderDoesNotSerializeIndependentCompute)
    {
        eastl::vector<GraphPass> passes{
            makePass(GraphPassKind::Render), makePass(GraphPassKind::Compute),
            makePass(GraphPassKind::Render), makePass(GraphPassKind::Compute)};
        const PassSchedule schedule(passes);
        EXPECT_TRUE(schedule.predecessors()[1].empty());
        EXPECT_EQ(schedule.predecessors()[2], (eastl::vector<uint32_t>{0}));
        EXPECT_TRUE(schedule.predecessors()[3].empty());
        EXPECT_EQ(schedule.batchEnds(passes, 64), (eastl::vector<uint32_t>{2, 4}));
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
        EXPECT_EQ(schedule.predecessors()[0], (eastl::vector<uint32_t>{1, 2}));
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

}
