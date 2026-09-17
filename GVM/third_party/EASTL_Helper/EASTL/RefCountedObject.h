#ifndef EASTL_REFCOUNTEDOBJECT_H
#define EASTL_REFCOUNTEDOBJECT_H
#include <EASTL/atomic.h>
namespace eastl
{

    class RefCountedObject
    {
        eastl::atomic<int> mRefCount = 0;

    public:
        virtual void AddRef() { mRefCount++; };
        virtual void Release()
        {
            mRefCount -= 1;
            if (mRefCount == 0)
            {
                delete this;
            }
        };
        virtual ~RefCountedObject() = default;
    };

}

#endif