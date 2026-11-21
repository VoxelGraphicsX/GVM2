#pragma once

#include "TaskGraphDefinition.hpp"
#include "GraphPass.hpp"
#include "../Public/Execution.hpp"
#include "../Public/ITaskExecutor.hpp"
#include <EASTL/fixed_vector.h>
#include <EASTL/unordered_map.h>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>

namespace GVM::Core::Private
{
    class TaskRuntimeState;
    class GpuGraphExecution;

    /// Owns one execution's immutable plan, result slots and continuation counters.
    class TaskExecutionState final : public eastl::enable_shared_from_this<TaskExecutionState>
    {
    public:
        /// Creates pending state without publishing work to an executor.
        explicit TaskExecutionState(eastl::shared_ptr<TaskRuntimeState> runtime);
        /// Publishes the preparation job after runtime admission; rejection is terminal.
        void start(eastl::function<TaskGraphOutput()> prepare);
        /// Marks a non-admitted execution rejected without calling its builder.
        void reject();
        /// Reads lifecycle state under the execution lock.
        ExecutionStatus status() const;
        /// Returns a bounded original failure message or the terminal rejection reason.
        eastl::string error() const;
        /// Waits for terminal accounting at a host boundary, with an optional finite timeout.
        bool wait(uint64_t timeoutNanoseconds) const;
        /// Requests cancellation without interrupting a callback that has already started.
        void cancel();
        /// Copies a successful result and transfers exported allocations to explicit caller destruction.
        eastl::any result() const;
        /// Locks result access and validates terminal completion before typed construction into caller storage.
        eastl::any *beginResultClaim(bool &outSingleTransfer) const;
        /// Releases the result claim lock, committing GPU ownership transfer only when commitSingleTransfer is true.
        void endResultClaim(bool commitSingleTransfer, bool movedFrom) const noexcept;

    private:
        /// Indexes subresource read and write ranges by RHI resource identity for cross-launch-group hazard detection.
        struct LaunchGroupResourceSet
        {
            /// Stores pruned write and read subresource intervals recorded on one RHI resource.
            struct ResourceIntervals
            {
                eastl::vector<PassResourceAccess> writes;
                eastl::vector<PassResourceAccess> reads;
            };

            eastl::unordered_map<const void *, ResourceIntervals> byResource;

            /// Returns true when access has an overlapping RAW, WAR, or WAW hazard with any recorded interval.
            bool conflictsWith(const PassResourceAccess &access) const;
            /// Inserts access while pruning redundant or covered intervals on the same resource.
            void record(const PassResourceAccess &access);
        };

        /// Tracks one active GPU launch group's resource footprint across all nested subgraphs of a root execution.
        struct SharedActiveGpuGroup
        {
            uint64_t globalId = 0;
            LaunchGroupResourceSet resources;
            bool hasOpaque = false;
        };

        /// Shares task/pass/in-flight budgets and cross-subgraph GPU resource hazard tracking across one root execution tree.
        struct ExecutionBudget
        {
            std::atomic<uint32_t> taskCount{0};
            std::atomic<uint32_t> passCount{0};
            std::atomic<uint32_t> inFlightTasks{0};
            std::atomic<bool> hasWaitingStates{false};
            std::mutex coordinatorMutex;
            uint64_t nextGlobalGpuGroupId = 1;
            eastl::vector<SharedActiveGpuGroup> activeGpuGroups;
            eastl::vector<eastl::weak_ptr<TaskExecutionState>> waitingStates;
        };

        struct ActiveChildExecution
        {
            uint64_t id = 0;
            uint32_t nodeIndex = 0;
            eastl::shared_ptr<TaskExecutionState> execution;
        };

        struct PendingGpuFragment
        {
            uint32_t nodeIndex = 0;
            RenderNodeFragment fragment;
            bool deferred = false;
        };

        struct ActiveGpuLaunchGroup
        {
            uint64_t id = 0;
            uint64_t globalId = 0;
            eastl::shared_ptr<GpuGraphExecution> execution;
            eastl::vector<uint32_t> deferredNodes;
            LaunchGroupResourceSet resources;
            eastl::fixed_vector<uint64_t, 8, true> descendantBits;
            eastl::vector<bool> descendants;
            bool hasOpaque = false;
        };

        struct PreparedGpuLaunchGroup
        {
            uint64_t id = 0;
            eastl::vector<RenderNodeFragment> fragments;
        };

        /// Carries one completed child subgraph's terminal payload to its parent without recursive stack frames.
        struct ChildCompletionStep
        {
            eastl::shared_ptr<TaskExecutionState> parent;
            uint32_t parentNodeIndex = 0;
            uint64_t childId = 0;
            ExecutionStatus status = ExecutionStatus::Pending;
            eastl::any finalResult;
            std::exception_ptr failure;
            const char *diagnostic = "";
            eastl::vector<eastl::shared_ptr<GraphResourceAllocation>> exportedAllocations;
        };

        TaskExecutionState(
            eastl::shared_ptr<TaskRuntimeState> runtime,
            eastl::shared_ptr<ExecutionBudget> budget,
            eastl::shared_ptr<TaskExecutionState> parent,
            uint32_t parentNodeIndex,
            uint64_t childId);

        /// Validates and installs a TaskGraphOutput plan while enforcing the shared execution task budget.
        void initializePlan(const TaskGraphOutput &output);
        /// Starts ready work on an initialized child subgraph execution without blocking the parent worker.
        void startChild() noexcept;
        /// Applies one completed child subgraph result to this parent state and reports whether this state became terminal.
        void applyChildCompletionStep(ChildCompletionStep &&step, bool &outTerminal) noexcept;
        /// Cleans up terminal execution state; populates outStep and returns true when a parent must be notified iteratively.
        bool prepareTerminalStep(ChildCompletionStep &outStep) noexcept;
        /// Cancels this state locally under mMutex and collects direct active GPU groups and child subgraphs.
        void cancelSingleNode(
            eastl::vector<eastl::shared_ptr<GpuGraphExecution>> &outGpus,
            eastl::vector<eastl::shared_ptr<TaskExecutionState>> &outChildren,
            bool &outTerminal) noexcept;
        /// Iteratively cancels a list of child subgraph trees without recursion.
        static void cancelSubgraphTrees(eastl::vector<eastl::shared_ptr<TaskExecutionState>> roots) noexcept;
        /// Reserves up to requested slots from the shared root execution in-flight task budget.
        uint32_t tryAcquireSharedInFlightSlots(uint32_t requested, uint32_t maxInFlight) noexcept;
        /// Registers this state in the shared budget waiter list under coordinatorMutex and re-checks slot availability.
        bool registerWaitingAndCheckInFlightAvailable(uint32_t maxInFlight);
        /// Registers this state in the shared budget waiter list while coordinatorMutex is already held.
        void registerBudgetWaiterLocked();
        /// Releases one shared in-flight task slot and wakes any throttled states in the execution tree.
        void releaseSharedInFlightSlotAndWake() noexcept;
        /// Removes one completed global GPU group from the shared budget and wakes any throttled states.
        void releaseSharedGpuGroupAndWake(uint64_t globalId) noexcept;
        /// Drains and dispatches states registered in mBudget->waitingStates outside locks.
        void wakeBudgetWaiters() noexcept;
        /// Returns true if any unsubmitted or in-flight parent GPU fragment reaches nodeIndex.
        bool hasUnfinishedGpuAncestorLocked(uint32_t nodeIndex) const;
        /// Builds an isolated plan, allocates stable slots, and makes dependency-free jobs ready.
        void prepare(eastl::function<TaskGraphOutput()> builder) noexcept;
        /// Publishes ready jobs up to the admission budget and launches non-conflicting GPU launch groups.
        void dispatchReady() noexcept;
        /// Executes an accepted node outside locks, capturing all user exceptions.
        void evaluate(uint32_t index, bool claimedGpuBuild) noexcept;
        /// Completes one accepted job and publishes its successors exactly once.
        void finish(
            uint32_t index,
            eastl::any value,
            std::exception_ptr failure,
            bool deferred = false,
            bool completedGpuBuild = false) noexcept;
        /// Collects non-conflicting pending GPU fragments into an active launch group under mMutex.
        bool tryCollectGpuLaunchGroupLocked(PreparedGpuLaunchGroup &outLaunchGroup, bool eagerReadbackOnly);
        /// Reports whether strict DAG reachability from sourceNode to targetNode holds using precomputed bitsets.
        bool isStrictDescendantBitsetLocked(uint32_t sourceNode, uint32_t targetNode) const noexcept;
        /// Unions strict DAG descendants of rootNode into targetBits using precomputed bitsets.
        void mergeDescendantBitsetLocked(uint32_t rootNode, eastl::fixed_vector<uint64_t, 8, true> &targetBits) const noexcept;
        /// Marks all DAG descendants reachable from rootNode in mSuccessors when bitset precomputation is bypassed.
        void markDescendantsLocked(uint32_t rootNode, eastl::vector<bool> &visited) const;
        /// Marks all DAG ancestors reaching rootNode in mTasks[i].predecessors when bitset precomputation is bypassed.
        void markAncestorsLocked(uint32_t rootNode, eastl::vector<bool> &visited) const;
        /// Compiles and launches one collected GPU launch group outside mMutex.
        void beginGpu(PreparedGpuLaunchGroup launchGroup) noexcept;
        /// Makes one completed readback ready for the same bounded dispatch path as ordinary CPU work.
        void gpuValueReady(uint32_t index) noexcept;
        /// Schedules launch group accounting after GPU completion or failure.
        void gpuFinished(uint64_t launchGroupId, std::exception_ptr failure) noexcept;
        /// Finalizes launch group accounting after an active GPU launch group drains and unlocks dependent groups.
        void finishGpu(uint64_t launchGroupId, std::exception_ptr failure) noexcept;
        /// Publishes one completed value with the execution lock held.
        void publishLocked(uint32_t index, eastl::any value);
        /// Records a scheduling failure for a job that the executor did not accept.
        void scheduleFailed(uint32_t index, ScheduleResult result, bool claimedGpuBuild = false) noexcept;
        /// Selects a terminal outcome only when every accepted job has stopped using its inputs.
        bool finishLocked() noexcept;
        /// Releases inputs and admission before publishing the terminal outcome to host waiters.
        void notifyTerminal() noexcept;
        /// Tests lifecycle state with mMutex held.
        bool terminalLocked() const noexcept;

        static constexpr uint32_t Preparation = UINT32_MAX;
        eastl::shared_ptr<TaskRuntimeState> mRuntime;
        eastl::shared_ptr<ExecutionBudget> mBudget;
        eastl::shared_ptr<TaskExecutionState> mParent;
        uint32_t mParentNodeIndex = 0;
        uint64_t mChildId = 0;
        bool mOwnsRuntimeAdmission = true;
        mutable std::mutex mMutex;
        mutable std::condition_variable mCompleted;
        ExecutionStatus mStatus = ExecutionStatus::Pending;
        ExecutionStatus mStopStatus = ExecutionStatus::Pending;
        std::exception_ptr mFailure;
        const char *mDiagnostic = "";
        eastl::vector<TaskDefinition> mTasks;
        eastl::vector<eastl::vector<uint32_t>> mSuccessors;
        eastl::vector<uint64_t> mDescendantBits;
        size_t mReachabilityWords = 0;
        eastl::vector<uint32_t> mPending;
        eastl::vector<uint32_t> mReady;
        eastl::vector<uint32_t> mBlockedTaskSubgraphs;
        TaskResults mResults;
        eastl::vector<PendingGpuFragment> mPendingGpuFragments;
        mutable eastl::vector<eastl::shared_ptr<GraphResourceAllocation>> mAllocations;
        eastl::vector<eastl::function<eastl::any()>> mGpuResolvers;
        eastl::vector<bool> mGpuValuesReady;
        eastl::vector<ActiveGpuLaunchGroup> mActiveGpuLaunchGroups;
        eastl::vector<ActiveChildExecution> mActiveChildExecutions;
        uint64_t mNextGpuLaunchGroupId = 1;
        uint64_t mNextChildExecutionId = 1;
        mutable eastl::any mFinalResult;
        eastl::function<eastl::any(const eastl::any &)> mResolveOutput;
        eastl::function<eastl::vector<const void *>(const eastl::any &)> mOutputResources;
        eastl::vector<const void *> mExportedResources;
        uint32_t mOutput = 0;
        uint32_t mInFlight = 0;
        uint32_t mBuildingGpuInFlight = 0;
        uint32_t mReadyGpuBuilds = 0;
        uint32_t mRemaining = 0;
        size_t mReadyCursor = 0;
        bool mPrepared = false;
        bool mDispatching = false;
        bool mTerminalPublished = false;
        bool mHasExportedResources = false;
        mutable bool mExportedResourcesClaimed = false;
    };
}
