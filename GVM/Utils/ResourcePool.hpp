#pragma once
#include <EASTL/atomic.h>
#include <EASTL/deque.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/vector.h>
#include <shared_mutex>
#include <stdexcept>

namespace xGE::Utils
{
    namespace
    {
        using ResourceIndexNumericType = uint32_t;
    }
    template <class Type>
    class ResourcePool;

    template <class Type>
    struct ResourceHandle
    {
        friend class ResourcePool<Type>;

    public:
        // ResourceIndexNumericType getID() const { return id; }
        // uint32_t getGen() const { return gen; }
        [[nodiscard]]
        Type *get() const
        {
            if (pool == nullptr || id == eastl::numeric_limits<ResourceIndexNumericType>::max())
            {
                return nullptr;
            }
            return pool->getByHandle(*this);
        }
        [[nodiscard]]
        Type *operator->() const
        {
            return get();
        }
        [[nodiscard]]
        bool isNull() const
        {
            return get() == nullptr;
        }
        void reset()
        {
            pool = nullptr;
            id = eastl::numeric_limits<ResourceIndexNumericType>::max();
            gen = 0;
        }


        bool operator==(const ResourceHandle<Type> &other) const
        {
            return pool == other.pool && id == other.id && gen == other.gen;
        }

        /**
         * @brief 成员 swap 函数
         * 执行成员变量的交换。标记为 noexcept 是非常重要的。
         */
        void swap(ResourceHandle &other) noexcept
        {
            eastl::swap(pool, other.pool);
            eastl::swap(id, other.id);
            eastl::swap(gen, other.gen);
        }

    private:
        ResourcePool<Type> *pool = nullptr;
        ResourceIndexNumericType id = eastl::numeric_limits<ResourceIndexNumericType>::max();
        uint32_t gen = 0;
    };

    template <class Type>
    class ResourcePool final
    {

        struct ResourceInfo
        {
            Type *resource = nullptr;
            uint32_t gen = 0;
        };

        eastl::vector<ResourceInfo> data;
        ResourceIndexNumericType poolSize = 0;
        eastl::deque<ResourceIndexNumericType> freelist = {};

        mutable std::shared_mutex mtx;

    public:
        void init()
        {
            constexpr size_t kInitialCapacity = 128;
            data.clear();
            data.resize(kInitialCapacity);
            freelist.clear();
            poolSize = 0;
        }
        [[nodiscard]]
        ResourceHandle<Type> alloc(Type *t)
        {
            if (t == nullptr)
            {
                throw std::invalid_argument("ResourcePool::alloc requires a non-null resource.");
            }
            std::unique_lock lock(mtx); // 写锁
            ResourceHandle<Type> handle;
            handle.pool = this;
            if (freelist.empty())
            {
                if (poolSize == eastl::numeric_limits<ResourceIndexNumericType>::max())
                {
                    throw std::overflow_error("ResourcePool exhausted ResourceIndexNumericType capacity.");
                }
                handle.id = poolSize++;
                if (handle.id >= data.size())
                {
                    const size_t currentSize = data.empty() ? 0u : data.size();
                    const size_t grownSize = currentSize == 0u ? size_t(128) : currentSize * 2u;
                    data.resize(grownSize);
                }
            }
            else
            {
                handle.id = freelist.back();
                freelist.pop_back();
            }
            handle.gen = data[handle.id].gen;
            data[handle.id].resource = t;
            return handle;
        }
        [[nodiscard]]
        Type *getByHandle(ResourceHandle<Type> handle) const
        {
            if (handle.pool != this)
            {
                return nullptr;
            }
            std::shared_lock lock(mtx); // 读锁
            if (handle.id >= poolSize)
            {
                return nullptr;
            }
            const auto &resourceInfo = data[handle.id];
            if (resourceInfo.gen == handle.gen)
            {
                return resourceInfo.resource;
            }
            return nullptr;
        }
        void freeByHandle(ResourceHandle<Type> handle)
        {
            if (handle.pool != this)
            {
                return;
            }
            std::unique_lock lock(mtx);
            if (handle.id >= poolSize)
            {
                return;
            }

            auto &resourceInfo = data[handle.id];
            if (resourceInfo.gen != handle.gen || resourceInfo.resource == nullptr)
            {
                return;
            }
            resourceInfo.gen++;
            freelist.emplace_front(handle.id);
            delete resourceInfo.resource;
            resourceInfo.resource = nullptr;
        }
    };

} // namespace xGE::Utils
