#pragma once
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
    // ======================================================================================
    // [优化核心 1] 线性分配器 (CPU Staging Arena)
    // 作用：解决内存碎片，替代 new/delete，提供极快的数据追加能力。
    // 所有用户线程产生的数据直接追加到这里，形成一个紧凑的内存块。
    // ======================================================================================
    class StagingLinearAllocator
    {
    public:
        void reserve(size_t capacity);

        void reset();

        // 核心接口：追加数据并返回由于对齐产生的相对偏移量
        // T 约束为 TriviallyCopyable，确保 memcpy 安全
        template <typename T>
        uint64_t append(const T *data, size_t count, size_t align = 64)
        {
            if (count == 0 || data == nullptr)
            {
                return mData.size();
            }
            size_t bytes = sizeof(T) * count;
            return appendRaw(data, bytes, align);
        }

        uint64_t appendRaw(const void *data, size_t bytes, size_t align = 64);
        uint64_t appendUninitialized(size_t bytes, size_t align = 64);

        const uint8_t *data() const;
        uint8_t *mutableData();
        size_t size() const;
        bool empty() const;

    private:
        eastl::vector<uint8_t> mData;
    };

} // namespace GVM::Core
