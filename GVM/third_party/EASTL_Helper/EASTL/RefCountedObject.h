#ifndef EASTL_REFCOUNTEDOBJECT_H
#define EASTL_REFCOUNTEDOBJECT_H
#include <EASTL/atomic.h>
namespace eastl
{

    class RefCountedObject
    {
        eastl::atomic<int> mRefCount = 0;

    public:
        virtual void AddRef() { mRefCount.fetch_add(1, eastl::memory_order_relaxed); };
        virtual void Release()
        {
            if (mRefCount.fetch_sub(1, eastl::memory_order_acq_rel) == 1)
            {
                delete this;
            }
        };
        virtual ~RefCountedObject() = default;
    };

}

#endif