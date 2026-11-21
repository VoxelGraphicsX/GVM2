#include "GraphDevice.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/fixed_vector.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    GraphDevice::GraphDevice(RHI::DeviceImpl &device)
        : mDevice(device)
    {
        mQueues.push_back(device.getQueue({.prefer = RHI::QueuePreference::Graphics}));
        if (mQueues.front() == nullptr)
        {
            throw std::invalid_argument("GPU graph runtime requires a graphics-capable queue.");
        }
        for (const auto preference : {RHI::QueuePreference::Compute, RHI::QueuePreference::Transfer})
        {
            auto *candidate = device.getQueue({.prefer = preference});
            if (candidate == nullptr)
            {
                throw std::invalid_argument("GPU graph requires a compatible queue for each pass kind.");
            }
            const auto found = eastl::find(mQueues.begin(), mQueues.end(), candidate);
            const auto index = static_cast<uint32_t>(found - mQueues.begin());
            if (found == mQueues.end())
            {
                mQueues.push_back(candidate);
            }
            (preference == RHI::QueuePreference::Compute ? mCandidates.compute : mCandidates.transfer) = index;
        }
        for (size_t queueIndex = 0; queueIndex < mQueues.size() && queueIndex < mEncoderBudgets.size(); ++queueIndex)
        {
            const uint32_t rawCap = mQueues[queueIndex]->getMaxUnsubmittedCommandEncoderCount();
            const uint32_t capacity = eastl::max(rawCap, kBatchSubmitEncoderOverhead + 2u);
            const uint32_t maxPasses = capacity > 13u
                ? eastl::min(192u, capacity - 9u)
                : eastl::max(1u, capacity - kBatchSubmitEncoderOverhead - 1u);
            mEncoderBudgets[queueIndex].capacity = capacity;
            mEncoderBudgets[queueIndex].maxBatchPasses = maxPasses;
            mEncoderBudgets[queueIndex].maxRootReservation = maxPasses + kBatchSubmitEncoderOverhead;
        }
    }

    GraphDevice::~GraphDevice()
    {
        std::thread toJoin;
        {
            std::lock_guard lock(mBridgeMutex);
            mBridgeStopping = true;
            if (mBridgeInKernelWait && mHostTimeline.isValid())
            {
                (void)mHostTimeline.signal();
            }
            mBridgeCv.notify_all();
            if (mBridgeThread.joinable())
            {
                toJoin = std::move(mBridgeThread);
            }
        }
        if (toJoin.joinable())
        {
            toJoin.join();
        }
    }

    eastl::shared_ptr<GraphDevice> GraphDevice::acquire(RHI::DeviceImpl &device)
    {
        static std::mutex registryMutex;
        static eastl::vector<eastl::weak_ptr<GraphDevice>> devices;
        std::lock_guard lock(registryMutex);
        for (auto current = devices.begin(); current != devices.end();)
        {
            if (const auto retained = current->lock())
            {
                if (&retained->mDevice == &device)
                {
                    return retained;
                }
                ++current;
            }
            else
            {
                current = devices.erase(current);
            }
        }
        auto result = eastl::shared_ptr<GraphDevice>(new GraphDevice(device));
        devices.push_back(result);
        return result;
    }

    RHI::DeviceImpl &GraphDevice::device() const
    {
        return mDevice;
    }
    RHI::Queue GraphDevice::queue(uint32_t index) const
    {
        return mQueues.at(index);
    }
    PassQueues GraphDevice::candidates() const
    {
        return mCandidates;
    }

    bool GraphDevice::canReserveEncoderBudgetLocked(const QueueEncoderBudget &budget, uint32_t count, bool speculative) noexcept
    {
        if (count == 0u)
        {
            return true;
        }
        if (speculative)
        {
            if (budget.capacity <= budget.maxRootReservation)
            {
                return false;
            }
            for (const auto &waiter : budget.waiters)
            {
                if (!waiter.speculative)
                {
                    return false;
                }
            }
            const uint32_t speculativeLimit = budget.capacity - budget.maxRootReservation;
            return count <= speculativeLimit && budget.reserved <= speculativeLimit - count;
        }
        if (budget.reserved == 0u)
        {
            return true;
        }
        return count <= budget.capacity && budget.reserved <= budget.capacity - count;
    }

    uint32_t GraphDevice::maxBatchPassCount(uint32_t queueIndex) const noexcept
    {
        if (queueIndex >= mEncoderBudgets.size())
        {
            return 1u;
        }
        return mEncoderBudgets[queueIndex].maxBatchPasses;
    }

    bool GraphDevice::canReserveQueueEncoders(uint32_t queueIndex, uint32_t count, bool speculative) const noexcept
    {
        if (queueIndex >= mEncoderBudgets.size())
        {
            return false;
        }
        const QueueEncoderBudget &budget = mEncoderBudgets[queueIndex];
        std::lock_guard lock(budget.mutex);
        return canReserveEncoderBudgetLocked(budget, count, speculative);
    }

    bool GraphDevice::canEverReserveSpeculatively(uint32_t queueIndex, uint32_t count) const noexcept
    {
        if (queueIndex >= mEncoderBudgets.size())
        {
            return false;
        }
        const QueueEncoderBudget &budget = mEncoderBudgets[queueIndex];
        if (budget.capacity <= budget.maxRootReservation)
        {
            return false;
        }
        return count <= (budget.capacity - budget.maxRootReservation);
    }

    bool GraphDevice::tryReserveQueueEncoders(uint32_t queueIndex, uint32_t count, bool speculative) noexcept
    {
        if (queueIndex >= mEncoderBudgets.size())
        {
            return false;
        }
        QueueEncoderBudget &budget = mEncoderBudgets[queueIndex];
        std::lock_guard lock(budget.mutex);
        if (!canReserveEncoderBudgetLocked(budget, count, speculative))
        {
            return false;
        }
        budget.reserved += count;
        return true;
    }

    void GraphDevice::releaseQueueEncoders(uint32_t queueIndex, uint32_t count) noexcept
    {
        if (queueIndex >= mEncoderBudgets.size() || count == 0u)
        {
            return;
        }
        eastl::vector<eastl::function<void()>> toWake;
        {
            QueueEncoderBudget &budget = mEncoderBudgets[queueIndex];
            std::lock_guard lock(budget.mutex);
            budget.reserved = budget.reserved > count ? (budget.reserved - count) : 0u;
            if (!budget.waiters.empty())
            {
                toWake.reserve(budget.waiters.size());
                while (!budget.waiters.empty())
                {
                    toWake.push_back(eastl::move(budget.waiters.front().callback));
                    budget.waiters.pop_front();
                }
            }
        }
        for (auto &callback : toWake)
        {
            if (callback)
            {
                callback();
            }
        }
    }

    bool GraphDevice::waitForQueueEncoders(
        uint32_t queueIndex,
        uint32_t count,
        bool speculative,
        const void *owner,
        eastl::function<void()> callback)
    {
        if (queueIndex >= mEncoderBudgets.size() || !callback)
        {
            return false;
        }
        eastl::function<void()> replacedCallback;
        {
            QueueEncoderBudget &budget = mEncoderBudgets[queueIndex];
            std::lock_guard lock(budget.mutex);
            if (canReserveEncoderBudgetLocked(budget, count, speculative))
            {
                return false;
            }
            for (auto &existing : budget.waiters)
            {
                if (existing.owner == owner)
                {
                    existing.speculative = existing.speculative && speculative;
                    replacedCallback = eastl::move(existing.callback);
                    existing.callback = eastl::move(callback);
                    return true;
                }
            }
            budget.waiters.push_back({owner, speculative, eastl::move(callback)});
        }
        return true;
    }

    void GraphDevice::cancelQueueEncoderWaiters(const void *owner) noexcept
    {
        if (owner == nullptr)
        {
            return;
        }
        eastl::vector<eastl::function<void()>> cancelledCallbacks;
        for (auto &budget : mEncoderBudgets)
        {
            std::lock_guard lock(budget.mutex);
            for (auto it = budget.waiters.begin(); it != budget.waiters.end();)
            {
                if (it->owner == owner)
                {
                    cancelledCallbacks.push_back(eastl::move(it->callback));
                    it = budget.waiters.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }
    uint32_t GraphDevice::queueLoad(uint32_t index) const
    {
        return index < mQueueLoads.size() ? mQueueLoads[index].load(std::memory_order_relaxed) : 0u;
    }
    void GraphDevice::retainQueueLoad(uint32_t index)
    {
        if (index < mQueueLoads.size())
        {
            mQueueLoads[index].fetch_add(1u, std::memory_order_relaxed);
        }
    }
    void GraphDevice::releaseQueueLoad(uint32_t index)
    {
        if (index < mQueueLoads.size())
        {
            mQueueLoads[index].fetch_sub(1u, std::memory_order_relaxed);
        }
    }
    std::unique_lock<std::mutex> GraphDevice::lockResources()
    {
        return std::unique_lock<std::mutex>(mResourceMutex);
    }
    std::unique_lock<std::mutex> GraphDevice::lockQueue(uint32_t index)
    {
        if (index >= mQueues.size() || index >= mQueueMutexes.size())
        {
            throw std::out_of_range("GraphDevice queue index out of range.");
        }
        return std::unique_lock<std::mutex>(mQueueMutexes[index]);
    }

    bool GraphDevice::hasPendingCompletionsLocked() const noexcept
    {
        for (const auto &queueEntries : mPendingCompletions)
        {
            if (!queueEntries.empty())
            {
                return true;
            }
        }
        return false;
    }

    void GraphDevice::awaitSubmission(
        uint32_t queueIndex,
        RHI::Submission submission,
        eastl::function<void()> callback)
    {
        if (!callback)
        {
            return;
        }
        if (!submission.isValid())
        {
            callback();
            return;
        }
        if (submission.hasNativeCompletionCallback())
        {
            (void)submission.tryAddNativeCompletionCallback(eastl::move(callback));
            return;
        }
        try
        {
            if (submission.isComplete())
            {
                callback();
                return;
            }
        }
        catch (...)
        {
            callback();
            return;
        }

        if (queueIndex >= mPendingCompletions.size())
        {
            queueIndex = 0u;
        }

        const uint64_t serial = submission.timelineValue();
        bool fireInline = false;
        {
            std::lock_guard lock(mBridgeMutex);
            if (mBridgeStopping)
            {
                fireInline = true;
            }
            else
            {
                if (!mBridgeStarted)
                {
                    mHostTimeline = mDevice.createHostTimeline();
                    mBridgeThread = std::thread(&GraphDevice::runCompletionBridge, this);
                    mBridgeStarted = true;
                }
                auto &entries = mPendingCompletions[queueIndex];
                auto pos = entries.end();
                while (pos != entries.begin())
                {
                    auto prev = pos;
                    --prev;
                    if (prev->serial <= serial)
                    {
                        break;
                    }
                    pos = prev;
                }
                entries.insert(pos, PendingCompletion{serial, eastl::move(submission), eastl::move(callback)});
                const uint64_t newMin = entries.front().serial;
                if (mBridgeInKernelWait && newMin < mWaitedMinSerial[queueIndex] && mHostTimeline.isValid())
                {
                    (void)mHostTimeline.signal();
                }
                else if (!mBridgeInKernelWait)
                {
                    mBridgeCv.notify_one();
                }
            }
        }
        if (fireInline && callback)
        {
            callback();
        }
    }

    void GraphDevice::runCompletionBridge() noexcept
    {
        for (;;)
        {
            eastl::fixed_vector<RHI::Submission, 3, true> activeSubmissions;
            uint64_t hostTarget = 0;
            {
                std::unique_lock lock(mBridgeMutex);
                mBridgeCv.wait(lock, [this] { return mBridgeStopping || hasPendingCompletionsLocked(); });
                if (mBridgeStopping)
                {
                    break;
                }
                mWaitedMinSerial.fill(std::numeric_limits<uint64_t>::max());
                for (size_t q = 0; q < mPendingCompletions.size(); ++q)
                {
                    if (!mPendingCompletions[q].empty())
                    {
                        activeSubmissions.push_back(mPendingCompletions[q].front().submission);
                        mWaitedMinSerial[q] = mPendingCompletions[q].front().serial;
                    }
                }
                hostTarget = mHostTimeline.isValid() ? (mHostTimeline.currentValue() + 1u) : 0u;
                mBridgeInKernelWait = true;
            }

            bool waitFailed = false;
            try
            {
                (void)RHI::waitAny(
                    eastl::span<const RHI::Submission>(activeSubmissions.data(), activeSubmissions.size()),
                    mHostTimeline.isValid() ? &mHostTimeline : nullptr,
                    hostTarget);
            }
            catch (...)
            {
                waitFailed = true;
            }
            if (waitFailed && !activeSubmissions.empty())
            {
                bool anyObservedComplete = false;
                for (const auto &sub : activeSubmissions)
                {
                    try
                    {
                        if (sub.isComplete())
                        {
                            anyObservedComplete = true;
                            break;
                        }
                    }
                    catch (...)
                    {
                        anyObservedComplete = true;
                        break;
                    }
                }
                if (!anyObservedComplete)
                {
                    try
                    {
                        (void)activeSubmissions.front().wait();
                    }
                    catch (...)
                    {
                    }
                }
            }

            eastl::vector<eastl::function<void()>> ready;
            {
                std::lock_guard lock(mBridgeMutex);
                mBridgeInKernelWait = false;
                mWaitedMinSerial.fill(std::numeric_limits<uint64_t>::max());
                for (auto &entries : mPendingCompletions)
                {
                    while (!entries.empty())
                    {
                        bool done = false;
                        try
                        {
                            done = entries.front().submission.isComplete();
                        }
                        catch (...)
                        {
                            done = true;
                        }
                        if (!done)
                        {
                            break;
                        }
                        ready.push_back(eastl::move(entries.front().callback));
                        entries.pop_front();
                    }
                }
            }

            for (auto &cb : ready)
            {
                if (cb)
                {
                    cb();
                    cb = nullptr;
                }
            }
        }

        eastl::vector<eastl::function<void()>> remaining;
        {
            std::lock_guard lock(mBridgeMutex);
            for (auto &entries : mPendingCompletions)
            {
                while (!entries.empty())
                {
                    remaining.push_back(eastl::move(entries.front().callback));
                    entries.pop_front();
                }
            }
        }
        for (auto &cb : remaining)
        {
            if (cb)
            {
                cb();
                cb = nullptr;
            }
        }
    }
}
