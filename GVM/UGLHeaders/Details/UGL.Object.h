#pragma once
#include <concepts>
#include <type_traits>
namespace UGL
{
    // DSL object-model contract:
    // - UGL source is not allowed to model ownership or access through raw pointers.
    // - `SPTR<T>` is a DSL/runtime object handle abstraction, not permission for user-authored
    //   raw-pointer programming.
    // - The only pointer semantics allowed in user-authored DSL code is the implicit `this`
    //   of the current class member function.
    class UObject
    {
    public:
        UObject() = default;
        virtual ~UObject() = default;
    };

    template <class T>
        requires std::is_base_of_v<UObject, T>
    class SPTR
    {
    public:
        SPTR() = default;
        virtual ~SPTR() = default;
        T *operator->()
        {
            return nullptr;
        }
        bool operator==(std::nullptr_t) const
        {
            return true;
        }

        bool operator!=(std::nullptr_t) const
        {
            return !(*this == nullptr);
        }

        void reset()
        {
            // Implementation
        }
    };

    template <class T>
    concept IsUObject = std::is_base_of_v<UObject, T>;

    template <class T, class... Args>
    SPTR<T> MakeSPTR(Args &&...args)
    {
        return SPTR<T>();
    }
} // namespace UGL
