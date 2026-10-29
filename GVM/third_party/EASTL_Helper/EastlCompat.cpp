#include <EASTL/allocator.h>
#include <EASTL/string.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

/** Formats EASTL narrow strings using the platform's standard bounded formatter. */
int Vsnprintf8(char *destination, size_t capacity, const char *format, va_list arguments)
{
    return vsnprintf(destination, capacity, format, arguments);
}

namespace eastl
{
    /** Creates an allocator using the optional diagnostic name without taking ownership of it. */
    allocator::allocator(const char *name)
    {
        set_name(name ? name : EASTL_ALLOCATOR_DEFAULT_NAME);
    }

    /** Copies the diagnostic name; all GVM EASTL allocators share the same allocation mechanism. */
    allocator::allocator(const allocator &other)
    {
        set_name(other.get_name());
    }

    /** Creates a compatible allocator with a replacement diagnostic name. */
    allocator::allocator(const allocator &, const char *name)
    {
        set_name(name ? name : EASTL_ALLOCATOR_DEFAULT_NAME);
    }

    /** Copies allocator metadata while preserving allocation and deallocation compatibility. */
    allocator &allocator::operator=(const allocator &other)
    {
        set_name(other.get_name());
        return *this;
    }

    /** Returns the configured diagnostic name, or the EASTL default when names are disabled. */
    const char *allocator::get_name() const
    {
#if EASTL_NAME_ENABLED
        return mpName;
#else
        return EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
    }

    /** Sets a borrowed diagnostic name when the EASTL build enables allocator names. */
    void allocator::set_name(const char *name)
    {
#if EASTL_NAME_ENABLED
        mpName = name;
#else
        (void)name;
#endif
    }

    /** Allocates ordinary storage with fundamental alignment; release it with allocator::deallocate. */
    void *allocator::allocate(size_t size, int flags)
    {
        return allocate(size, alignof(std::max_align_t), 0, flags);
    }

    /** Allocates storage so address plus offset meets a nonzero power-of-two alignment.
     *  Throws std::bad_alloc for invalid alignment, capacity overflow, or allocation failure.
     *  The returned storage must be released with allocator::deallocate.
     */
    void *allocator::allocate(size_t size, size_t alignment, size_t offset, int)
    {
        constexpr size_t maximumSize = std::numeric_limits<size_t>::max();
        if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
            alignment - 1 > maximumSize - sizeof(void *))
        {
            throw std::bad_alloc();
        }
        const size_t overhead = sizeof(void *) + alignment - 1;
        const size_t payloadSize = size == 0 ? 1 : size;
        if (payloadSize > maximumSize - overhead)
        {
            throw std::bad_alloc();
        }
        void *allocation = std::malloc(payloadSize + overhead);
        if (allocation == nullptr)
        {
            throw std::bad_alloc();
        }
        auto *storage = static_cast<unsigned char *>(allocation) + sizeof(void *);
        const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
        const size_t mask = alignment - 1;
        const size_t padding = (alignment - ((address + (offset & mask)) & mask)) & mask;
        storage += padding;
        // A nonzero offset may leave the metadata unaligned, so use byte-wise copying.
        std::memcpy(storage - sizeof(void *), &allocation, sizeof(allocation));
        return storage;
    }

    /** Releases storage from either allocation overload; accepting nullptr matches EASTL's contract. */
    void allocator::deallocate(void *storage, size_t)
    {
        if (storage != nullptr)
        {
            void *allocation = nullptr;
            std::memcpy(&allocation, static_cast<unsigned char *>(storage) - sizeof(void *), sizeof(allocation));
            std::free(allocation);
        }
    }

    /** Reports interchangeability because all GVM EASTL allocators use the same storage format. */
    bool operator==(const allocator &, const allocator &)
    {
        return true;
    }

#if !defined(EA_COMPILER_HAS_THREE_WAY_COMPARISON)
    /** Reports that no two GVM EASTL allocators have incompatible allocation mechanisms. */
    bool operator!=(const allocator &, const allocator &)
    {
        return false;
    }
#endif

    allocator gDefaultAllocator;
    allocator *gpDefaultAllocator = &gDefaultAllocator;

    /** Returns the borrowed default allocator used by EASTL's default allocator adapters. */
    allocator *GetDefaultAllocator()
    {
        return gpDefaultAllocator;
    }

    /** Replaces the borrowed EASTL default allocator and returns the previous pointer. */
    allocator *SetDefaultAllocator(allocator *allocatorInstance)
    {
        allocator *previousAllocator = gpDefaultAllocator;
        gpDefaultAllocator = allocatorInstance;
        return previousAllocator;
    }
}
