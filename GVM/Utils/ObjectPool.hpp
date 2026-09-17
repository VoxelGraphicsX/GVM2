#pragma once
#include <EASTL/deque.h>
#include <functional>
namespace xGE::Utils
{
    template <class Type>
    class ObjectPool
    {
        eastl::deque<Type*> objects;
        using AllocFunc = std::function<Type*()>;
        using DeallocFunc = std::function<void(Type*)>;
        AllocFunc mAllocFunc;
        DeallocFunc mDeallocFunc;
        public:
        void init(const AllocFunc& afn, const DeallocFunc& dfn)
        {
            this->mAllocFunc = afn;
            this->mDeallocFunc = dfn;
        }
        Type* acquire()
        {
            if(objects.empty())
            {
                return this->mAllocFunc();
            }
            else
            {
                auto object = objects.front();
                objects.pop_front();
                return object;
            }
        }
        void collect(Type* object)
        {
            objects.emplace_back(object);
        }
        void destroy()
        {
            for(auto object:objects)
            {
                this->mDeallocFunc(object);
            }
        }
    };

}