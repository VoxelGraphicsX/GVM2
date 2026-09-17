#include <gtest/gtest.h>
#include <EASTL/allocator.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

/** Supplies an over-aligned element to exercise EASTL container growth and destruction. */
struct alignas(128) AllocatorAlignedElement
{
    uint32_t value = 0;
};

/** Verifies offset alignment and writable payloads across allocation sizes and alignments. */
TEST(EastlAllocator, HonorsAlignmentAndOffset)
{
    eastl::allocator allocator;
    for (size_t size : {size_t{0}, size_t{1}, size_t{7}, size_t{80}, size_t{192}, size_t{4097}})
    {
        for (size_t alignment : {size_t{1}, size_t{8}, size_t{16}, size_t{64}, size_t{128}, size_t{4096}})
        {
            for (size_t offset : {size_t{0}, size_t{1}, size_t{3}, size_t{63}, size_t{129}, size_t{4099}})
            {
                void *storage = allocator.allocate(size, alignment, offset);
                EXPECT_EQ((reinterpret_cast<uintptr_t>(storage) + offset) % alignment, 0u);
                std::memset(storage, 0xA5, size);
                allocator.deallocate(storage, size);
            }
        }
    }
}

/** Retains simultaneous allocations to avoid size-class coincidences masking misalignment. */
TEST(EastlAllocator, AlignsSimultaneousAllocations)
{
    eastl::allocator allocator;
    void *allocations[128] = {};
    for (void *&storage : allocations)
    {
        storage = allocator.allocate(80, 64, 0);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(storage) % 64u, 0u);
    }
    for (void *storage : allocations)
    {
        allocator.deallocate(storage, 80);
    }
}

/** Checks fundamental alignment and deallocation compatibility across allocator instances. */
TEST(EastlAllocator, SharesOrdinaryStorageAndAcceptsNull)
{
    eastl::allocator first;
    eastl::allocator second(first);
    void *storage = first.allocate(81);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(storage) % alignof(std::max_align_t), 0u);
    EXPECT_EQ(first, second);
    second.deallocate(storage, 81);
    second.deallocate(nullptr, 0);
}

/** Exercises real container reallocations while checking element alignment and retained values. */
TEST(EastlAllocator, PreservesOveralignedContainers)
{
    eastl::vector<AllocatorAlignedElement> values;
    for (uint32_t index = 0; index < 257; ++index)
    {
        values.push_back({index});
        EXPECT_EQ(reinterpret_cast<uintptr_t>(values.data()) % alignof(AllocatorAlignedElement), 0u);
    }
    values.reserve(1024);
    for (uint32_t index = 0; index < values.size(); ++index)
    {
        EXPECT_EQ(values[index].value, index);
    }
    values.clear();
    values.shrink_to_fit();
}

/** Rejects invalid alignment and arithmetic overflow before requesting system storage. */
TEST(EastlAllocator, RejectsInvalidCapacity)
{
    eastl::allocator allocator;
    EXPECT_THROW(allocator.allocate(8, 0, 0), std::bad_alloc);
    EXPECT_THROW(allocator.allocate(8, 3, 0), std::bad_alloc);
    EXPECT_THROW(allocator.allocate(std::numeric_limits<size_t>::max(), 64, 0), std::bad_alloc);
}
