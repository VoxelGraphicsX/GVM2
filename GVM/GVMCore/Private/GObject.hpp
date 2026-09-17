#pragma once
#include <EASTL/atomic.h>
#include <EASTL/intrusive_ptr.h>
namespace GVM::Core
{
    class UObject
    {
        eastl::atomic<int> mRefCount = 0;

    public:
        virtual void AddRef();
        virtual void Release();
        virtual ~UObject() = default;
    };

    template <class T>
    using SPTR = eastl::intrusive_ptr<T>;

    template <class T, class... Args>
    SPTR<T> MakeSPTR(Args &&...args)
    {
        return new T(args...);
    }
}