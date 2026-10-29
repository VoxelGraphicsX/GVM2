#include <gtest/gtest.h>

#include <EASTL/atomic.h>
#include <EASTL/intrusive_ptr.h>
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>

#include <atomic>
#include <cstddef>
#include <thread>

namespace
{
    /** Counts its own destruction so tests can assert exactly-once delete under concurrent release. */
    class DestructionCountedObject final : public GVM::RHI::RefCountedObject
    {
    public:
        explicit DestructionCountedObject(eastl::atomic<int> &destructions)
            : mDestructions(destructions)
        {
        }

        ~DestructionCountedObject() override
        {
            mDestructions.fetch_add(1, eastl::memory_order_relaxed);
        }

    private:
        eastl::atomic<int> &mDestructions;
    };

    TEST(RefCountedObjectTests, ConcurrentReleasesDestroyTheObjectExactlyOnce)
    {
        constexpr size_t kObjectCount = 20000;

        eastl::atomic<int> destructions{0};
        eastl::vector<eastl::intrusive_ptr<DestructionCountedObject>> firstReferences;
        eastl::vector<eastl::intrusive_ptr<DestructionCountedObject>> secondReferences;
        firstReferences.reserve(kObjectCount);
        secondReferences.reserve(kObjectCount);
        for (size_t index = 0; index < kObjectCount; ++index)
        {
            DestructionCountedObject *object = new DestructionCountedObject(destructions);
            firstReferences.emplace_back(object);
            secondReferences.emplace_back(object);
        }

        std::atomic<bool> start{false};
        auto releaseAll = [&start](eastl::vector<eastl::intrusive_ptr<DestructionCountedObject>> &references) {
            while (!start.load(std::memory_order_acquire))
            {
            }
            for (auto &reference : references)
            {
                reference.reset();
            }
        };
        std::thread first(releaseAll, std::ref(firstReferences));
        std::thread second(releaseAll, std::ref(secondReferences));
        start.store(true, std::memory_order_release);
        first.join();
        second.join();

        EXPECT_EQ(destructions.load(eastl::memory_order_relaxed), static_cast<int>(kObjectCount));
    }

    TEST(RefCountedObjectTests, ReferenceCountFollowsIntrusivePtrCopiesAndResets)
    {
        eastl::atomic<int> destructions{0};
        auto *object = new DestructionCountedObject(destructions);
        eastl::intrusive_ptr<DestructionCountedObject> first(object);
        EXPECT_EQ(object->getReferenceCount(), 1);

        {
            eastl::intrusive_ptr<DestructionCountedObject> second(first);
            EXPECT_EQ(object->getReferenceCount(), 2);
            eastl::intrusive_ptr<DestructionCountedObject> third = second;
            EXPECT_EQ(object->getReferenceCount(), 3);
            third.reset();
            EXPECT_EQ(object->getReferenceCount(), 2);
        }
        EXPECT_EQ(object->getReferenceCount(), 1);

        first.reset();
        EXPECT_EQ(destructions.load(eastl::memory_order_relaxed), 1);
    }
} // namespace
