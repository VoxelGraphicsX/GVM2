#pragma once
#include <cstdint>
#include <type_traits>

namespace UGL
{
    template <class T>
    concept AtomicScalar = std::is_same_v<T, int> || std::is_same_v<T, uint32_t>;

    template <class T>
    class GroupShared
    {
    public:
        operator T()
        {
            return T();
        }
        T operator=(const T &other)
        {
            return T();
        }
    };

    /** Performs an atomic add on a plain scalar lvalue; UGLC creates an atomic view of the original storage address without changing the declared resource layout. */
    template <AtomicScalar T, class U>
    T atomicAdd(T &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic add on a plain groupshared scalar lvalue; UGLC creates an atomic view of the original workgroup address without changing its declared layout. */
    template <AtomicScalar T, class U>
    T atomicAdd(GroupShared<T> &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic OR on a plain scalar lvalue; the declared resource type does not need to expose a legacy atomic wrapper. */
    template <AtomicScalar T, class U>
    T atomicOr(T &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic OR on a plain groupshared scalar lvalue without requiring a legacy atomic wrapper in DSL code. */
    template <AtomicScalar T, class U>
    T atomicOr(GroupShared<T> &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic AND on a plain scalar lvalue; UGLC creates an atomic view of the original storage address without changing the declared resource layout. */
    template <AtomicScalar T, class U>
    T atomicAnd(T &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic AND on a plain groupshared scalar lvalue without requiring a legacy atomic wrapper in DSL code. */
    template <AtomicScalar T, class U>
    T atomicAnd(GroupShared<T> &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Loads a storage scalar with acquire ordering; the caller keeps ordinary scalar resource declarations. */
    template <AtomicScalar T>
    T atomicLoad(const T &atom)
    {
        return atom;
    }

    /** Stores a storage scalar with release ordering through an operation-local backend atomic view. */
    template <AtomicScalar T, class U>
    void atomicStore(T &atom, U value)
    {
    }

    /** Loads a groupshared scalar with acquire ordering without requiring an atomic-qualified DSL declaration. */
    template <AtomicScalar T>
    T atomicLoad(const GroupShared<T> &atom)
    {
        return T();
    }

    /** Stores a groupshared scalar with release ordering without changing the declared workgroup layout. */
    template <AtomicScalar T, class U>
    void atomicStore(GroupShared<T> &atom, U value)
    {
    }

    /** Performs strong compare-exchange on storage with acquire/release success and acquire failure ordering, returning the original value through the output. */
    template <AtomicScalar T>
    void atomicCompareExchange(T &atom, T compare, T value, T &originalValue)
    {
    }

    /** Performs strong compare-exchange on groupshared storage with acquire/release success and acquire failure ordering, returning the original value through the output. */
    template <AtomicScalar T>
    void atomicCompareExchange(GroupShared<T> &atom, T compare, T value, T &originalValue)
    {
    }

    /** Performs an atomic max on a plain scalar lvalue; unsupported scalar types are rejected during C++ overload resolution. */
    template <AtomicScalar T, class U>
    void atomicMax(T &atom, U value)
    {
    }

    /** Performs an atomic max on a plain groupshared scalar lvalue; unsupported scalar types are rejected during C++ overload resolution. */
    template <AtomicScalar T, class U>
    void atomicMax(GroupShared<T> &atom, U value)
    {
    }

    /** Performs an atomic min on a plain scalar lvalue; unsupported scalar types are rejected during C++ overload resolution. */
    template <AtomicScalar T, class U>
    void atomicMin(T &atom, U value)
    {
    }

    /** Performs an atomic min on a plain groupshared scalar lvalue; unsupported scalar types are rejected during C++ overload resolution. */
    template <AtomicScalar T, class U>
    void atomicMin(GroupShared<T> &atom, U value)
    {
    }

} // namespace UGL
