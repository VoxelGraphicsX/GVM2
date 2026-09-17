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

    /** Performs an atomic add on a plain scalar lvalue; UGLC lowers the storage layout when a backend requires atomic-qualified resources. */
    template <AtomicScalar T, class U>
    T atomicAdd(T &atom, U value)
    {
        return static_cast<T>(value);
    }

    /** Performs an atomic add on a plain groupshared scalar lvalue; UGLC lowers the groupshared layout when a backend requires atomic-qualified storage. */
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

    /** Performs an atomic AND on a plain scalar lvalue; UGLC lowers the storage layout when a backend requires atomic-qualified resources. */
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

    /** Loads a plain scalar lvalue through the atomic builtin surface so HLSL-style DSL code can omit legacy atomic resource wrappers. */
    template <AtomicScalar T>
    T atomicLoad(const T &atom)
    {
        return atom;
    }

    /** Stores a plain scalar lvalue through the atomic builtin surface so UGLC can infer backend atomic layout from use sites. */
    template <AtomicScalar T, class U>
    void atomicStore(T &atom, U value)
    {
    }

    /** Loads a plain groupshared scalar lvalue through the atomic builtin surface without requiring a legacy atomic wrapper. */
    template <AtomicScalar T>
    T atomicLoad(const GroupShared<T> &atom)
    {
        return T();
    }

    /** Stores a plain groupshared scalar lvalue through the atomic builtin surface without requiring a legacy atomic wrapper. */
    template <AtomicScalar T, class U>
    void atomicStore(GroupShared<T> &atom, U value)
    {
    }

    /** Performs an atomic compare-exchange on a plain scalar lvalue and writes the original value to the caller-provided output. */
    template <AtomicScalar T>
    void atomicCompareExchange(T &atom, T compare, T value, T &originalValue)
    {
    }

    /** Performs an atomic compare-exchange on a plain groupshared scalar lvalue and writes the original value to the caller-provided output. */
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
